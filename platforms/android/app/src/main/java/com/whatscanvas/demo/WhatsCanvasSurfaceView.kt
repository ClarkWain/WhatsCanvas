package com.whatscanvas.demo

import android.content.Context
import android.os.Build
import android.opengl.GLSurfaceView
import android.util.Log
import android.view.Choreographer
import android.view.Surface
import android.view.SurfaceHolder
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class WhatsCanvasSurfaceView(
    context: Context,
    captureTimeSeconds: Float? = null,
    sceneId: String = "feature_showcase"
) : GLSurfaceView(context) {
    private val canvasRenderer = WhatsCanvasRenderer(sceneId) {
        resources.displayMetrics.density
    }.also { it.captureTimeSeconds = captureTimeSeconds }
    private val choreographer = Choreographer.getInstance()
    private var frameCallbackScheduled = false
    private var rendering = false
    private val frameCallback = Choreographer.FrameCallback {
        frameCallbackScheduled = false
        if (rendering) {
            requestRender()
            scheduleNextFrame()
        }
    }

    init {
        setEGLContextClientVersion(3)
        setEGLConfigChooser(8, 8, 8, 8, 24, 8)
        // Match Flutter's normal mobile lifecycle: pausing rendering should
        // not eagerly discard a healthy GPU context and all derived caches.
        // Android may still revoke it under memory pressure; onSurfaceCreated
        // remains the authoritative context-loss signal.
        preserveEGLContextOnPause = true
        setRenderer(canvasRenderer)
        // Rendering is paced by Android's display VSYNC. GLSurfaceView's
        // continuous mode runs independently of SurfaceFlinger and can render
        // frames that are never presented, wasting CPU and battery.
        renderMode = RENDERMODE_WHEN_DIRTY
    }

    fun startRendering() {
        if (rendering) return
        rendering = true
        scheduleNextFrame()
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        super.surfaceCreated(holder)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                // Some OEM display managers keep the previous physical mode
                // when only a seamless switch is allowed (for example 50 Hz
                // even though this continuously animated surface asks for
                // 60 Hz). Permit the platform to perform a mode switch.
                holder.surface.setFrameRate(
                    ANIMATION_REFRESH_RATE_HZ,
                    Surface.FRAME_RATE_COMPATIBILITY_DEFAULT,
                    Surface.CHANGE_FRAME_RATE_ALWAYS
                )
            } else {
                holder.surface.setFrameRate(
                    ANIMATION_REFRESH_RATE_HZ,
                    Surface.FRAME_RATE_COMPATIBILITY_DEFAULT
                )
            }
        }
    }

    fun stopRendering() {
        rendering = false
        if (frameCallbackScheduled) {
            choreographer.removeFrameCallback(frameCallback)
            frameCallbackScheduled = false
        }
    }

    private fun scheduleNextFrame() {
        if (!rendering || frameCallbackScheduled) return
        frameCallbackScheduled = true
        choreographer.postFrameCallback(frameCallback)
    }

    private companion object {
        const val ANIMATION_REFRESH_RATE_HZ = 60.0f
    }

    fun releaseNativeRenderer() {
        queueEvent { canvasRenderer.destroyOnGlThread() }
    }

    fun ensureNativeRenderer() {
        canvasRenderer.ensureNativeRenderer()
    }
}

class WhatsCanvasRenderer(
    private val sceneId: String,
    private val densityProvider: () -> Float
) : GLSurfaceView.Renderer {
    private val nanoVG = sceneId.startsWith(NANOVG_SCENE_PREFIX)
    private val nativeSceneId = sceneId.removePrefix(NANOVG_SCENE_PREFIX)
    private val startedAtNanos = System.nanoTime()
    private var frameWindowStartedAtNanos = 0L
    private var frameWindowCount = 0
    @Volatile
    private var nativeHandle: Long = if (nanoVG) {
        nativeNanoCreate(nativeSceneId)
    } else {
        nativeCreate(sceneId)
    }
    var captureTimeSeconds: Float? = null

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        val initialized = if (nanoVG) {
            nativeNanoSurfaceCreated(nativeHandle)
        } else {
            nativeSurfaceCreated(nativeHandle)
        }
        if (!initialized) {
            Log.e(TAG, "Unable to initialize the OpenGL ES function loader")
        }
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        val resized = if (nanoVG) {
            nativeNanoResize(nativeHandle, width, height, densityProvider())
        } else {
            nativeResize(nativeHandle, width, height, densityProvider())
        }
        if (!resized) {
            Log.e(TAG, "Unable to create the WhatsCanvas OpenGLES renderer")
        }
    }

    override fun onDrawFrame(gl: GL10?) {
        val nowNanos = System.nanoTime()
        val elapsedSeconds = captureTimeSeconds
            ?: (nowNanos - startedAtNanos) / 1_000_000_000.0f
        if (nanoVG) {
            nativeNanoRender(nativeHandle, elapsedSeconds)
        } else {
            nativeRender(nativeHandle, elapsedSeconds)
        }

        if (frameWindowStartedAtNanos == 0L) {
            frameWindowStartedAtNanos = nowNanos
        }
        frameWindowCount++
        val windowNanos = nowNanos - frameWindowStartedAtNanos
        if (windowNanos >= FRAME_LOG_INTERVAL_NANOS) {
            val renderedFps = frameWindowCount * 1_000_000_000.0 / windowNanos
            val backend = if (nanoVG) "NanoVG" else "WhatsCanvas"
            Log.i(TAG, "Frame pacing: backend=%s renderedFps=%.1f frames=%d".format(
                backend, renderedFps, frameWindowCount))
            frameWindowStartedAtNanos = nowNanos
            frameWindowCount = 0
        }
    }

    fun ensureNativeRenderer() {
        if (nativeHandle == 0L) {
            nativeHandle = if (nanoVG) {
                nativeNanoCreate(nativeSceneId)
            } else {
                nativeCreate(sceneId)
            }
        }
    }

    fun destroyOnGlThread() {
        val handle = nativeHandle
        if (handle != 0L) {
            if (nanoVG) nativeNanoDestroy(handle) else nativeDestroy(handle)
            nativeHandle = 0L
        }
    }

    private external fun nativeCreate(sceneId: String): Long
    private external fun nativeSurfaceCreated(handle: Long): Boolean
    private external fun nativeResize(handle: Long, width: Int, height: Int, density: Float): Boolean
    private external fun nativeRender(handle: Long, elapsedSeconds: Float)
    private external fun nativeDestroy(handle: Long)
    private external fun nativeNanoCreate(sceneId: String): Long
    private external fun nativeNanoSurfaceCreated(handle: Long): Boolean
    private external fun nativeNanoResize(
        handle: Long, width: Int, height: Int, density: Float
    ): Boolean
    private external fun nativeNanoRender(handle: Long, elapsedSeconds: Float)
    private external fun nativeNanoDestroy(handle: Long)

    private companion object {
        const val TAG = "WhatsCanvas"
        const val NANOVG_SCENE_PREFIX = "nanovg_"
        const val FRAME_LOG_INTERVAL_NANOS = 5_000_000_000L

        init {
            System.loadLibrary("whatscanvas_android")
            System.loadLibrary("nanovg_android")
        }
    }
}
