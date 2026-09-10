#include "Renderer.h"
#include "AnimationTests.h"
#include <GLFW/glfw3.h>
#include <wsc/CanvasStats.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace chess;
struct Host {
    GLFWwindow* window;
    wsc::Canvas& canvas;
    Game game;
    Renderer renderer;
    GLFWcursor* handCursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    int width = 1120, height = 820, fbWidth = 1120, fbHeight = 820;
    double lastPrepareMs = 0;

    Host(GLFWwindow* w, wsc::Canvas& c, Difficulty difficulty) : window(w), canvas(c), game(difficulty) {}
    ~Host() { if (handCursor) glfwDestroyCursor(handCursor); }
    void resize() {
        glfwGetWindowSize(window, &width, &height);
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        if (fbWidth <= 0 || fbHeight <= 0 || width <= 0 || height <= 0) return;
        glViewport(0, 0, fbWidth, fbHeight);
        canvas.setSize(fbWidth, fbHeight);
        canvas.setDevicePixelRatio(static_cast<float>(fbWidth) / width);
    }
    void pointer(double x, double y) {
        const auto p = layout::Viewport::fit(static_cast<float>(width), static_cast<float>(height))
                           .toDesign(static_cast<float>(x), static_cast<float>(y));
        game.click(p.first, p.second);
    }
    void hover(double x, double y) {
        const auto p = layout::Viewport::fit(static_cast<float>(width), static_cast<float>(height))
                           .toDesign(static_cast<float>(x), static_cast<float>(y));
        renderer.pointerMove(p.first, p.second);
        const int s = layout::hitSquare(p.first, p.second);
        const bool piece = s >= 0 && game.match().position().board[s]
            && game.match().position().board[s].side == Side::White && !game.thinking()
            && !game.boardBusy() && !game.dialog() && game.match().outcome() == Outcome::Playing;
        glfwSetCursor(window, game.controlAt(p.first, p.second) >= 0 || piece ? handCursor : nullptr);
    }
    void key(int key) {
        if (key == GLFW_KEY_ESCAPE) { game.cancelDialog(); game.clearSelection(); }
        else if (game.promoting()) {
            if (key == GLFW_KEY_Q) game.choosePromotion(Kind::Queen);
            else if (key == GLFW_KEY_R) game.choosePromotion(Kind::Rook);
            else if (key == GLFW_KEY_B) game.choosePromotion(Kind::Bishop);
            else if (key == GLFW_KEY_N) game.choosePromotion(Kind::Knight);
        }
        else if (game.dialog()) { if (key == GLFW_KEY_ENTER) game.confirmNewGame(); }
        else if (key == GLFW_KEY_D) game.claimDraw();
        else if (key == GLFW_KEY_U) game.undo();
        else if (key == GLFW_KEY_N) game.requestNewGame(game.match().difficulty());
        else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_3) game.requestNewGame(static_cast<Difficulty>(key - GLFW_KEY_1));
    }
    double frame(bool swap = true) {
        if (fbWidth <= 0 || fbHeight <= 0) return 0;
        game.update();
        const float pixelScale = layout::Viewport::fit(static_cast<float>(width), static_cast<float>(height)).scale
                               * static_cast<float>(fbWidth) / width;
        // Cache construction/readback must be outside the destination's frame.
        const auto prepareStart = std::chrono::steady_clock::now();
        renderer.prepare(canvas, game, pixelScale);
        game.beginPresentation();
        lastPrepareMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - prepareStart).count();
        const auto start = std::chrono::steady_clock::now();
        canvas.beginFrame();
        renderer.draw(canvas, game, static_cast<float>(width), static_cast<float>(height), game.now());
        canvas.endFrame();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (swap) glfwSwapBuffers(window);
        return ms;
    }
};
Host& host(GLFWwindow* w) { return *static_cast<Host*>(glfwGetWindowUserPointer(w)); }
void mouseCallback(GLFWwindow* w, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    double x, y;
    glfwGetCursorPos(w, &x, &y);
    host(w).hover(x, y);
    host(w).renderer.pointerPressed(action == GLFW_PRESS);
    if (action == GLFW_PRESS) host(w).pointer(x, y);
}
void keyCallback(GLFWwindow* w, int key, int, int action, int) {
    if (action == GLFW_PRESS) host(w).key(key);
}
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("Desktop smoke: " + message);
}
void clickDesign(Host& h, float x, float y) {
    const auto view = layout::Viewport::fit(static_cast<float>(h.width), static_cast<float>(h.height));
    // Inject through the same window-coordinate input path as the GLFW mouse callback.
    h.pointer(view.x + x * view.scale, view.y + y * view.scale);
}
void clickSquare(Host& h, int x, int y) {
    clickDesign(h, layout::BoardX + x * layout::Cell, layout::BoardY + y * layout::Cell);
}
void checkSidebarPixels(Host& h) {
    std::vector<unsigned char> pixels;
    require(h.canvas.readPixelsRGBA(pixels), "sidebar readback");
    const auto view = layout::Viewport::fit(static_cast<float>(h.width), static_cast<float>(h.height));
    auto pixel = [&](float x, float y, int channel) {
        const int px = static_cast<int>((view.x + x * view.scale) * h.fbWidth / h.width);
        const int py = static_cast<int>((view.y + y * view.scale) * h.fbHeight / h.height);
        return pixels[(static_cast<std::size_t>(py) * h.fbWidth + px) * 4 + channel];
    };
    // Regression for accidentally reading the host framebuffer while baking the
    // offscreen sidebar: that bug rendered a second chessboard inside this panel.
    require(pixel(742, 126, 0) < 65 && pixel(742, 126, 1) < 50, "sidebar contains dark panel, not copied board pixels");
    const Rect button = layout::DifficultyButtons[static_cast<int>(h.game.match().difficulty())];
    require(pixel(button.x + 14, button.y + 12, 0) > 150
            && pixel(button.x + 14, button.y + 12, 1) > 100, "active difficulty has visible brass fill");
}
void waitForAI(Host& h) {
    const double deadline = glfwGetTime() + 6;
    int frames = 0;
    double maxPrepareMs = 0;
    while ((h.game.thinking() || h.game.boardBusy()) && glfwGetTime() < deadline) {
        h.frame(); glfwPollEvents(); ++frames;
        maxPrepareMs = std::max(maxPrepareMs, h.lastPrepareMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(!h.game.thinking() && !h.game.boardBusy(), "AI and landing must finish while desktop continues rendering");
    std::cout << "AI_RENDER frames=" << frames << " search_ms=" << h.game.lastSearch().milliseconds
              << " max_cache_prepare_ms=" << maxPrepareMs << '\n';
}
void settle(Host& h) {
    const double deadline = glfwGetTime() + 3;
    while (h.game.transition().active(h.game.now()) && glfwGetTime() < deadline) {
        h.frame(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    require(!h.game.transition().active(h.game.now()), "visual transition completes");
    h.frame(false);
}
void smoke(Host& h, const std::string& capture) {
    glfwSwapInterval(0);
    h.frame(false);
    settle(h);
    checkSidebarPixels(h);
    std::vector<unsigned char> before, selected, after;
    require(h.canvas.readPixelsRGBA(before) && !before.empty(), "initial framebuffer readback");
    clickSquare(h, 4, 6); h.frame(false);
    const double selectedAt = glfwGetTime();
    while (glfwGetTime() - selectedAt < 0.18) {
        h.frame(false); std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    require(h.game.selected() == square(4, 6), "select pawn via host input");
    require(h.canvas.readPixelsRGBA(selected) && selected != before, "selection changes actual pixels");
    clickSquare(h, 4, 4); h.frame(false);
    require(h.game.thinking(), "move hands turn to AI");
    waitForAI(h);
    require(h.game.match().moves().size() == 2, "AI reply appears on board");
    h.frame(false);
    require(h.canvas.readPixelsRGBA(after) && after != before, "played board changes actual pixels");
    keyCallback(h.window, GLFW_KEY_U, 0, GLFW_PRESS, 0); h.frame(false);
    require(h.game.match().position().sameBoard(Position::initial()), "keyboard undo restores board");

    keyCallback(h.window, GLFW_KEY_3, 0, GLFW_PRESS, 0);
    clickSquare(h, 4, 6); clickSquare(h, 4, 5); h.frame();
    keyCallback(h.window, GLFW_KEY_U, 0, GLFW_PRESS, 0);
    require(h.game.match().moves().size() == 1, "hard mode blocks keyboard undo");
    waitForAI(h);
    keyCallback(h.window, GLFW_KEY_N, 0, GLFW_PRESS, 0); h.frame();
    require(h.game.dialog(), "restart dialog rendered");
    keyCallback(h.window, GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
    require(!h.game.dialog() && h.game.match().moves().size() == 2, "escape cancels restart");
    keyCallback(h.window, GLFW_KEY_N, 0, GLFW_PRESS, 0);
    keyCallback(h.window, GLFW_KEY_ENTER, 0, GLFW_PRESS, 0);
    require(h.game.match().moves().empty(), "enter confirms restart");

    for (auto size : {std::pair<int, int>{840, 615}, {1400, 900}, {1680, 1230}, {1120, 820}}) {
        glfwSetWindowSize(h.window, size.first, size.second); glfwPollEvents(); h.resize();
        h.frame(false); checkSidebarPixels(h); clickSquare(h, 2, 6);
        require(h.game.selected() == square(2, 6), "resized host maps clicks to board");
        h.game.clearSelection();
        if (size.first == 1680 && !capture.empty())
            require(h.canvas.savePixelsPPM(capture + ".large.ppm"), "large raster-cache screenshot saved");
    }
    keyCallback(h.window, GLFW_KEY_1, 0, GLFW_PRESS, 0);
    clickSquare(h, 2, 6); clickSquare(h, 2, 5); waitForAI(h);
    require(h.game.match().moves().size() == 2, "easy mode completes full turn");
    h.game.undo();
    settle(h);
    clickSquare(h, 1, 7); // Knight destinations in the screenshot.
    h.frame(false);
    if (!capture.empty()) require(h.canvas.savePixelsPPM(capture), "screenshot saved");
    h.game.clearSelection(); h.frame(false);
    // Let selection highlight fade before measuring the genuinely idle board.
    std::this_thread::sleep_for(std::chrono::milliseconds(400)); h.frame(false);

    // Stable warm frames must reuse all Images: no CPU rebakes, texture uploads,
    // glyph rasterization or artwork path construction. Measure submitted draw calls
    // and synchronized completion as well as CPU submission (which excludes GPU work).
    const auto builds = h.renderer.cacheBuilds();
    std::vector<double> timings, completedTimings;
    std::size_t maxDraws = 0, maxCommands = 0;
    for (int i = 0; i < 180; ++i) {
        const auto start = std::chrono::steady_clock::now();
        timings.push_back(h.frame(false));
        glFinish();
        completedTimings.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        const auto& stats = h.canvas.getRenderStats();
        if (i == 0) std::cout << "WARM_STATS glyphs=" << stats.glyphRasterizationCount
            << " path_vertices=" << stats.pathTessellatedVertexCount << " draws=" << stats.drawCallCount
            << " commands=" << stats.commandCount << " quads=" << stats.imageBatchQuadCount << '\n';
        maxDraws = std::max(maxDraws, stats.drawCallCount);
        maxCommands = std::max(maxCommands, stats.commandCount);
        // drawColor contributes one six-vertex background quad; artwork is all Images.
        require(stats.glyphRasterizationCount == 0 && stats.pathTessellatedVertexCount <= 6, "warm frame uses baked artwork");
    }
    require(h.renderer.cacheBuilds() == builds, "warm frames do not rebuild images");
    require(maxDraws <= 12, "warm frame draw-call budget exceeded");
    std::sort(timings.begin(), timings.end());
    std::sort(completedTimings.begin(), completedTimings.end());
    std::cout << "RENDER frames=180 cpu_median_ms=" << timings[90] << " cpu_p95_ms=" << timings[171]
              << " completed_median_ms=" << completedTimings[90] << " completed_p95_ms=" << completedTimings[171]
              << " max_draw_calls=" << maxDraws << " max_commands=" << maxCommands
              << " cache_builds=" << builds << " framebuffer=" << h.fbWidth << 'x' << h.fbHeight << '\n';
    require(completedTimings[171] < 16.67, "completed warm-frame p95 exceeds 60fps budget");
    std::cout << "PASS desktop smoke: pixels, input, all AI levels, undo, restart, resize, caches, frame budget\n";
}
} // namespace

int main(int argc, char** argv) {
    bool smokeTest = false;
    bool animationTest = false;
    std::string framesDirectory;
    std::string capture;
    Difficulty difficulty = Difficulty::Medium;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--smoke-test") smokeTest = true;
        else if (arg == "--animation-test") animationTest = true;
        else if (arg == "--frames-dir" && i + 1 < argc) framesDirectory = argv[++i];
        else if (arg == "--capture" && i + 1 < argc) capture = argv[++i];
        else if (arg == "--difficulty" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (value == "easy") difficulty = Difficulty::Easy;
            else if (value == "medium") difficulty = Difficulty::Medium;
            else if (value == "hard") difficulty = Difficulty::Hard;
            else { std::cerr << "Expected easy, medium or hard\n"; return 1; }
        } else if (arg == "--help") {
            std::cout << "Chess [--difficulty easy|medium|hard] [--capture frame.ppm] [--smoke-test]\n"
                         "        [--animation-test [--frames-dir output-directory]]\n";
            return 0;
        } else { std::cerr << "Unknown/incomplete argument: " << arg << '\n'; return 1; }
    }
    if (!glfwInit()) { std::cerr << "Cannot initialize GLFW\n"; return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_SAMPLES, 4);
    if (smokeTest || animationTest || !capture.empty()) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(1120, 820, "Chess - WhatsCanvas", nullptr, nullptr);
    if (!window) { std::cerr << "Cannot create OpenGL 3.3 window\n"; glfwTerminate(); return 1; }
    glfwSetWindowSizeLimits(window, 840, 615, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    int result = 0;
    try {
        if (!wsc::Canvas::loadOpenGL(reinterpret_cast<wsc::Canvas::OpenGLProcAddress>(glfwGetProcAddress)))
            throw std::runtime_error("Cannot load OpenGL");
        glEnable(0x809D); // GL_MULTISAMPLE
        auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, 1120, 820);
        if (!canvas || !canvas->initializeContext()) throw std::runtime_error("Cannot create Canvas");
        // Game/Renderer and their Images are destroyed while the context is current.
        Host h(window, *canvas, difficulty);
        glfwSetWindowUserPointer(window, &h);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int) { host(w).resize(); });
        glfwSetWindowSizeCallback(window, [](GLFWwindow* w, int, int) { host(w).resize(); });
        glfwSetWindowContentScaleCallback(window, [](GLFWwindow* w, float, float) { host(w).resize(); });
        glfwSetMouseButtonCallback(window, mouseCallback);
        glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) { host(w).hover(x, y); });
        glfwSetCursorEnterCallback(window, [](GLFWwindow* w, int entered) {
            if (!entered) { host(w).renderer.pointerMove(-100, -100); host(w).renderer.pointerPressed(false); }
        });
        glfwSetKeyCallback(window, keyCallback);
        h.resize();
        if (animationTest) runAnimationTests(*canvas, framesDirectory);
        else if (smokeTest) smoke(h, capture);
        else if (!capture.empty()) { h.frame(false); settle(h); require(canvas->savePixelsPPM(capture), "capture saved"); }
        else while (!glfwWindowShouldClose(window)) {
            const double started = glfwGetTime();
            h.frame();
            // Sleep when minimized and avoid busy looping on drivers that ignore VSYNC.
            const double remainder = 1.0 / 60 - (glfwGetTime() - started);
            if (remainder > 0) glfwWaitEventsTimeout(remainder);
            else glfwPollEvents();
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
