package com.whatscanvas.demo

import android.content.Context
import android.opengl.GLSurfaceView
import android.util.Log
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class WhatsCanvasSurfaceView(context: Context) : GLSurfaceView(context) {
    private val canvasRenderer = WhatsCanvasRenderer {
        resources.displayMetrics.density
    }

    init {
        setEGLContextClientVersion(3)
        setEGLConfigChooser(8, 8, 8, 8, 24, 8)
        setRenderer(canvasRenderer)
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    fun releaseNativeRenderer() {
        queueEvent { canvasRenderer.destroyOnGlThread() }
    }

    fun ensureNativeRenderer() {
        canvasRenderer.ensureNativeRenderer()
    }
}

class WhatsCanvasRenderer(
    private val densityProvider: () -> Float
) : GLSurfaceView.Renderer {
    private val startedAtNanos = System.nanoTime()
    @Volatile
    private var nativeHandle: Long = nativeCreate()

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        if (!nativeSurfaceCreated(nativeHandle)) {
            Log.e(TAG, "Unable to initialize the OpenGL ES function loader")
        }
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        if (!nativeResize(nativeHandle, width, height, densityProvider())) {
            Log.e(TAG, "Unable to create the WhatsCanvas OpenGLES renderer")
        }
    }

    override fun onDrawFrame(gl: GL10?) {
        val elapsedSeconds = (System.nanoTime() - startedAtNanos) / 1_000_000_000.0f
        nativeRender(nativeHandle, elapsedSeconds)
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

    private external fun nativeCreate(): Long
    private external fun nativeSurfaceCreated(handle: Long): Boolean
    private external fun nativeResize(handle: Long, width: Int, height: Int, density: Float): Boolean
    private external fun nativeRender(handle: Long, elapsedSeconds: Float)
    private external fun nativeDestroy(handle: Long)

    private companion object {
        const val TAG = "WhatsCanvas"

        init {
            System.loadLibrary("whatscanvas_android")
        }
    }
}
