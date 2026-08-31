package com.whatscanvas.spider

import android.content.Context
import android.opengl.GLSurfaceView
import android.os.Build
import android.util.Log
import android.view.Choreographer
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class SpiderSurfaceView(context: Context) : GLSurfaceView(context) {
    private val renderer = SpiderRenderer()
    private val choreographer = Choreographer.getInstance()
    private var frameCallbackScheduled = false
    private var rendering = false
    private var activePointerId = INVALID_POINTER_ID
    // The GL surface buffer is smaller than the physical View when
    // SurfaceHolder.setFixedSize is used (see surfaceCreated). MotionEvent
    // coordinates are in View pixels, so multiply by the current buffer/view
    // ratio before forwarding to the native renderer.
    private var touchScaleX = 1.0f
    private var touchScaleY = 1.0f

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
        preserveEGLContextOnPause = true
        setRenderer(renderer)
        // Rendering is paced by Android's display VSYNC via Choreographer.
        // Compared with RENDERMODE_CONTINUOUSLY the pace is much steadier on
        // both reference devices because it avoids the swap/present backlog
        // GLSurfaceView otherwise builds up when render is momentarily slow.
        renderMode = RENDERMODE_WHEN_DIRTY
        isFocusable = true
        isFocusableInTouchMode = true
    }

    fun startRendering() {
        if (rendering) return
        rendering = true
        scheduleNextFrame()
    }

    fun stopRendering() {
        rendering = false
        if (frameCallbackScheduled) {
            choreographer.removeFrameCallback(frameCallback)
            frameCallbackScheduled = false
        }
    }

    fun ensureNativeRenderer() {
        renderer.ensureNativeRenderer()
    }

    fun releaseNativeRenderer() {
        queueEvent { renderer.destroyOnGlThread() }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        super.surfaceCreated(holder)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
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

    override fun onTouchEvent(event: MotionEvent): Boolean {
        // Convert View pixel coordinates to buffer coordinates, since the GL
        // surface is smaller than the physical panel (see surfaceCreated).
        val sx = touchScaleX
        val sy = touchScaleY
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                activePointerId = event.getPointerId(0)
                val x = event.getX(0) * sx
                val y = event.getY(0) * sy
                queueEvent { renderer.pointerDown(x, y) }
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                // Multi-touch: only track the first finger. Additional
                // fingers that land while a run is being dragged are
                // ignored so that stray palm contacts do not scramble the
                // primary interaction.
            }
            MotionEvent.ACTION_MOVE -> {
                val index = event.findPointerIndex(activePointerId)
                if (index >= 0) {
                    val x = event.getX(index) * sx
                    val y = event.getY(index) * sy
                    queueEvent { renderer.pointerMove(x, y) }
                }
            }
            MotionEvent.ACTION_UP -> {
                val index = event.findPointerIndex(activePointerId)
                val x = if (index >= 0) event.getX(index) * sx else event.x * sx
                val y = if (index >= 0) event.getY(index) * sy else event.y * sy
                queueEvent { renderer.pointerUp(x, y) }
                activePointerId = INVALID_POINTER_ID
            }
            MotionEvent.ACTION_POINTER_UP -> {
                val pointerId = event.getPointerId(event.actionIndex)
                if (pointerId == activePointerId) {
                    val x = event.getX(event.actionIndex) * sx
                    val y = event.getY(event.actionIndex) * sy
                    queueEvent { renderer.pointerUp(x, y) }
                    activePointerId = INVALID_POINTER_ID
                }
            }
            MotionEvent.ACTION_CANCEL -> {
                queueEvent { renderer.pointerCancel() }
                activePointerId = INVALID_POINTER_ID
            }
        }
        return true
    }

    private fun scheduleNextFrame() {
        if (!rendering || frameCallbackScheduled) return
        frameCallbackScheduled = true
        choreographer.postFrameCallback(frameCallback)
    }

    private companion object {
        const val ANIMATION_REFRESH_RATE_HZ = 60.0f
        const val INVALID_POINTER_ID = -1
    }
}

class SpiderRenderer : GLSurfaceView.Renderer {
    private val startedAtNanos = System.nanoTime()
    private var frameWindowStartedAtNanos = 0L
    private var frameWindowCount = 0
    @Volatile
    private var nativeHandle: Long = nativeCreate()

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        if (!nativeSurfaceCreated(nativeHandle)) {
            Log.e(TAG, "Failed to initialize the OpenGL ES function loader")
        }
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        // The native side uses the framebuffer resolution directly. Density is
        // passed for text/glyph atlas selection but the design surface itself
        // stretches to fit whatever pixel viewport the SurfaceView provides.
        if (!nativeResize(nativeHandle, width, height, 1.0f)) {
            Log.e(TAG, "Failed to (re)size the Spider renderer")
        }
    }

    override fun onDrawFrame(gl: GL10?) {
        val nowNanos = System.nanoTime()
        val elapsedSeconds = (nowNanos - startedAtNanos) / 1_000_000_000.0f
        nativeRender(nativeHandle, elapsedSeconds)

        if (frameWindowStartedAtNanos == 0L) {
            frameWindowStartedAtNanos = nowNanos
        }
        frameWindowCount++
        val windowNanos = nowNanos - frameWindowStartedAtNanos
        if (windowNanos >= FRAME_LOG_INTERVAL_NANOS) {
            val renderedFps = frameWindowCount * 1_000_000_000.0 / windowNanos
            Log.i(TAG, "Frame pacing: renderedFps=%.1f frames=%d".format(
                renderedFps, frameWindowCount))
            frameWindowStartedAtNanos = nowNanos
            frameWindowCount = 0
        }
    }

    fun ensureNativeRenderer() {
        if (nativeHandle == 0L) {
            nativeHandle = nativeCreate()
        }
    }

    fun destroyOnGlThread() {
        val handle = nativeHandle
        if (handle != 0L) {
            nativeDestroy(handle)
            nativeHandle = 0L
        }
    }

    fun pointerDown(x: Float, y: Float) {
        if (nativeHandle != 0L) nativePointerDown(nativeHandle, x, y)
    }

    fun pointerMove(x: Float, y: Float) {
        if (nativeHandle != 0L) nativePointerMove(nativeHandle, x, y)
    }

    fun pointerUp(x: Float, y: Float) {
        if (nativeHandle != 0L) nativePointerUp(nativeHandle, x, y)
    }

    fun pointerCancel() {
        if (nativeHandle != 0L) nativePointerCancel(nativeHandle)
    }

    private external fun nativeCreate(): Long
    private external fun nativeSurfaceCreated(handle: Long): Boolean
    private external fun nativeResize(handle: Long, width: Int, height: Int, density: Float): Boolean
    private external fun nativeRender(handle: Long, elapsedSeconds: Float)
    private external fun nativePointerDown(handle: Long, x: Float, y: Float)
    private external fun nativePointerMove(handle: Long, x: Float, y: Float)
    private external fun nativePointerUp(handle: Long, x: Float, y: Float)
    private external fun nativePointerCancel(handle: Long)
    private external fun nativeDestroy(handle: Long)

    private companion object {
        const val TAG = "SpiderSolitaire"
        const val FRAME_LOG_INTERVAL_NANOS = 5_000_000_000L

        init {
            System.loadLibrary("spider_android")
        }
    }
}
