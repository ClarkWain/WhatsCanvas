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
        val requestedCaptureTime = intent
            ?.takeIf { it.hasExtra(CAPTURE_TIME_EXTRA) }
            ?.getFloatExtra(CAPTURE_TIME_EXTRA, 0.0f)
            ?.takeIf { it.isFinite() && it >= 0.0f }
        val requestedScene = intent
            ?.getStringExtra(CAPTURE_SCENE_EXTRA)
            ?.takeIf { it in VALID_SCENES }
            ?: DEFAULT_SCENE
        canvasView = WhatsCanvasSurfaceView(
            this, requestedCaptureTime, requestedScene)
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
        const val CAPTURE_TIME_EXTRA = "capture_time_seconds"
        const val CAPTURE_SCENE_EXTRA = "capture_scene_id"
        const val DEFAULT_SCENE = "feature_showcase"
        val VALID_SCENES = setOf(
            DEFAULT_SCENE,
            "feature_showcase_picture",
            "text_stress",
            "geometry_stress",
            "compositing_stress",
            "nanovg_feature_showcase",
            "nanovg_text_stress",
            "nanovg_geometry_stress",
            "nanovg_compositing_stress"
        )
    }
}
