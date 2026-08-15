package com.whatscanvas.demo

import android.app.Activity
import android.os.Bundle

class MainActivity : Activity() {
    private lateinit var canvasView: WhatsCanvasSurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        canvasView = WhatsCanvasSurfaceView(this)
        setContentView(canvasView)
    }

    override fun onResume() {
        super.onResume()
        canvasView.ensureNativeRenderer()
        canvasView.onResume()
    }

    override fun onPause() {
        canvasView.releaseNativeRenderer()
        canvasView.onPause()
        super.onPause()
    }
}
