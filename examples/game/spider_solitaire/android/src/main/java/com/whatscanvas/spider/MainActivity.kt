package com.whatscanvas.spider

import android.app.Activity
import android.os.Build
import android.os.Bundle
import android.view.WindowManager
import kotlin.math.abs

class MainActivity : Activity() {
    private var canvasView: SpiderSurfaceView? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Ask the compositor to schedule a 60 Hz cadence: adaptive-refresh OEM
        // builds occasionally park a mostly-static Activity at 30 Hz even when
        // the SurfaceView is producing continuous animation frames.
        val layoutParams: WindowManager.LayoutParams = window.attributes
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

        if (intent.getStringExtra("renderer") == "native") {
            setContentView(NativeCanvasBenchmarkView(this))
        } else {
            canvasView = SpiderSurfaceView(this)
            setContentView(canvasView)
        }
    }

    override fun onResume() {
        super.onResume()
        canvasView?.ensureNativeRenderer()
        canvasView?.onResume()
        canvasView?.startRendering()
    }

    override fun onPause() {
        canvasView?.stopRendering()
        if (isFinishing || isChangingConfigurations) {
            canvasView?.releaseNativeRenderer()
        }
        canvasView?.onPause()
        super.onPause()
    }

    private companion object {
        const val ANIMATION_REFRESH_RATE_HZ = 60.0f
    }
}
