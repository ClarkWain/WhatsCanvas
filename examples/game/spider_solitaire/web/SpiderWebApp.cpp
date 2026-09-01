#include "../common/SpiderGame.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
#include <GLES3/gl3.h>
#include <wsc/FontSystem.h>
#include <wsc/wsc.h>

namespace {

constexpr const char* kCanvasSelector = "#spider-canvas";
constexpr const char* kLatinFontPath = "/fonts/Roboto-Regular.ttf";
constexpr const char* kCjkFontPath = "/fonts/Mplus1p-Regular.ttf";
constexpr const char* kEmojiFontPath = "/fonts/NotoColorEmoji.demo.subset.ttf";

EM_JS(double, queryDevicePixelRatio, (), {
    const raw = new URLSearchParams(window.location.search).get('dpr');
    const override = raw === null ? NaN : Number(raw);
    const value = Number.isFinite(override) ? override : window.devicePixelRatio;
    return Math.max(1.0, Math.min(4.0, value || 1.0));
});

EM_JS(int, queryBoolFromUrl, (const char* key, int fallback), {
    const value = new URLSearchParams(window.location.search).get(UTF8ToString(key));
    if (value === null) return fallback;
    const normalized = value.trim().toLowerCase();
    if (normalized === '1' || normalized === 'true' || normalized === 'yes' || normalized === 'on') return 1;
    if (normalized === '0' || normalized === 'false' || normalized === 'no' || normalized === 'off') return 0;
    return fallback;
});

EM_JS(int, queryDifficultyFromUrl, (), {
    const raw = new URLSearchParams(window.location.search).get('suits');
    const value = raw === null ? 1 : Number(raw);
    if (value === 2 || value === 4) return value;
    return 1;
});

EM_JS(double, querySeedFromUrl, (), {
    const raw = new URLSearchParams(window.location.search).get('seed');
    if (raw === null) return -1.0;
    const value = Number(raw);
    if (!Number.isInteger(value) || value < 0 || value > 4294967295) return -1.0;
    return value;
});

EM_JS(int, queryTouchCapability, (), {
    return (('ontouchstart' in window) || navigator.maxTouchPoints > 0) ? 1 : 0;
});

EM_JS(void, publishState,
      (const char* state, int physicalWidth, int physicalHeight,
       double logicalWidth, double logicalHeight, double dpr,
       const char* detail), {
    const stateText = UTF8ToString(state);
    const detailText = UTF8ToString(detail);
    const snapshot = window.spiderSolitaireWeb || {};
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
    window.spiderSolitaireWeb = snapshot;
    document.documentElement.dataset.spiderState = stateText;
    const status = document.getElementById('status');
    if (status) {
        status.textContent = detailText;
        status.hidden = stateText === 'ready';
    }
});

EM_JS(void, publishFrameRate, (double fps, int frameIndex), {
    const snapshot = window.spiderSolitaireWeb || {};
    snapshot.fps = fps;
    snapshot.frameIndex = frameIndex;
    window.spiderSolitaireWeb = snapshot;
    const fpsNode = document.getElementById('hud-fps');
    if (fpsNode) fpsNode.textContent = 'FPS ' + fps.toFixed(1);
});

EM_JS(void, publishMode, (int suits, int useCache, int useRaster, int useAtlas, int touchMode), {
    const snapshot = window.spiderSolitaireWeb || {};
    snapshot.suits = suits;
    snapshot.usePictureCache = !!useCache;
    snapshot.rasterizePictures = !!useRaster;
    snapshot.useCardAtlas = !!useAtlas;
    snapshot.touchInputMode = !!touchMode;
    window.spiderSolitaireWeb = snapshot;
    const modeNode = document.getElementById('hud-mode');
    if (!modeNode) return;
    modeNode.textContent =
        suits + '-suit | cache ' + (useCache ? 'on' : 'off')
        + ' | raster ' + (useRaster ? 'on' : 'off')
        + ' | atlas ' + (useAtlas ? 'on' : 'off')
        + ' | touch ' + (touchMode ? 'on' : 'off');
});

EM_JS(void, installInputGuards, (), {
    const canvas = document.getElementById('spider-canvas');
    if (!canvas) return;
    canvas.addEventListener('contextmenu', function(event) {
        event.preventDefault();
    });
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

    wsc::FontFallbackChain primaryChain(wsc::FontSystem::kDefaultPrimaryFamily);
    primaryChain.addFallbackFamily(wsc::FontSystem::kDefaultCjkFamily);
    primaryChain.addFallbackFamily(wsc::FontSystem::kDefaultSymbolFamily);

    wsc::FontFallbackChain cjkChain(wsc::FontSystem::kDefaultCjkFamily);
    cjkChain.addFallbackFamily(wsc::FontSystem::kDefaultSymbolFamily);

    const bool primaryFallback = canvas.setFontFallbackChain(primaryChain);
    const bool cjkFallback = canvas.setFontFallbackChain(cjkChain);
    return latin && cjk && emoji && primaryFallback && cjkFallback;
}

bool pickTouchPoint(const EmscriptenTouchEvent* event, float& x, float& y)
{
    for (int i = 0; i < event->numTouches; ++i) {
        const auto& touch = event->touches[i];
        if (!touch.isChanged) continue;
        x = static_cast<float>(touch.targetX);
        y = static_cast<float>(touch.targetY);
        return true;
    }
    for (int i = 0; i < event->numTouches; ++i) {
        const auto& touch = event->touches[i];
        if (!touch.onTarget) continue;
        x = static_cast<float>(touch.targetX);
        y = static_cast<float>(touch.targetY);
        return true;
    }
    return false;
}

class SpiderWebApp
{
public:
    SpiderWebApp()
        : game_(queryDifficultyFromUrl(), readSeed())
    {
        touchInputMode_ = queryBoolFromUrl("touch", queryTouchCapability() != 0 ? 1 : 0) != 0;
        usePictureCache_ = queryBoolFromUrl("cache", 1) != 0;
        rasterizePictures_ = queryBoolFromUrl("raster", 1) != 0;
        useCardAtlas_ = queryBoolFromUrl("atlas", 0) != 0;
        const bool runIntroAnimation = queryBoolFromUrl("intro", 1) != 0;

        game_.setTouchInputMode(touchInputMode_);
        game_.setUsePictureCache(usePictureCache_);
        game_.setRasterizePictures(rasterizePictures_);
        game_.setUseCardAtlas(useCardAtlas_);
        if (runIntroAnimation) game_.startIntroAnimation();
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
            EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_FALSE, &SpiderWebApp::onResize);
        emscripten_set_orientationchange_callback(
            this, EM_FALSE, &SpiderWebApp::onOrientationChange);
        emscripten_set_visibilitychange_callback(
            this, EM_FALSE, &SpiderWebApp::onVisibilityChange);
        emscripten_set_webglcontextlost_callback(
            kCanvasSelector, this, EM_FALSE, &SpiderWebApp::onContextLost);
        emscripten_set_webglcontextrestored_callback(
            kCanvasSelector, this, EM_FALSE, &SpiderWebApp::onContextRestored);

        emscripten_set_mousemove_callback(kCanvasSelector, this, EM_TRUE, &SpiderWebApp::onMouseMove);
        emscripten_set_mousedown_callback(kCanvasSelector, this, EM_TRUE, &SpiderWebApp::onMouseDown);
        emscripten_set_mouseup_callback(kCanvasSelector, this, EM_TRUE, &SpiderWebApp::onMouseUp);
        emscripten_set_mouseleave_callback(kCanvasSelector, this, EM_TRUE, &SpiderWebApp::onMouseLeave);

        emscripten_set_touchstart_callback(kCanvasSelector, this, EM_TRUE, &SpiderWebApp::onTouchStart);
        emscripten_set_touchmove_callback(kCanvasSelector, this, EM_TRUE, &SpiderWebApp::onTouchMove);
        emscripten_set_touchend_callback(kCanvasSelector, this, EM_TRUE, &SpiderWebApp::onTouchEnd);
        emscripten_set_touchcancel_callback(kCanvasSelector, this, EM_TRUE, &SpiderWebApp::onTouchCancel);

        emscripten_set_keydown_callback(
            EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_TRUE, &SpiderWebApp::onKeyDown);
        installInputGuards();

        lastFrameMilliseconds_ = emscripten_get_now();
        publishReady();
        publishMode(game_.difficulty(), usePictureCache_ ? 1 : 0,
                rasterizePictures_ ? 1 : 0,
                    useCardAtlas_ ? 1 : 0, touchInputMode_ ? 1 : 0);
        emscripten_set_main_loop_arg(&SpiderWebApp::mainLoop, this, 0, EM_TRUE);
        return true;
    }

private:
    static std::uint32_t readSeed()
    {
        const double seed = querySeedFromUrl();
        if (seed < 0.0) return 0;
        return static_cast<std::uint32_t>(seed);
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
        const bool changed = force || width != physicalWidth_ || height != physicalHeight_
            || std::abs(dpr - devicePixelRatio_) > 0.001f;
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
            canvas_->setDevicePixelRatio(devicePixelRatio_);
        }
        return true;
    }

    void forwardPointerMove(float x, float y)
    {
        const auto mapped = game_.toDesign(x, y);
        game_.pointerMove(mapped.first, mapped.second);
    }

    void forwardPointerDown(float x, float y)
    {
        const auto mapped = game_.toDesign(x, y);
        game_.pointerDown(mapped.first, mapped.second);
    }

    void forwardPointerUp(float x, float y)
    {
        const auto mapped = game_.toDesign(x, y);
        game_.pointerUp(mapped.first, mapped.second);
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
        const double delta = std::clamp((now - lastFrameMilliseconds_) / 1000.0, 0.0, 0.1);
        lastFrameMilliseconds_ = now;

        game_.update(static_cast<float>(delta));

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.01f, 0.07f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        const int windowW = std::max(1, static_cast<int>(std::lround(logicalWidth_)));
        const int windowH = std::max(1, static_cast<int>(std::lround(logicalHeight_)));

        canvas_->beginFrame();
        game_.render(*canvas_, windowW, windowH);
        canvas_->endFrame();

        ++frameIndex_;
        ++framesInWindow_;
        elapsedInWindow_ += delta;
        if (elapsedInWindow_ >= 1.0) {
            publishFrameRate(static_cast<double>(framesInWindow_) / elapsedInWindow_, frameIndex_);
            publishMode(game_.difficulty(), usePictureCache_ ? 1 : 0,
                        rasterizePictures_ ? 1 : 0,
                        useCardAtlas_ ? 1 : 0, touchInputMode_ ? 1 : 0);
            framesInWindow_ = 0;
            elapsedInWindow_ = 0.0;
        }
    }

    void handleContextLost()
    {
        contextLost_ = true;
        if (canvas_) {
            canvas_->abandonContext();
        }
        game_.releaseGpuResources();
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

        if (!registerWebFonts(*canvas_)) {
            fail("Portable Web fonts could not be registered after restoration");
            return false;
        }

        contextLost_ = false;
        game_.releaseGpuResources();

        if (!resizeSurface(true)) {
            return false;
        }

        lastFrameMilliseconds_ = emscripten_get_now();
        publishReady();
        publishMode(game_.difficulty(), usePictureCache_ ? 1 : 0,
                rasterizePictures_ ? 1 : 0,
                    useCardAtlas_ ? 1 : 0, touchInputMode_ ? 1 : 0);
        return true;
    }

    void publishReady() const
    {
        const std::string detail = "Spider Solitaire Web ready: "
            + std::to_string(physicalWidth_) + "x" + std::to_string(physicalHeight_)
            + " @ " + std::to_string(devicePixelRatio_)
            + " DPR; suits=" + std::to_string(game_.difficulty());
        publishState("ready", physicalWidth_, physicalHeight_,
                     logicalWidth_, logicalHeight_, devicePixelRatio_, detail.c_str());
        std::fprintf(stdout, "%s\n", detail.c_str());
    }

    void fail(const char* message) const
    {
        publishState("error", physicalWidth_, physicalHeight_,
                     logicalWidth_, logicalHeight_, devicePixelRatio_, message);
        std::fprintf(stderr, "[SpiderSolitaireWeb] %s\n", message);
    }

    static void mainLoop(void* userData)
    {
        static_cast<SpiderWebApp*>(userData)->drawFrame();
    }

    static EM_BOOL onResize(int, const EmscriptenUiEvent*, void* userData)
    {
        static_cast<SpiderWebApp*>(userData)->resizePending_ = true;
        return EM_TRUE;
    }

    static EM_BOOL onOrientationChange(
        int, const EmscriptenOrientationChangeEvent*, void* userData)
    {
        static_cast<SpiderWebApp*>(userData)->resizePending_ = true;
        return EM_TRUE;
    }

    static EM_BOOL onVisibilityChange(
        int, const EmscriptenVisibilityChangeEvent* event, void* userData)
    {
        auto* app = static_cast<SpiderWebApp*>(userData);
        app->hidden_ = event->hidden != 0;
        if (!app->hidden_) {
            app->resizePending_ = true;
            app->lastFrameMilliseconds_ = emscripten_get_now();
        }
        return EM_TRUE;
    }

    static EM_BOOL onContextLost(int, const void*, void* userData)
    {
        static_cast<SpiderWebApp*>(userData)->handleContextLost();
        return EM_TRUE;
    }

    static EM_BOOL onContextRestored(int, const void*, void* userData)
    {
        return static_cast<SpiderWebApp*>(userData)->handleContextRestored() ? EM_TRUE : EM_FALSE;
    }

    static EM_BOOL onMouseMove(int, const EmscriptenMouseEvent* event, void* userData)
    {
        auto* app = static_cast<SpiderWebApp*>(userData);
        app->forwardPointerMove(static_cast<float>(event->targetX), static_cast<float>(event->targetY));
        return EM_TRUE;
    }

    static EM_BOOL onMouseDown(int, const EmscriptenMouseEvent* event, void* userData)
    {
        if (event->button != 0) return EM_FALSE;
        auto* app = static_cast<SpiderWebApp*>(userData);
        app->forwardPointerDown(static_cast<float>(event->targetX), static_cast<float>(event->targetY));
        return EM_TRUE;
    }

    static EM_BOOL onMouseUp(int, const EmscriptenMouseEvent* event, void* userData)
    {
        if (event->button != 0) return EM_FALSE;
        auto* app = static_cast<SpiderWebApp*>(userData);
        app->forwardPointerUp(static_cast<float>(event->targetX), static_cast<float>(event->targetY));
        return EM_TRUE;
    }

    static EM_BOOL onMouseLeave(int, const EmscriptenMouseEvent* event, void* userData)
    {
        auto* app = static_cast<SpiderWebApp*>(userData);
        app->forwardPointerUp(static_cast<float>(event->targetX), static_cast<float>(event->targetY));
        return EM_TRUE;
    }

    static EM_BOOL onTouchStart(int, const EmscriptenTouchEvent* event, void* userData)
    {
        float x = 0.0f;
        float y = 0.0f;
        if (!pickTouchPoint(event, x, y)) return EM_FALSE;
        auto* app = static_cast<SpiderWebApp*>(userData);
        app->forwardPointerMove(x, y);
        app->forwardPointerDown(x, y);
        return EM_TRUE;
    }

    static EM_BOOL onTouchMove(int, const EmscriptenTouchEvent* event, void* userData)
    {
        float x = 0.0f;
        float y = 0.0f;
        if (!pickTouchPoint(event, x, y)) return EM_FALSE;
        auto* app = static_cast<SpiderWebApp*>(userData);
        app->forwardPointerMove(x, y);
        return EM_TRUE;
    }

    static EM_BOOL onTouchEnd(int, const EmscriptenTouchEvent* event, void* userData)
    {
        float x = 0.0f;
        float y = 0.0f;
        auto* app = static_cast<SpiderWebApp*>(userData);
        if (!pickTouchPoint(event, x, y)) {
            app->game_.cancelSelection();
            return EM_TRUE;
        }
        app->forwardPointerUp(x, y);
        return EM_TRUE;
    }

    static EM_BOOL onTouchCancel(int, const EmscriptenTouchEvent* event, void* userData)
    {
        float x = 0.0f;
        float y = 0.0f;
        auto* app = static_cast<SpiderWebApp*>(userData);
        if (!pickTouchPoint(event, x, y)) {
            app->game_.cancelSelection();
            return EM_TRUE;
        }
        app->forwardPointerUp(x, y);
        return EM_TRUE;
    }

    static EM_BOOL onKeyDown(int, const EmscriptenKeyboardEvent* event, void* userData)
    {
        if (event->repeat) return EM_TRUE;

        auto* app = static_cast<SpiderWebApp*>(userData);
        switch (event->keyCode) {
        case 27:
            app->game_.cancelSelection();
            return EM_TRUE;
        case 49:
            app->game_.setDifficulty(1, false);
            publishMode(app->game_.difficulty(), app->usePictureCache_ ? 1 : 0,
                        app->rasterizePictures_ ? 1 : 0,
                        app->useCardAtlas_ ? 1 : 0, app->touchInputMode_ ? 1 : 0);
            return EM_TRUE;
        case 50:
            app->game_.setDifficulty(2, false);
            publishMode(app->game_.difficulty(), app->usePictureCache_ ? 1 : 0,
                        app->rasterizePictures_ ? 1 : 0,
                        app->useCardAtlas_ ? 1 : 0, app->touchInputMode_ ? 1 : 0);
            return EM_TRUE;
        case 52:
            app->game_.setDifficulty(4, false);
            publishMode(app->game_.difficulty(), app->usePictureCache_ ? 1 : 0,
                        app->rasterizePictures_ ? 1 : 0,
                        app->useCardAtlas_ ? 1 : 0, app->touchInputMode_ ? 1 : 0);
            return EM_TRUE;
        case 67:
            app->game_.cycleDifficulty(false);
            publishMode(app->game_.difficulty(), app->usePictureCache_ ? 1 : 0,
                        app->rasterizePictures_ ? 1 : 0,
                        app->useCardAtlas_ ? 1 : 0, app->touchInputMode_ ? 1 : 0);
            return EM_TRUE;
        case 72:
            app->game_.hint();
            return EM_TRUE;
        case 78:
        case 82:
            app->game_.newGame(0, false);
            return EM_TRUE;
        case 85:
            app->game_.undo();
            return EM_TRUE;
        default:
            return EM_FALSE;
        }
    }

    spider::SpiderGame game_;
    std::unique_ptr<wsc::Canvas> canvas_;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context_ = 0;
    int physicalWidth_ = 0;
    int physicalHeight_ = 0;
    float logicalWidth_ = 0.0f;
    float logicalHeight_ = 0.0f;
    float devicePixelRatio_ = 1.0f;
    double lastFrameMilliseconds_ = 0.0;
    double elapsedInWindow_ = 0.0;
    int frameIndex_ = 0;
    int framesInWindow_ = 0;
    bool touchInputMode_ = false;
    bool usePictureCache_ = true;
    bool rasterizePictures_ = true;
    bool useCardAtlas_ = false;
    bool resizePending_ = false;
    bool hidden_ = false;
    bool contextLost_ = false;
};

} // namespace

int main()
{
    auto app = std::make_unique<SpiderWebApp>();
    if (!app->start()) {
        return 1;
    }
    app.release();
    return 0;
}
