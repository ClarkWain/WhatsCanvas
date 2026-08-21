#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
#include <GLES3/gl3.h>
#include <wsc/wsc.h>

#include "scenes/FeatureShowcaseScene.h"
#include "scenes/StressScene.h"

namespace {

constexpr const char* kCanvasSelector = "#whatscanvas-canvas";
constexpr const char* kLatinFontPath = "/fonts/Roboto-Regular.ttf";
constexpr const char* kCjkFontPath = "/fonts/Mplus1p-Regular.ttf";
constexpr const char* kEmojiFontPath = "/fonts/NotoColorEmoji.demo.subset.ttf";

EM_JS(double, queryFixedTimeSeconds, (), {
    const raw = new URLSearchParams(window.location.search).get('time');
    if (raw === null) return -1.0;
    const value = Number(raw);
    return Number.isFinite(value) && value >= 0 ? value : -1.0;
});

EM_JS(double, queryDevicePixelRatio, (), {
    const raw = new URLSearchParams(window.location.search).get('dpr');
    const override = raw === null ? NaN : Number(raw);
    const value = Number.isFinite(override) ? override : window.devicePixelRatio;
    return Math.max(1.0, Math.min(4.0, value || 1.0));
});

EM_JS(int, querySceneIndex, (), {
    const scene = new URLSearchParams(window.location.search).get('scene');
    if (scene === 'text_stress') return 1;
    if (scene === 'geometry_stress') return 2;
    if (scene === 'compositing_stress') return 3;
    return 0;
});

EM_JS(void, publishState,
      (const char* state, int physicalWidth, int physicalHeight,
       double logicalWidth, double logicalHeight, double dpr,
       const char* detail), {
    const stateText = UTF8ToString(state);
    const detailText = UTF8ToString(detail);
    const snapshot = window.whatsCanvasDemo || {};
    Object.assign(snapshot, {
        state: stateText,
        ready: stateText === 'ready',
        physicalWidth,
        physicalHeight,
        logicalWidth,
        logicalHeight,
        dpr,
        detail: detailText
    });
    window.whatsCanvasDemo = snapshot;
    document.documentElement.dataset.whatscanvasState = stateText;
    const status = document.getElementById('status');
    if (status) {
        status.textContent = detailText;
        status.hidden = stateText === 'ready';
    }
});

EM_JS(void, publishFrameRate, (double fps, int frameIndex), {
    const snapshot = window.whatsCanvasDemo || {};
    snapshot.fps = fps;
    snapshot.frameIndex = frameIndex;
    window.whatsCanvasDemo = snapshot;
});

EM_JS(void, installContextTestHook, (), {
    const snapshot = window.whatsCanvasDemo || {};
    snapshot.loseAndRestoreContext = function(delayMs) {
        const gl = Module.canvas.getContext('webgl2');
        const extension = gl && gl.getExtension('WEBGL_lose_context');
        if (!extension) return false;
        extension.loseContext();
        window.setTimeout(function() { extension.restoreContext(); },
                          Math.max(0, delayMs || 100));
        return true;
    };
    snapshot.setTestVisibility = function(hidden) {
        try {
            if (hidden) {
                Object.defineProperty(document, 'hidden', {
                    configurable: true, value: true
                });
                Object.defineProperty(document, 'visibilityState', {
                    configurable: true, value: 'hidden'
                });
            } else {
                delete document.hidden;
                delete document.visibilityState;
            }
            document.dispatchEvent(new Event('visibilitychange'));
            return true;
        } catch (_) {
            return false;
        }
    };
    window.whatsCanvasDemo = snapshot;
});

void* loadWebGlProcedure(const char* name)
{
    return emscripten_webgl_get_proc_address(name);
}

bool registerWebFonts(wsc::Canvas& canvas)
{
    const bool latin = canvas.registerFontFace(wsc::FontFace::fromFile(
        wsc::FontDescriptor(wsc::FontSystem::kDefaultPrimaryFamily, 400),
        kLatinFontPath));
    const bool cjk = canvas.registerFontFace(wsc::FontFace::fromFile(
        wsc::FontDescriptor(wsc::FontSystem::kDefaultCjkFamily, 400),
        kCjkFontPath));
    const bool emoji = canvas.registerFontFace(wsc::FontFace::fromFile(
        wsc::FontDescriptor(wsc::FontSystem::kDefaultSymbolFamily, 400),
        kEmojiFontPath));
    wsc::FontFallbackChain primaryChain(
        wsc::FontSystem::kDefaultPrimaryFamily);
    primaryChain.addFallbackFamily(wsc::FontSystem::kDefaultCjkFamily);
    primaryChain.addFallbackFamily(wsc::FontSystem::kDefaultSymbolFamily);

    // The canonical complex-text line explicitly selects the CJK family.
    // Give that family its own symbol fallback so one missing emoji cluster
    // cannot force the complete mixed-script line onto the placeholder path.
    wsc::FontFallbackChain cjkChain(wsc::FontSystem::kDefaultCjkFamily);
    cjkChain.addFallbackFamily(wsc::FontSystem::kDefaultSymbolFamily);

    const bool primaryFallback = canvas.setFontFallbackChain(primaryChain);
    const bool cjkFallback = canvas.setFontFallbackChain(cjkChain);
    return latin && cjk && emoji && primaryFallback && cjkFallback;
}

class WebApp
{
public:
    WebApp()
        : scene_(makeScene()),
          fixedTimeSeconds_(queryFixedTimeSeconds())
    {
    }

    bool start()
    {
        EmscriptenWebGLContextAttributes attributes;
        emscripten_webgl_init_context_attributes(&attributes);
        attributes.alpha = EM_FALSE;
        attributes.depth = EM_FALSE;
        attributes.stencil = EM_TRUE;
        attributes.antialias = EM_TRUE;
        attributes.premultipliedAlpha = EM_TRUE;
        attributes.preserveDrawingBuffer = EM_TRUE;
        attributes.enableExtensionsByDefault = EM_TRUE;
        attributes.majorVersion = 2;
        attributes.minorVersion = 0;

        context_ = emscripten_webgl_create_context(kCanvasSelector, &attributes);
        if (context_ <= 0
            || emscripten_webgl_make_context_current(context_) != EMSCRIPTEN_RESULT_SUCCESS) {
            fail("WebGL 2 context creation failed");
            return false;
        }
        if (!wsc::Canvas::loadOpenGL(&loadWebGlProcedure)) {
            fail("WhatsCanvas could not load WebGL 2 entry points");
            return false;
        }

        if (!resizeSurface(true) || !createCanvas()) {
            return false;
        }

        emscripten_set_resize_callback(
            EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_FALSE, &WebApp::onResize);
        emscripten_set_orientationchange_callback(
            this, EM_FALSE, &WebApp::onOrientationChange);
        emscripten_set_visibilitychange_callback(
            this, EM_FALSE, &WebApp::onVisibilityChange);
        emscripten_set_webglcontextlost_callback(
            kCanvasSelector, this, EM_FALSE, &WebApp::onContextLost);
        emscripten_set_webglcontextrestored_callback(
            kCanvasSelector, this, EM_FALSE, &WebApp::onContextRestored);

        installContextTestHook();
        lastFrameMilliseconds_ = emscripten_get_now();
        publishReady();
        emscripten_set_main_loop_arg(&WebApp::mainLoop, this, 0, EM_TRUE);
        return true;
    }

private:
    static whatscanvas::desktop::ScenePtr makeScene()
    {
        switch (querySceneIndex()) {
        case 1:
            return std::make_unique<whatscanvas::desktop::StressScene>(
                whatscanvas::scenes::StressSceneId::Text);
        case 2:
            return std::make_unique<whatscanvas::desktop::StressScene>(
                whatscanvas::scenes::StressSceneId::Geometry);
        case 3:
            return std::make_unique<whatscanvas::desktop::StressScene>(
                whatscanvas::scenes::StressSceneId::Compositing);
        default:
            return std::make_unique<whatscanvas::desktop::FeatureShowcaseScene>(
                whatscanvas::desktop::FeatureShowcaseBranding{
                    "WhatsCanvas Web",
                    "WebGL 2  |  WebAssembly  |  live feature matrix",
                    "8 feature cards  |  real WebGL 2 output"});
        }
    }

    bool createCanvas()
    {
        canvas_ = wsc::Canvas::create(
            wsc::Canvas::Backend::OpenGLES, physicalWidth_, physicalHeight_);
        if (!canvas_) {
            fail("Canvas::create(OpenGLES) failed");
            return false;
        }
        canvas_->setDevicePixelRatio(devicePixelRatio_);
        if (!canvas_->initializeContext()) {
            fail("Canvas::initializeContext failed");
            canvas_.reset();
            return false;
        }
        if (!registerWebFonts(*canvas_)) {
            fail("Portable Web fonts could not be registered");
            canvas_->finalizeContext();
            canvas_.reset();
            return false;
        }
        scene_->onCanvasReady(*canvas_);
        scene_->onLayout(*canvas_, logicalWidth_, logicalHeight_);
        return true;
    }

    bool resizeSurface(bool force)
    {
        double cssWidth = 0.0;
        double cssHeight = 0.0;
        if (emscripten_get_element_css_size(
                kCanvasSelector, &cssWidth, &cssHeight) != EMSCRIPTEN_RESULT_SUCCESS
            || !std::isfinite(cssWidth) || !std::isfinite(cssHeight)
            || cssWidth <= 0.0 || cssHeight <= 0.0) {
            fail("Canvas CSS size is invalid");
            return false;
        }

        const float dpr = static_cast<float>(queryDevicePixelRatio());
        const int width = std::max(1, static_cast<int>(std::lround(cssWidth * dpr)));
        const int height = std::max(1, static_cast<int>(std::lround(cssHeight * dpr)));
        const bool changed = force || width != physicalWidth_
            || height != physicalHeight_ || std::abs(dpr - devicePixelRatio_) > 0.001f;
        if (!changed) {
            return true;
        }

        if (emscripten_set_canvas_element_size(
                kCanvasSelector, width, height) != EMSCRIPTEN_RESULT_SUCCESS) {
            fail("Canvas drawing-buffer resize failed");
            return false;
        }
        physicalWidth_ = width;
        physicalHeight_ = height;
        logicalWidth_ = static_cast<float>(cssWidth);
        logicalHeight_ = static_cast<float>(cssHeight);
        devicePixelRatio_ = dpr;
        glViewport(0, 0, width, height);

        if (canvas_) {
            canvas_->setSize(width, height);
            canvas_->setDevicePixelRatio(dpr);
            scene_->onLayout(*canvas_, logicalWidth_, logicalHeight_);
        }
        return true;
    }

    void drawFrame()
    {
        if (hidden_ || contextLost_ || !canvas_) {
            return;
        }
        if (resizePending_) {
            resizePending_ = false;
            if (!resizeSurface(false)) {
                return;
            }
        }

        const double now = emscripten_get_now();
        const double delta = std::clamp(
            (now - lastFrameMilliseconds_) / 1000.0, 0.0, 0.1);
        lastFrameMilliseconds_ = now;
        animationSeconds_ += delta;

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.03f, 0.04f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        canvas_->beginFrame();
        const float sceneTime = static_cast<float>(
            fixedTimeSeconds_ >= 0.0 ? fixedTimeSeconds_ : animationSeconds_);
        scene_->onFrame(*canvas_, whatscanvas::desktop::FrameInfo{
            logicalWidth_, logicalHeight_, sceneTime, frameIndex_});
        canvas_->endFrame();

        ++frameIndex_;
        ++framesInWindow_;
        elapsedInWindow_ += delta;
        if (elapsedInWindow_ >= 1.0) {
            publishFrameRate(
                static_cast<double>(framesInWindow_) / elapsedInWindow_, frameIndex_);
            framesInWindow_ = 0;
            elapsedInWindow_ = 0.0;
        }
    }

    void handleContextLost()
    {
        contextLost_ = true;
        if (canvas_) {
            canvas_->abandonContext();
            scene_->onCanvasReleasing();
        }
        publishState("context-lost", physicalWidth_, physicalHeight_,
                     logicalWidth_, logicalHeight_, devicePixelRatio_,
                     "WebGL context lost; waiting for restoration");
    }

    bool handleContextRestored()
    {
        if (!canvas_
            || emscripten_webgl_make_context_current(context_) != EMSCRIPTEN_RESULT_SUCCESS
            || !wsc::Canvas::loadOpenGL(&loadWebGlProcedure)
            || !canvas_->initializeContext()) {
            fail("WebGL context restoration failed");
            return false;
        }
        contextLost_ = false;
        if (!resizeSurface(true)) {
            return false;
        }
        scene_->onCanvasReady(*canvas_);
        scene_->onLayout(*canvas_, logicalWidth_, logicalHeight_);
        lastFrameMilliseconds_ = emscripten_get_now();
        publishReady();
        return true;
    }

    void publishReady() const
    {
        const std::string detail = "WebGL 2 ready: "
            + std::to_string(physicalWidth_) + "x"
            + std::to_string(physicalHeight_) + " @ "
            + std::to_string(devicePixelRatio_) + " DPR; "
            + wsc::Canvas::getOpenGLVersionString();
        publishState("ready", physicalWidth_, physicalHeight_,
                     logicalWidth_, logicalHeight_, devicePixelRatio_,
                     detail.c_str());
        std::fprintf(stdout, "%s\n", detail.c_str());
    }

    void fail(const char* message) const
    {
        publishState("error", physicalWidth_, physicalHeight_,
                     logicalWidth_, logicalHeight_, devicePixelRatio_, message);
        std::fprintf(stderr, "[WhatsCanvasWeb] %s\n", message);
    }

    static void mainLoop(void* userData)
    {
        static_cast<WebApp*>(userData)->drawFrame();
    }

    static EM_BOOL onResize(int, const EmscriptenUiEvent*, void* userData)
    {
        static_cast<WebApp*>(userData)->resizePending_ = true;
        return EM_TRUE;
    }

    static EM_BOOL onOrientationChange(
        int, const EmscriptenOrientationChangeEvent*, void* userData)
    {
        static_cast<WebApp*>(userData)->resizePending_ = true;
        return EM_TRUE;
    }

    static EM_BOOL onVisibilityChange(
        int, const EmscriptenVisibilityChangeEvent* event, void* userData)
    {
        auto* app = static_cast<WebApp*>(userData);
        app->hidden_ = event->hidden != 0;
        if (!app->hidden_) {
            app->lastFrameMilliseconds_ = emscripten_get_now();
            app->resizePending_ = true;
        }
        return EM_TRUE;
    }

    static EM_BOOL onContextLost(int, const void*, void* userData)
    {
        static_cast<WebApp*>(userData)->handleContextLost();
        return EM_TRUE;
    }

    static EM_BOOL onContextRestored(int, const void*, void* userData)
    {
        return static_cast<WebApp*>(userData)->handleContextRestored()
            ? EM_TRUE : EM_FALSE;
    }

    whatscanvas::desktop::ScenePtr scene_;
    std::unique_ptr<wsc::Canvas> canvas_;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context_ = 0;
    int physicalWidth_ = 0;
    int physicalHeight_ = 0;
    float logicalWidth_ = 0.0f;
    float logicalHeight_ = 0.0f;
    float devicePixelRatio_ = 1.0f;
    double fixedTimeSeconds_ = -1.0;
    double lastFrameMilliseconds_ = 0.0;
    double animationSeconds_ = 0.0;
    double elapsedInWindow_ = 0.0;
    int frameIndex_ = 0;
    int framesInWindow_ = 0;
    bool resizePending_ = false;
    bool hidden_ = false;
    bool contextLost_ = false;
};

} // namespace

int main()
{
    auto app = std::make_unique<WebApp>();
    if (!app->start()) {
        return 1;
    }
    app.release();
    return 0;
}
