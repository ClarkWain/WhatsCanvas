// Android JNI host for the WhatsCanvas Spider Solitaire example.
//
// The renderer owns a wsc::Canvas backed by the running EGL context and
// forwards touch events to the shared spider::SpiderGame implementation
// declared in SpiderGame.h (also used by the GLFW desktop build).

#include <wsc/FontSystem.h>
#include <wsc/Log.h>
#include <wsc/wsc.h>

#include <SpiderGame.h>

#include <jni.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

namespace {

constexpr const char* kLogTag = "SpiderSolitaire";

void logError(const char* message)
{
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", message);
}

int androidLogPriority(wsc::LogLevel level)
{
    switch (level) {
    case wsc::LogLevel::Trace:
    case wsc::LogLevel::Debug:
        return ANDROID_LOG_DEBUG;
    case wsc::LogLevel::Info:
        return ANDROID_LOG_INFO;
    case wsc::LogLevel::Warning:
        return ANDROID_LOG_WARN;
    case wsc::LogLevel::Error:
        return ANDROID_LOG_ERROR;
    case wsc::LogLevel::Off:
        return ANDROID_LOG_SILENT;
    }
    return ANDROID_LOG_DEFAULT;
}

void* loadOpenGlesProcedure(const char* name)
{
    const auto procedure = eglGetProcAddress(name);
    if (procedure != nullptr) {
        return reinterpret_cast<void*>(procedure);
    }
    return dlsym(RTLD_DEFAULT, name);
}

class SpiderRenderer {
public:
    SpiderRenderer() {
        game_.setTouchInputMode(true);
        // The tableau is retained as ten independently invalidated column
        // pictures, so rasterization stays cheap when a drag changes only its
        // source/destination while steady-state frames remain texture blits.
        game_.setRasterizePictures(true);
        game_.setUseCardAtlas(true);
    }

    bool surfaceCreated()
    {
        const EGLContext currentContext = eglGetCurrentContext();
        if (currentContext == EGL_NO_CONTEXT) {
            logError("surfaceCreated without a current EGL context");
            return false;
        }
        if (eglContext_ != EGL_NO_CONTEXT && eglContext_ != currentContext) {
            __android_log_print(ANDROID_LOG_WARN, kLogTag,
                                "EGL context changed; rebuilding derived resources");
            abandon();
        }
        eglContext_ = currentContext;
        const bool loaded = wsc::Canvas::loadOpenGL(&loadOpenGlesProcedure);
        if (!loaded) {
            logError("Canvas::loadOpenGL failed");
        }
        return loaded;
    }

    bool resize(int width, int height, float density)
    {
        if (width <= 0 || height <= 0) {
            return false;
        }
        const float safeDensity = std::isfinite(density) && density > 0.0f
            ? density : 1.0f;
        glViewport(0, 0, width, height);
        if (canvas_ && width == physicalWidth_ && height == physicalHeight_
            && std::abs(safeDensity - density_) < 0.001f) {
            return true;
        }
        const EGLContext currentContext = eglGetCurrentContext();
        release();
        eglContext_ = currentContext;
        physicalWidth_ = width;
        physicalHeight_ = height;
        density_ = safeDensity;

        canvas_ = wsc::Canvas::create(wsc::Canvas::Backend::OpenGLES, width, height);
        if (!canvas_) {
            logError("Canvas::create(OpenGLES) returned null");
            return false;
        }
        canvas_->setDevicePixelRatio(safeDensity);
        if (!canvas_->initializeContext()) {
            logError("Canvas::initializeContext failed");
            canvas_.reset();
            return false;
        }
        canvas_->setGpuTimingEnabled(true);
        // The default retained-picture raster cache budget is a small
        // fraction of GPU memory. The Spider tableau alone is a 1280x860
        // RGBA texture (~4.4 MB); add the five other retained pictures and
        // we can trip the cache LRU and force an unexpected re-rasterize.
        // A generous 32 MiB budget keeps every retained picture live so
        // frames only re-rasterize when the game explicitly invalidates
        // them.
        canvas_->setRetainedPictureRasterCacheBudgetBytes(
            static_cast<std::size_t>(32) * 1024 * 1024);
        for (const wsc::FontFace& face : wsc::FontSystem::defaultSystemFontFaces()) {
            canvas_->registerFontFace(face);
        }
        canvas_->setFontFallbackChain(wsc::FontSystem::defaultFallbackChain());

        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "Spider renderer ready: %dx%d, dpr=%.2f",
                            width, height, safeDensity);
        return true;
    }

    // The Kotlin side uses the framebuffer size as its logical viewport so the
    // pixel-perfect 1280x860 design surface is aspect-fitted end-to-end.
    void render(float elapsedSeconds)
    {
        if (!canvas_) {
            return;
        }
        const float dt = lastElapsedSeconds_ < 0.0f
            ? 0.0f
            : std::clamp(elapsedSeconds - lastElapsedSeconds_, 0.0f, 0.1f);
        lastElapsedSeconds_ = elapsedSeconds;

        const auto renderStart = std::chrono::steady_clock::now();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.02f, 0.09f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        canvas_->beginFrame();
        game_.update(dt);
        game_.render(*canvas_, physicalWidth_, physicalHeight_);
        drawFpsOverlay();
        canvas_->endFrame();
        const auto renderEnd = std::chrono::steady_clock::now();

        // Log per-frame timing whenever the frame is a candidate stutter
        // (>20 ms of CPU-side work). Together with the wall-clock spacing
        // logged below this is enough to tell whether the drag hitch is a
        // record-picture spike, a text/glyph atlas grow, or a swap-block
        // waiting for the display.
        const std::uint64_t frameUs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                renderEnd - renderStart).count());
        if (gestureActive_) {
            ++gestureFrames_;
            gestureTotalFrameUs_ += frameUs;
            gestureMaxFrameUs_ = std::max(gestureMaxFrameUs_, frameUs);
            if (frameUs > 20000) ++gestureSlowFrames_;
        }
        if (frameUs > 20000) {
            __android_log_print(
                ANDROID_LOG_WARN, kLogTag,
                "SLOW frame: cpuUs=%llu",
                static_cast<unsigned long long>(frameUs));
        }

        if (!firstFrameLogged_) {
            firstFrameLogged_ = true;
            __android_log_print(ANDROID_LOG_INFO, kLogTag,
                                "First Spider frame ready: %dx%d",
                                physicalWidth_, physicalHeight_);
        }

        if (finishGestureAfterFrame_) {
            finishGestureAfterFrame_ = false;
            finishGestureSample();
        }
    }

    // Compute a rolling FPS estimate from the wall-clock spacing of the
    // present calls and draw it on top of the game so the user can verify the
    // measured framerate directly on the device instead of via logcat.
    void drawFpsOverlay()
    {
        const auto now = std::chrono::steady_clock::now();
        if (!fpsHasSample_) {
            fpsHasSample_ = true;
            fpsLastFrameTime_ = now;
            fpsWindowStart_ = now;
            fpsWindowFrames_ = 0;
            return;
        }
        ++fpsWindowFrames_;
        const auto windowElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - fpsWindowStart_).count();
        if (windowElapsed >= 500) {
            fpsCurrent_ = static_cast<float>(fpsWindowFrames_ * 1000.0 / windowElapsed);
            fpsWindowStart_ = now;
            fpsWindowFrames_ = 0;
        }
        fpsLastFrameTime_ = now;
        if (fpsCurrent_ <= 0.0f) return;

        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.0f FPS",
                      static_cast<double>(fpsCurrent_));

        // Draw at physical (buffer) coordinates so the overlay sits in the
        // top-right corner of the actual GL surface, independent of the
        // 1280x860 letterbox transform SpiderGame applies internally.
        const float margin = std::max(8.0f, physicalHeight_ * 0.02f);
        const float boxW = std::max(120.0f, physicalHeight_ * 0.13f);
        const float boxH = std::max(48.0f, physicalHeight_ * 0.055f);
        const float boxX = physicalWidth_ - boxW - margin;
        const float boxY = margin;
        wsc::Paint bg;
        bg.setStyle(wsc::Paint::Style::FILL);
        bg.setColor(wsc::Color(4, 12, 8, 210));
        canvas_->drawRoundRect(wsc::RectF(boxX, boxY, boxW, boxH), boxH * 0.35f, bg);
        wsc::Paint border;
        border.setStyle(wsc::Paint::Style::STROKE);
        border.setStrokeWidth(2.0f);
        const bool at60 = fpsCurrent_ >= 58.0f;
        const bool at30 = fpsCurrent_ >= 28.0f;
        const wsc::Color accent = at60 ? wsc::Color(120, 231, 190)
                                       : at30 ? wsc::Color(240, 210, 110)
                                              : wsc::Color(232, 108, 96);
        border.setColor(accent);
        canvas_->drawRoundRect(wsc::RectF(boxX, boxY, boxW, boxH), boxH * 0.35f, border);
        wsc::Paint text;
        text.setStyle(wsc::Paint::Style::FILL);
        text.setColor(accent);
        text.setTextSize(boxH * 0.5f);
        text.setTextAlign(wsc::Paint::TextAlign::CENTER);
        text.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
        text.setFont(wsc::FontSystem::kDefaultPrimaryFamily);
        text.setFontWeight(800);
        canvas_->drawText(buffer, boxX + boxW * 0.5f, boxY + boxH * 0.5f, text);
    }

    void pointerDown(float physicalX, float physicalY)
    {
        if (!canvas_) return;
        beginGestureSample();
        auto p = game_.toDesign(physicalX, physicalY);
        game_.pointerDown(p.first, p.second);
    }

    void pointerMove(float physicalX, float physicalY)
    {
        if (!canvas_) return;
        if (gestureActive_) ++gestureMoves_;
        auto p = game_.toDesign(physicalX, physicalY);
        game_.pointerMove(p.first, p.second);
    }

    void pointerUp(float physicalX, float physicalY)
    {
        if (!canvas_) return;
        auto p = game_.toDesign(physicalX, physicalY);
        game_.pointerUp(p.first, p.second);
        finishGestureAfterFrame_ = gestureActive_;
    }

    void pointerCancel()
    {
        if (!canvas_) return;
        game_.cancelSelection();
        finishGestureAfterFrame_ = gestureActive_;
    }

    void release()
    {
        game_.releaseGpuResources();
        if (canvas_) {
            canvas_->finalizeContext();
        }
        canvas_.reset();
        eglContext_ = EGL_NO_CONTEXT;
        physicalWidth_ = 0;
        physicalHeight_ = 0;
        density_ = 1.0f;
        firstFrameLogged_ = false;
        lastElapsedSeconds_ = -1.0f;
    }

    void abandon()
    {
        game_.releaseGpuResources();
        if (canvas_) {
            canvas_->abandonContext();
        }
        canvas_.reset();
        eglContext_ = EGL_NO_CONTEXT;
        physicalWidth_ = 0;
        physicalHeight_ = 0;
        density_ = 1.0f;
        firstFrameLogged_ = false;
        lastElapsedSeconds_ = -1.0f;
    }

private:
    void beginGestureSample()
    {
        gestureActive_ = true;
        finishGestureAfterFrame_ = false;
        gestureStart_ = std::chrono::steady_clock::now();
        gestureFrames_ = 0;
        gestureMoves_ = 0;
        gestureSlowFrames_ = 0;
        gestureTotalFrameUs_ = 0;
        gestureMaxFrameUs_ = 0;
    }

    void finishGestureSample()
    {
        if (!gestureActive_) return;
        const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - gestureStart_).count();
        const std::uint64_t averageUs = gestureFrames_ == 0
            ? 0 : gestureTotalFrameUs_ / gestureFrames_;
        __android_log_print(
            ANDROID_LOG_INFO, kLogTag,
            "DRAG_PERF durationMs=%lld frames=%u moves=%u avgCpuUs=%llu maxCpuUs=%llu slowFrames=%u",
            static_cast<long long>(durationMs), gestureFrames_, gestureMoves_,
            static_cast<unsigned long long>(averageUs),
            static_cast<unsigned long long>(gestureMaxFrameUs_), gestureSlowFrames_);
        gestureActive_ = false;
    }

    std::unique_ptr<wsc::Canvas> canvas_;
    spider::SpiderGame game_{1, 0};
    EGLContext eglContext_ = EGL_NO_CONTEXT;
    int physicalWidth_ = 0;
    int physicalHeight_ = 0;
    float density_ = 1.0f;
    bool firstFrameLogged_ = false;
    float lastElapsedSeconds_ = -1.0f;
    bool fpsHasSample_ = false;
    std::chrono::steady_clock::time_point fpsLastFrameTime_{};
    std::chrono::steady_clock::time_point fpsWindowStart_{};
    int fpsWindowFrames_ = 0;
    float fpsCurrent_ = 0.0f;
    bool gestureActive_ = false;
    bool finishGestureAfterFrame_ = false;
    std::chrono::steady_clock::time_point gestureStart_{};
    unsigned int gestureFrames_ = 0;
    unsigned int gestureMoves_ = 0;
    unsigned int gestureSlowFrames_ = 0;
    std::uint64_t gestureTotalFrameUs_ = 0;
    std::uint64_t gestureMaxFrameUs_ = 0;
};

SpiderRenderer* rendererFromHandle(jlong handle)
{
    return reinterpret_cast<SpiderRenderer*>(
        static_cast<std::uintptr_t>(handle));
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_whatscanvas_spider_SpiderRenderer_nativeCreate(
    JNIEnv*, jobject)
{
    wsc::Log::setLevel(wsc::LogLevel::Info);
    wsc::Log::setHandler([](const wsc::LogMessage& message) {
        __android_log_print(
            androidLogPriority(message.level), kLogTag,
            "%s: %s", message.category, message.message.c_str());
    });
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(new SpiderRenderer()));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_whatscanvas_spider_SpiderRenderer_nativeSurfaceCreated(
    JNIEnv*, jobject, jlong handle)
{
    SpiderRenderer* r = rendererFromHandle(handle);
    return r && r->surfaceCreated() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_whatscanvas_spider_SpiderRenderer_nativeResize(
    JNIEnv*, jobject, jlong handle, jint width, jint height, jfloat density)
{
    SpiderRenderer* r = rendererFromHandle(handle);
    return r && r->resize(width, height, density) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_whatscanvas_spider_SpiderRenderer_nativeRender(
    JNIEnv*, jobject, jlong handle, jfloat elapsedSeconds)
{
    SpiderRenderer* r = rendererFromHandle(handle);
    if (r) r->render(elapsedSeconds);
}

extern "C" JNIEXPORT void JNICALL
Java_com_whatscanvas_spider_SpiderRenderer_nativePointerDown(
    JNIEnv*, jobject, jlong handle, jfloat x, jfloat y)
{
    SpiderRenderer* r = rendererFromHandle(handle);
    if (r) r->pointerDown(x, y);
}

extern "C" JNIEXPORT void JNICALL
Java_com_whatscanvas_spider_SpiderRenderer_nativePointerMove(
    JNIEnv*, jobject, jlong handle, jfloat x, jfloat y)
{
    SpiderRenderer* r = rendererFromHandle(handle);
    if (r) r->pointerMove(x, y);
}

extern "C" JNIEXPORT void JNICALL
Java_com_whatscanvas_spider_SpiderRenderer_nativePointerUp(
    JNIEnv*, jobject, jlong handle, jfloat x, jfloat y)
{
    SpiderRenderer* r = rendererFromHandle(handle);
    if (r) r->pointerUp(x, y);
}

extern "C" JNIEXPORT void JNICALL
Java_com_whatscanvas_spider_SpiderRenderer_nativePointerCancel(
    JNIEnv*, jobject, jlong handle)
{
    SpiderRenderer* r = rendererFromHandle(handle);
    if (r) r->pointerCancel();
}

extern "C" JNIEXPORT void JNICALL
Java_com_whatscanvas_spider_SpiderRenderer_nativeDestroy(
    JNIEnv*, jobject, jlong handle)
{
    SpiderRenderer* r = rendererFromHandle(handle);
    if (r) {
        r->release();
        delete r;
    }
}
