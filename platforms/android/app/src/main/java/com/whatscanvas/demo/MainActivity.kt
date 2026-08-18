package com.whatscanvas.demo

import android.app.Activity
import android.os.Bundle
import android.os.Build
import kotlin.math.abs

class MainActivity : Activity() {
    private lateinit var canvasView: WhatsCanvasSurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // This demo contains continuous motion. Tell SurfaceFlinger that it
        // benefits from a 60 Hz mode; otherwise adaptive-refresh OEM builds
        // can classify the mostly static Activity as 30 Hz even while its
        // SurfaceView is producing animation frames.
        val layoutParams = window.attributes
        layoutParams.preferredRefreshRate = ANIMATION_REFRESH_RATE_HZ
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            @Suppress("DEPRECATION")
            val display = windowManager.defaultDisplay
            val sixtyHertzMode = display.supportedModes.minByOrNull {
                abs(it.refreshRate - ANIMATION_REFRESH_RATE_HZ)
            }
            if (sixtyHertzMode != null) {
                layoutParams.preferredDisplayModeId = sixtyHertzMode.modeId
            }
        }
        window.attributes = layoutParams
        canvasView = WhatsCanvasSurfaceView(this)
        setContentView(canvasView)
    }

    override fun onResume() {
        super.onResume()
        canvasView.ensureNativeRenderer()
        canvasView.onResume()
        canvasView.startRendering()
    }

    override fun onPause() {
        canvasView.stopRendering()
        // Keep a healthy EGL context and the retained raster layer across an
        // ordinary background/foreground transition. When this Activity is
        // actually going away, release while the GL thread/context is still
        // alive, before GLSurfaceView.onPause() stops it.
        if (isFinishing || isChangingConfigurations) {
            canvasView.releaseNativeRenderer()
        }
        canvasView.onPause()
        super.onPause()
    }

    private companion object {
        const val ANIMATION_REFRESH_RATE_HZ = 60.0f
    }
}
