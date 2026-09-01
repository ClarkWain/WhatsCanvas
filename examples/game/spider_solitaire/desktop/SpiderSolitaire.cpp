#include "SpiderGame.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#include <GLFW/glfw3.h>
#include <wsc/FontSystem.h>

using namespace wsc;
using spider::SpiderGame;
using spider::DESIGN_W;
using spider::DESIGN_H;

namespace {

constexpr unsigned int kOpenGLMultisample = 0x809D;

struct GameContext {
    SpiderGame* game = nullptr;
    Canvas* canvas = nullptr;
    int windowW = static_cast<int>(DESIGN_W);
    int windowH = static_cast<int>(DESIGN_H);
};

void framebufferCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    auto* ctx = static_cast<GameContext*>(glfwGetWindowUserPointer(window));
    if (!ctx || !ctx->canvas || width <= 0 || height <= 0) return;
    int windowW = 0, windowH = 0;
    glfwGetWindowSize(window, &windowW, &windowH);
    if (windowW <= 0 || windowH <= 0) return;
    ctx->canvas->setSize(width, height);
    const float dpr = static_cast<float>(width) / static_cast<float>(windowW);
    ctx->canvas->setDevicePixelRatio(dpr);
    ctx->windowW = windowW;
    ctx->windowH = windowH;
}

void contentScaleCallback(GLFWwindow* window, float, float) {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    framebufferCallback(window, width, height);
}

void cursorCallback(GLFWwindow* window, double x, double y) {
    auto* ctx = static_cast<GameContext*>(glfwGetWindowUserPointer(window));
    if (!ctx || !ctx->game) return;
    auto p = ctx->game->toDesign(static_cast<float>(x), static_cast<float>(y));
    ctx->game->pointerMove(p.first, p.second);
}

void mouseCallback(GLFWwindow* window, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    auto* ctx = static_cast<GameContext*>(glfwGetWindowUserPointer(window));
    if (!ctx || !ctx->game) return;
    double x = 0, y = 0;
    glfwGetCursorPos(window, &x, &y);
    auto p = ctx->game->toDesign(static_cast<float>(x), static_cast<float>(y));
    if (action == GLFW_PRESS) ctx->game->pointerDown(p.first, p.second);
    else if (action == GLFW_RELEASE) ctx->game->pointerUp(p.first, p.second);
}

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    auto* ctx = static_cast<GameContext*>(glfwGetWindowUserPointer(window));
    if (!ctx || !ctx->game) return;
    if (key == GLFW_KEY_ESCAPE) {
        ctx->game->cancelSelection();
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    } else if (key == GLFW_KEY_N || key == GLFW_KEY_R) ctx->game->newGame(0, false);
    else if (key == GLFW_KEY_U) ctx->game->undo();
    else if (key == GLFW_KEY_H) ctx->game->hint();
    else if (key == GLFW_KEY_1) ctx->game->setDifficulty(1, false);
    else if (key == GLFW_KEY_2) ctx->game->setDifficulty(2, false);
    else if (key == GLFW_KEY_4) ctx->game->setDifficulty(4, false);
}

} // namespace

int main(int argc, char** argv) {
    bool selfTest = false;
    bool playTest = false;
    bool hoverTest = false;
    bool completionDemo = false;
    bool winDemo = false;
    bool exitAfterFrame = false;
    std::string capturePath;
    std::uint32_t seed = 0;
    int difficulty = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--self-test") selfTest = true;
        else if (arg == "--play-test") playTest = true;
        else if (arg == "--hover-test") hoverTest = true;
        else if (arg == "--completion-demo") completionDemo = true;
        else if (arg == "--win-demo") winDemo = true;
        else if (arg == "--capture" && i + 1 < argc) capturePath = argv[++i];
        else if (arg == "--exit-after-frame") exitAfterFrame = true;
        else if (arg == "--seed" && i + 1 < argc) seed = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        else if (arg == "--suits" && i + 1 < argc) difficulty = std::stoi(argv[++i]);
    }
    if (selfTest) return SpiderGame::runSelfTests() ? 0 : 1;
    if (playTest) return SpiderGame::runInteractionTests() ? 0 : 1;

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(static_cast<int>(DESIGN_W), static_cast<int>(DESIGN_H),
                                          "Spider Solitaire - WhatsCanvas", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!Canvas::loadOpenGL(reinterpret_cast<Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
        std::cerr << "Failed to load OpenGL\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    glEnable(kOpenGLMultisample);

    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    auto canvasOwner = Canvas::create(Canvas::Backend::OpenGL, 0, 0);
    if (!canvasOwner) {
        std::cerr << "Failed to create Canvas\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    Canvas& canvas = *canvasOwner;
    canvas.setSize(fbw, fbh);
    for (const FontFace& face : FontSystem::defaultSystemFontFaces()) canvas.registerFontFace(face);
    canvas.setFontFallbackChain(FontSystem::defaultFallbackChain());
    int windowW = 0, windowH = 0;
    glfwGetWindowSize(window, &windowW, &windowH);
    const float dpr = windowW > 0 ? static_cast<float>(fbw) / static_cast<float>(windowW) : 1.0f;
    canvas.setDevicePixelRatio(dpr);

    SpiderGame game(difficulty, seed);
    if (completionDemo || winDemo) game.startCompletionDemo(winDemo);
    else if (capturePath.empty()) game.startIntroAnimation();
    GameContext ctx{&game, &canvas, windowW, windowH};
    glfwSetWindowUserPointer(window, &ctx);
    glfwSetFramebufferSizeCallback(window, framebufferCallback);
    glfwSetWindowContentScaleCallback(window, contentScaleCallback);
    glfwSetCursorPosCallback(window, cursorCallback);
    glfwSetMouseButtonCallback(window, mouseCallback);
    glfwSetKeyCallback(window, keyCallback);

    bool hoverTestFailed = false;
    if (hoverTest) {
        game.updateRenderTransform(ctx.windowW, ctx.windowH);
        const RectF difficulty(884, 28, 112, 44);
        const auto windowPoint = game.toWindow(difficulty.getX() + difficulty.getWidth() * 0.5f,
                                               difficulty.getY() + difficulty.getHeight() * 0.5f);
        glfwSetCursorPos(window, windowPoint.first, windowPoint.second);
        glfwPollEvents();
        double cursorX = 0.0, cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        const auto mapped = game.toDesign(static_cast<float>(cursorX), static_cast<float>(cursorY));
        game.pointerMove(mapped.first, mapped.second);
        hoverTestFailed = !game.difficultyHoverMatches();
        std::cout << "HOVER_TEST " << (hoverTestFailed ? "FAIL" : "PASS")
                  << " window=" << ctx.windowW << 'x' << ctx.windowH
                  << " framebuffer=" << fbw << 'x' << fbh
                  << " dpr=" << dpr
                  << " cursor=" << cursorX << ',' << cursorY
                  << " design=" << mapped.first << ',' << mapped.second << '\n';
    }

    double last = glfwGetTime();
    double nextFrame = last;
    const double captureReadyAt = last + (capturePath.empty() ? 0.0 : winDemo ? 0.8 : 0.35);
    bool captured = false;
    bool captureFailed = false;
    constexpr double targetFrameSeconds = 1.0 / 60.0;
    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - last);
        last = now;
        glClear(GL_COLOR_BUFFER_BIT);
        canvas.beginFrame();
        game.update(dt);
        game.render(canvas, ctx.windowW, ctx.windowH);
        canvas.endFrame();
        if (!captured && !capturePath.empty() && now >= captureReadyAt) {
            captured = true;
            if (!canvas.savePixelsPPM(capturePath)) {
                std::cerr << "Failed to save capture: " << capturePath << '\n';
                captureFailed = true;
            } else {
                std::cout << "CAPTURED " << capturePath << '\n';
            }
        }
        glfwSwapBuffers(window);
        if (exitAfterFrame && (capturePath.empty() || captured)) glfwSetWindowShouldClose(window, GLFW_TRUE);
        nextFrame += targetFrameSeconds;
        const double frameRemainder = nextFrame - glfwGetTime();
        if (frameRemainder > 0.0 && !glfwWindowShouldClose(window)) {
            std::this_thread::sleep_for(std::chrono::duration<double>(frameRemainder));
        } else if (frameRemainder < -targetFrameSeconds) {
            nextFrame = glfwGetTime();
        }
        glfwPollEvents();
    }

    canvas.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return captureFailed || hoverTestFailed ? 1 : 0;
}
