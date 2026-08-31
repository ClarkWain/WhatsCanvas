package com.whatscanvas.spider

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.os.SystemClock
import android.util.Log
import android.view.MotionEvent
import android.view.View
import kotlin.math.max
import kotlin.math.min

/**
 * Android Canvas control renderer for the Spider drag workload.
 *
 * It intentionally redraws all 54 dealt cards on every invalidation. All
 * common pixels live in only two shared Bitmaps (face and back); rank and suit
 * are lightweight overlays. A production implementation could additionally
 * flatten the static tableau to one Bitmap, so this is a conservative control.
 */
class NativeCanvasBenchmarkView(context: Context) : View(context) {
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        textAlign = Paint.Align.LEFT
    }
    private lateinit var faceBitmap: Bitmap
    private lateinit var backBitmap: Bitmap
    private val faceRanks = arrayOf("7", "6", "K", "A", "5", "4", "5", "8", "4", "A")

    private var renderScale = 1f
    private var renderOffsetX = 0f
    private var renderOffsetY = 0f
    private var selectedColumn = -1
    private var dragging = false
    private var pointerX = 0f
    private var pointerY = 0f
    private var pressX = 0f
    private var pressY = 0f
    private var dragOffsetX = 0f
    private var dragOffsetY = 0f

    private var gestureActive = false
    private var finishAfterDraw = false
    private var gestureStartedMs = 0L
    private var gestureFrames = 0
    private var gestureMoves = 0
    private var totalDrawUs = 0L
    private var maxDrawUs = 0L
    private var maxIntervalUs = 0L
    private var lastDrawStartedUs = 0L

    init {
        isFocusable = true
        isFocusableInTouchMode = true
        buildSharedCardImages()
    }

    override fun onDraw(canvas: Canvas) {
        val drawStartedUs = SystemClock.elapsedRealtimeNanos() / 1_000L
        super.onDraw(canvas)
        renderScale = min(width / DESIGN_W, height / DESIGN_H)
        renderOffsetX = (width - DESIGN_W * renderScale) * 0.5f
        renderOffsetY = (height - DESIGN_H * renderScale) * 0.5f

        canvas.drawColor(Color.rgb(2, 30, 24))
        canvas.save()
        canvas.translate(renderOffsetX, renderOffsetY)
        canvas.scale(renderScale, renderScale)
        drawTable(canvas)
        canvas.restore()

        val drawUs = SystemClock.elapsedRealtimeNanos() / 1_000L - drawStartedUs
        if (gestureActive) {
            gestureFrames++
            totalDrawUs += drawUs
            maxDrawUs = max(maxDrawUs, drawUs)
            if (lastDrawStartedUs != 0L) {
                maxIntervalUs = max(maxIntervalUs, drawStartedUs - lastDrawStartedUs)
            }
            lastDrawStartedUs = drawStartedUs
        }
        if (finishAfterDraw) {
            finishGesture()
        } else if (gestureActive) {
            // Match GLSurfaceView's VSYNC-paced continuous rendering during a
            // drag instead of depending on the OEM input batching frequency.
            postInvalidateOnAnimation()
        }
    }

    private fun drawTable(canvas: Canvas) {
        paint.style = Paint.Style.FILL
        paint.shader = LinearGradient(0f, 0f, DESIGN_W, DESIGN_H,
            Color.rgb(18, 105, 76), Color.rgb(3, 47, 39), Shader.TileMode.CLAMP)
        canvas.drawRect(0f, 0f, DESIGN_W, DESIGN_H, paint)
        paint.shader = null

        paint.color = Color.argb(220, 4, 31, 27)
        canvas.drawRect(0f, 0f, DESIGN_W, 160f, paint)
        textPaint.color = Color.rgb(238, 222, 181)
        textPaint.textSize = 28f
        canvas.drawText("SPIDER  •  NATIVE CANVAS CONTROL", 34f, 58f, textPaint)

        for (column in 0 until 10) {
            val count = if (column < 4) 6 else 5
            val x = columnX(column)
            for (index in 0 until count - 1) {
                drawSharedBitmap(canvas, backBitmap, x, TABLE_Y + index * CARD_STEP)
            }
            val faceY = faceY(column)
            if (!(dragging && column == selectedColumn)) {
                drawFace(canvas, x, faceY, faceRanks[column])
            }
        }

        if (selectedColumn >= 0) {
            val x = if (dragging) pointerX - dragOffsetX else columnX(selectedColumn)
            val y = if (dragging) pointerY - dragOffsetY else faceY(selectedColumn)
            drawFace(canvas, x, y, faceRanks[selectedColumn])
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = 2.2f
            paint.color = Color.rgb(232, 188, 103)
            canvas.drawRoundRect(RectF(x, y, x + CARD_W, y + CARD_H), 10f, 10f, paint)
        }
    }

    private fun drawSharedBitmap(canvas: Canvas, bitmap: Bitmap, x: Float, y: Float) {
        canvas.drawBitmap(bitmap, null, RectF(x, y, x + CARD_W, y + CARD_H), paint)
    }

    private fun drawFace(canvas: Canvas, x: Float, y: Float, rank: String) {
        drawSharedBitmap(canvas, faceBitmap, x, y)
        textPaint.color = Color.rgb(28, 31, 43)
        textPaint.textSize = if (rank == "10") 22f else 26f
        canvas.drawText(rank, x + 10f, y + 31f, textPaint)
        textPaint.textSize = 39f
        canvas.drawText("♠", x + 31f, y + 91f, textPaint)
    }

    private fun buildSharedCardImages() {
        faceBitmap = Bitmap.createBitmap(BITMAP_W, BITMAP_H, Bitmap.Config.ARGB_8888)
        Canvas(faceBitmap).run {
            scale(BITMAP_SCALE, BITMAP_SCALE)
            val p = Paint(Paint.ANTI_ALIAS_FLAG)
            p.color = Color.rgb(246, 241, 231)
            drawRoundRect(RectF(0f, 0f, CARD_W, CARD_H), 10f, 10f, p)
            p.style = Paint.Style.STROKE
            p.strokeWidth = 1.2f
            p.color = Color.rgb(190, 171, 139)
            drawRoundRect(RectF(0.6f, 0.6f, CARD_W - 0.6f, CARD_H - 0.6f), 10f, 10f, p)
        }

        backBitmap = Bitmap.createBitmap(BITMAP_W, BITMAP_H, Bitmap.Config.ARGB_8888)
        Canvas(backBitmap).run {
            scale(BITMAP_SCALE, BITMAP_SCALE)
            val p = Paint(Paint.ANTI_ALIAS_FLAG)
            p.color = Color.rgb(64, 29, 55)
            drawRoundRect(RectF(0f, 0f, CARD_W, CARD_H), 10f, 10f, p)
            p.style = Paint.Style.STROKE
            p.strokeWidth = 2f
            p.color = Color.rgb(210, 179, 119)
            drawRoundRect(RectF(3f, 3f, CARD_W - 3f, CARD_H - 3f), 8f, 8f, p)
            p.strokeWidth = 1f
            p.color = Color.argb(110, 210, 179, 119)
            for (offset in -CARD_H.toInt() until CARD_W.toInt() step 15) {
                drawLine(offset.toFloat(), 0f, offset + CARD_H, CARD_H, p)
            }
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val x = (event.x - renderOffsetX) / renderScale.coerceAtLeast(0.001f)
        val y = (event.y - renderOffsetY) / renderScale.coerceAtLeast(0.001f)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                selectedColumn = hitFace(x, y)
                if (selectedColumn < 0) return true
                pointerX = x
                pointerY = y
                pressX = x
                pressY = y
                dragOffsetX = x - columnX(selectedColumn)
                dragOffsetY = y - faceY(selectedColumn)
                dragging = false
                beginGesture()
                postInvalidateOnAnimation()
            }
            MotionEvent.ACTION_MOVE -> if (selectedColumn >= 0) {
                pointerX = x
                pointerY = y
                gestureMoves++
                val dx = x - pressX
                val dy = y - pressY
                if (!dragging && dx * dx + dy * dy > 220f) dragging = true
                postInvalidateOnAnimation()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> if (selectedColumn >= 0) {
                pointerX = x
                pointerY = y
                finishAfterDraw = true
                postInvalidateOnAnimation()
            }
        }
        return true
    }

    private fun hitFace(x: Float, y: Float): Int {
        for (column in 0 until 10) {
            val cardX = columnX(column)
            val cardY = faceY(column)
            if (x >= cardX && x <= cardX + CARD_W && y >= cardY && y <= cardY + CARD_H) {
                return column
            }
        }
        return -1
    }

    private fun beginGesture() {
        gestureActive = true
        finishAfterDraw = false
        gestureStartedMs = SystemClock.elapsedRealtime()
        gestureFrames = 0
        gestureMoves = 0
        totalDrawUs = 0
        maxDrawUs = 0
        maxIntervalUs = 0
        lastDrawStartedUs = 0
    }

    private fun finishGesture() {
        finishAfterDraw = false
        val durationMs = SystemClock.elapsedRealtime() - gestureStartedMs
        val averageDrawUs = if (gestureFrames == 0) 0 else totalDrawUs / gestureFrames
        Log.i(TAG, "DRAG_PERF_NATIVE durationMs=$durationMs frames=$gestureFrames " +
            "moves=$gestureMoves avgDrawUs=$averageDrawUs maxDrawUs=$maxDrawUs " +
            "maxIntervalUs=$maxIntervalUs sharedBitmaps=2")
        gestureActive = false
        selectedColumn = -1
        dragging = false
    }

    private fun columnX(column: Int) = COL_X + column * COL_GAP
    private fun faceY(column: Int) = TABLE_Y + (if (column < 4) 5 else 4) * CARD_STEP

    private companion object {
        const val TAG = "SpiderNativeCanvas"
        const val DESIGN_W = 1280f
        const val DESIGN_H = 860f
        const val CARD_W = 98f
        const val CARD_H = 136f
        const val COL_X = 34f
        const val COL_GAP = 124f
        const val TABLE_Y = 176f
        const val CARD_STEP = 18f
        const val BITMAP_SCALE = 2f
        const val BITMAP_W = 196
        const val BITMAP_H = 272
    }
}
