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

namespace chess {
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
} // namespace
void runAnimationTests(wsc::Canvas& canvas, const std::string& framesDirectory) {
    canvas.setSize(1120,820); canvas.setDevicePixelRatio(1);
    if(!framesDirectory.empty()) std::filesystem::create_directories(framesDirectory);
    Probe p(canvas,Position::initial(),framesDirectory);
    p.frame(0); const auto entrance=p.pixels(); p.advance(0.7);
    const auto ready=p.pixels();
    check(differentPatch(entrance,ready,386,602,28),"opening reveals white pawn");
    p.click(4,6); p.advance(0.2);
    check(differentPatch(ready,p.pixels(),356,602,12),"selection fades in without moving piece");
    p.click(4,4); p.frame(0);
    const auto before=p.pixels(); const auto builds=p.renderer.cacheBuilds();
    p.advance(motion::Travel/2);
    check(differentPatch(before,p.pixels(),386,530,25),"pawn occupies intermediate square");
    check(p.renderer.cacheBuilds()==builds,"piece motion does not rebuild textures");
    p.advance(motion::Travel/2+motion::Landing+0.02);
    check(p.game.match().moves().size()==1 && !p.game.boardBusy(),"AI waits for presentation");
    p.game.undo(); p.advance(motion::StepDuration+0.1);
    check(p.game.match().position().sameBoard(Position::initial()),"undo restores pawn and en passant rights");
    const auto beforeHover=p.pixels(); p.renderer.pointerMove(890,650); p.advance(0.2);
    check(differentPatch(beforeHover,p.pixels(),760,650,14),"hover feedback visible");
    p.renderer.pointerPressed(true); p.advance(0.1); p.renderer.pointerPressed(false);
    p.renderer.pointerMove(-100,-100); p.advance(0.2);

    Probe castle(canvas,Position::fromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    castle.frame(0); castle.advance(0.7);
    const auto castleBefore=castle.pixels();
    castle.click(4,7); castle.click(6,7); castle.frame(0); castle.advance(motion::Travel/2);
    check(differentPatch(castleBefore,castle.pixels(),458,674,22),"castling king slides through f1");
    check(differentPatch(castleBefore,castle.pixels(),530,674,22),"castling rook slides through g1 simultaneously");
    check(castle.game.transition().steps[0].companion.valid(),"castling keeps companion rook motion");
    castle.game.undo(); castle.advance(motion::StepDuration+0.1);
    check(castle.game.match().position().castling==15,"castling undo restores rights");

    const auto epPosition=Position::fromFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    Probe ep(canvas,epPosition); ep.frame(0); ep.advance(0.7);
    const auto victim=ep.pixels();
    ep.click(4,3); ep.click(3,2); ep.advance(motion::StepDuration+0.01);
    check(differentPatch(victim,ep.pixels(),314,386,24),"en passant removes pawn on adjacent square");
    check(ep.game.transition().steps[0].victim==square(3,3),"en passant effect uses actual victim square");
    ep.game.undo(); ep.advance(motion::StepDuration+0.1);
    check(ep.game.match().position().sameBoard(epPosition),"en passant undo restores victim and opportunity");

    const auto promotion=Position::fromFen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    Probe promote(canvas,promotion); promote.frame(0); promote.advance(0.7);
    const auto beforeChoice=promote.pixels(); promote.click(0,1); promote.click(0,0); promote.frame(0);
    check(promote.game.promoting() && differentPatch(beforeChoice,promote.pixels(),767,540,20),"promotion chooser is drawn");
    const auto knight=layout::Promotion[3]; promote.game.click(knight.x+30,knight.y+20);
    promote.advance(motion::StepDuration+0.05);
    check(promote.game.match().position().board[0].kind==Kind::Knight,"promotion button chooses knight");
    check(differentPatch(beforeChoice,promote.pixels(),98,170,24),"promoted silhouette appears at destination");
    promote.game.undo(); promote.advance(motion::StepDuration+0.1);
    check(promote.game.match().position().sameBoard(promotion),"promotion undo restores pawn");

    Probe victory(canvas,Position::fromFen("7k/6Q1/5K2/8/8/8/8/8 b - - 0 1"));
    victory.frame(0); victory.advance(0.62); const auto noResult=victory.pixels(); victory.advance(0.3);
    check(victory.game.match().outcome()==Outcome::WhiteWins && differentPatch(noResult,victory.pixels(),350,433,40),"checkmate result fades in");
    std::sort(p.timings.begin(),p.timings.end());
    const double p95=p.timings[static_cast<std::size_t>(p.timings.size()*0.95)];
    check(p95<16.67 && p.draws<=24,"animation performance budget");
    std::cout << "ANIMATION frames=" << p.timings.size() << " completed_p95_ms=" << p95 << " max_draw_calls=" << p.draws << '\n';
    std::cout << "PASS animation: planar moves, castling, en passant, promotion, undo, hover and checkmate\n";
}
} // namespace chess
