#include "AnimationTests.h"
#include "Renderer.h"
#include <wsc/CanvasStats.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace xiangqi {
namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(std::string("Animation test: ") + message);
}
struct Probe {
    wsc::Canvas& canvas;
    double time = 0;
    Game game;
    Renderer renderer;
    std::vector<double> timings;
    std::size_t draws = 0;
    int frameIndex = 0;
    std::string directory;
    Probe(wsc::Canvas& c, Position p, std::string output = {})
        : canvas(c), game(Difficulty::Easy, 42, p, [this] { return time; }), directory(std::move(output)) {}
    void frame(double delta) {
        time += delta;
        game.update();
        renderer.prepare(canvas, game, 1);
        game.beginPresentation();
        const auto start = std::chrono::steady_clock::now();
        canvas.beginFrame();
        renderer.draw(canvas, game, 1120, 820, time);
        canvas.endFrame();
        glFinish();
        timings.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        draws = std::max(draws, canvas.getRenderStats().drawCallCount);
        check(canvas.getRenderStats().glyphRasterizationCount == 0, "effects must use cached glyph Images");
        if (!directory.empty()) {
            std::ostringstream file;
            file << directory << "/frame-" << std::setw(4) << std::setfill('0') << frameIndex++ << ".ppm";
            check(canvas.savePixelsPPM(file.str()), "cannot export animation frame");
        }
    }
    void advance(double seconds) {
        const int frames = std::max(1, static_cast<int>(seconds * 30 + 0.5));
        for (int i = 0; i < frames; ++i) frame(seconds / frames);
    }
    void click(int file, int rank) { game.click(layout::BoardX + file * layout::Cell, layout::BoardY + rank * layout::Cell); }
    std::vector<unsigned char> pixels() {
        std::vector<unsigned char> result;
        check(canvas.readPixelsRGBA(result), "framebuffer readback failed");
        return result;
    }
};
// Compare only the moving sprite region; full-frame differences could be caused
// solely by an unrelated thinking indicator or changed status text.
bool differentPatch(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b, int x, int y, int radius) {
    std::size_t changed = 0;
    for (int py = y - radius; py <= y + radius; ++py)
        for (int px = x - radius; px <= x + radius; ++px) {
            const std::size_t offset = (static_cast<std::size_t>(py) * 1120 + px) * 4;
            for (int channel = 0; channel < 3; ++channel)
                if (std::abs(static_cast<int>(a[offset + channel]) - b[offset + channel]) > 12) { ++changed; break; }
        }
    return changed > 40;
}
bool samePieceInterior(const std::vector<unsigned char>& before, const std::vector<unsigned char>& after,
                       int fromX, int fromY, int toX, int toY) {
    // The carved glyph must translate unchanged: scaling/bouncing would change
    // these interior pixels even if its center eventually reached the right square.
    for (int dy = -14; dy <= 14; ++dy) for (int dx = -14; dx <= 14; ++dx) {
        const std::size_t a = (static_cast<std::size_t>(fromY + dy) * 1120 + fromX + dx) * 4;
        const std::size_t b = (static_cast<std::size_t>(toY + dy) * 1120 + toX + dx) * 4;
        for (int channel = 0; channel < 3; ++channel)
            if (std::abs(static_cast<int>(before[a + channel]) - after[b + channel]) > 3) return false;
    }
    return true;
}
} // namespace
void runAnimationTests(wsc::Canvas& canvas, const std::string& framesDirectory) {
    canvas.setSize(1120, 820); canvas.setDevicePixelRatio(1);
    if (!framesDirectory.empty()) std::filesystem::create_directories(framesDirectory);
    Probe p(canvas, Position::initial(), framesDirectory);
    p.frame(0);
    const auto entrance = p.pixels();
    p.advance(0.7);
    const auto ready = p.pixels();
    check(differentPatch(entrance, ready, 158, 602, 28), "opening must visibly reveal pieces");
    p.click(1, 7); p.advance(0.2);
    check(differentPatch(ready, p.pixels(), 128, 602, 12), "selected piece must visibly highlight without moving");
    check(samePieceInterior(ready, p.pixels(), 158, 602, 158, 602), "selection must not lift or scale the piece");
    p.click(1, 0); p.frame(0);
    const auto departure = p.pixels();
    const auto builds = p.renderer.cacheBuilds();
    p.advance(motion::Travel / 2);
    const auto middle = p.pixels();
    check(differentPatch(departure, middle, 158, 378, 24), "moving cannon must occupy intermediate pixels");
    check(samePieceInterior(ready, middle, 158, 602, 158, 378), "movement must translate at fixed size without jumping");
    check(p.renderer.cacheBuilds() == builds, "moving piece must not rebake artwork");
    p.advance(motion::Travel / 2);
    const auto impact = p.pixels();
    p.advance(motion::Landing + 0.02);
    const auto landed = p.pixels();
    check(differentPatch(impact, landed, 158, 154, 32), "capture and landing effects must change impact pixels");
    check(!p.game.boardBusy() && p.game.match().moves().size() == 1, "AI waits until human presentation finishes");
    p.game.undo(); p.advance(motion::StepDuration / 2);
    check(differentPatch(landed, p.pixels(), 158, 154, 28), "undo visibly returns captured piece");
    p.advance(motion::StepDuration / 2 + 0.15);
    check(p.game.match().position().sameBoard(Position::initial()), "undo restores the starting board");
    const auto beforeHover = p.pixels();
    const auto hoverBuilds = p.renderer.cacheBuilds();
    p.renderer.pointerMove(890, 650); p.advance(0.2);
    check(differentPatch(beforeHover, p.pixels(), 760, 650, 14), "button hover visibly changes fill");
    p.renderer.pointerPressed(true); p.advance(0.1);
    p.renderer.pointerPressed(false); p.renderer.pointerMove(-100, -100); p.advance(0.2);
    check(p.renderer.cacheBuilds() == hoverBuilds, "hover must not rebuild sidebar");
    std::sort(p.timings.begin(), p.timings.end());
    const double p95 = p.timings[static_cast<std::size_t>(p.timings.size() * 0.95)];
    check(p95 < 16.67 && p.draws <= 24, "animation frame budget exceeded");
    std::cout << "ANIMATION frames=" << p.timings.size() << " completed_p95_ms=" << p95 << " max_draw_calls=" << p.draws << '\n';

    Position checkPosition;
    checkPosition.board[square(4, 9)] = {Kind::General, Side::Red};
    checkPosition.board[square(3, 0)] = {Kind::General, Side::Black};
    checkPosition.board[square(4, 2)] = {Kind::Rook, Side::Black};
    Probe warning(canvas, checkPosition);
    warning.frame(0); warning.advance(0.62);
    const auto noBanner = warning.pixels();
    warning.advance(0.25);
    const auto visibleWarning = warning.pixels();
    check(differentPatch(noBanner, visibleWarning, 350, 433, 40), "check announcement fades into the river area");
    warning.advance(1.2);
    check(differentPatch(visibleWarning, warning.pixels(), 350, 433, 40), "check banner disappears after its display interval");

    Position won;
    won.turn = Side::Black;
    won.board[square(4, 0)] = {Kind::General, Side::Black};
    won.board[square(3, 9)] = {Kind::General, Side::Red};
    for (int x : {3, 4, 5}) won.board[square(x, 2)] = {Kind::Rook, Side::Red};
    Probe victory(canvas, won);
    victory.frame(0); victory.advance(0.62);
    const auto beforeResult = victory.pixels();
    victory.advance(0.3);
    check(differentPatch(beforeResult, victory.pixels(), 350, 433, 40), "victory announcement visibly fades in");
    check(victory.game.match().outcome() == Outcome::RedWins, "visual result matches rules outcome");
    std::cout << "PASS animation pixels: entrance, selection, travel, capture, landing, undo, hover, check and victory\n";
}
} // namespace xiangqi
