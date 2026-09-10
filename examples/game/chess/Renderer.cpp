#include "Renderer.h"
#include "PieceArt.h"
#include <wsc/FontSystem.h>
#include <wsc/Path.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <filesystem>
#include <stdexcept>

namespace chess {
namespace {
using namespace wsc;
// Walnut, brass and slate: warm walnut belongs only to the board itself.
// Ambient UI (sidebar, muted text, state chips) uses a cool graphite palette
// so the deep-green desk and the ivory-and-walnut board stay as the only
// warm surfaces on screen.
const Color Ink(43, 29, 21), Muted(155, 168, 178), Red(144, 37, 24), Gold(211, 171, 101);
const Color Ivory(245, 229, 196), Panel(28, 34, 38);
Paint fill(Color color) { Paint p; p.setColor(color); return p; }
Paint stroke(Color color, float width) {
    auto p = fill(color); p.setStyle(Paint::Style::STROKE); p.setStrokeWidth(width); return p;
}
void text(Canvas& c, const std::string& label, float x, float y, float size, Color color = Ivory,
          bool center = false, bool carved = false) {
    Paint p = fill(color);
    p.setTextSize(size);
    p.setFontFamily(carved ? "Chess Carved" : "Chess UI");
    p.setFontWeight(carved ? 400 : 500);
    p.setTextBaseline(Paint::TextBaseline::MIDDLE);
    if (center) p.setTextAlign(Paint::TextAlign::CENTER);
    c.drawText(label, x, y, p);
}
void round(Canvas& c, Rect r, float radius, Color color) {
    c.drawRoundRect(RectF(r.x, r.y, r.w, r.h), radius, fill(color));
}
void line(Canvas& c, float x1, float y1, float x2, float y2, Color color, float width = 1) {
    c.drawLine(x1, y1, x2, y2, stroke(color, width));
}
void button(Canvas& c, Rect r, const std::string& label, bool active = false, bool enabled = true) {
    round(c, {r.x, r.y + 3, r.w, r.h}, 6, Color(6, 10, 12, 100));
    Paint surface;
    surface.setLinearGradient(r.x, r.y, r.x, r.y + r.h,
        active ? Color(227, 190, 123) : Color(52, 62, 70),
        active ? Color(182, 137, 70)  : Color(34, 42, 48));
    c.drawRoundRect(RectF(r.x, r.y, r.w, r.h), 6, surface);
    c.drawRoundRect(RectF(r.x + 0.5f, r.y + 0.5f, r.w - 1, r.h - 1), 6,
                    stroke(active ? Color(245, 216, 163, 160) : Color(160, 178, 190, 70), 1));
    text(c, label, r.x + r.w / 2, r.y + r.h / 2, 18,
         !enabled ? Color(110, 122, 132) : active ? Ink : Ivory, true);
}
void fonts(Canvas& c) {
    for (const auto& face : FontSystem::defaultSystemFontFaces()) c.registerFontFace(face);
    c.setFontFallbackChain(FontSystem::defaultFallbackChain());
    // Use installed fonts only; every glyph is still drawn by Canvas. Traditional
    // Chinese UI and calligraphic piece faces have independent fallback families.
    auto registerFirst = [&](const char* family, std::initializer_list<const char*> paths) {
        for (const char* path : paths) if (std::filesystem::exists(path)) {
            c.registerFontFace(FontFace::fromFile(FontDescriptor(family, 400), path));
            break;
        }
        FontFallbackChain chain(family);
        chain.addFallbackFamily(FontSystem::kDefaultCjkFamily);
        chain.addFallbackFamily(FontSystem::kDefaultPrimaryFamily);
        c.setFontFallbackChain(chain);
    };
    registerFirst("Chess UI", {"C:/Windows/Fonts/msjh.ttc", "/System/Library/Fonts/PingFang.ttc",
                               "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"});
    registerFirst("Chess Carved", {"C:/Windows/Fonts/simkai.ttf", "/System/Library/Fonts/Supplemental/Kaiti.ttc",
                                   "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc"});
}
void bake(Canvas& target, Image& image, int width, int height, int scale,
          const std::function<void(Canvas&)>& paint) {
    auto scratch = Canvas::create(Canvas::Backend::Software, width * scale, height * scale);
    if (!scratch || !scratch->initializeContext()) throw std::runtime_error("Cannot create Canvas cache surface");
    fonts(*scratch);
    scratch->beginFrame();
    scratch->save();
    scratch->drawColor(Color(0, 0, 0, 0));
    scratch->scale(static_cast<float>(scale), static_cast<float>(scale));
    paint(*scratch);
    scratch->restore();
    scratch->endFrame();
    std::vector<unsigned char> pixels;
    if (!scratch->readPixelsRGBA(pixels) || !image.replacePixelsRGBA(target, pixels, width * scale, height * scale))
        throw std::runtime_error("Cannot upload Canvas artwork cache");
}
void background(Canvas& c) {
    Paint room;
    // European club feel: dark billiards-green leather deskpad instead of warm oriental wood.
    room.setLinearGradient(0, 0, 1120, 820, Color(30, 46, 38), Color(11, 20, 17));
    c.drawRect(RectF(0, 0, 1120, 820), room);
    // Subtle table grain is generated deterministically, never loaded from a bitmap.
    for (int y = 0; y < 820; y += 4) {
        Path grain; grain.moveTo(0, static_cast<float>(y));
        grain.cubicTo(320, y + std::sin(y * 0.1f) * 8, 710, y - 8.0f, 1120, y + 3.0f);
        c.drawPath(grain, stroke(Color(150, 190, 165, y % 12 == 0 ? 14 : 5), 0.6f));
    }
    // Cool graphite monogram plate instead of the red seal-of-chess.
    round(c, {38, 28, 46, 48}, 4, Color(36, 44, 42));
    c.drawRoundRect(RectF(42, 32, 38, 40), 2, stroke(Color(233, 180, 119, 160), 1));
    text(c, "K", 61, 52, 27, Gold, true, true);
    text(c, "Chess", 107, 48, 38, Ivory, false, true);
    text(c, "64 SQUARES  ·  HUMAN vs AI", 110, 82, 12, Muted);
    text(c, "H U M A N   v s   A I", 934, 44, 15, Gold);
    text(c, "CHESS  /  WHATSCANVAS", 896, 72, 10, Muted);

    for (int i = 5; i >= 1; --i)
        round(c, {36.0f - i, 110.0f + i, 628.0f + i * 2, 658.0f + i * 2}, 17, Color(0, 0, 0, 13));
    // The lower dark edge gives the board a tangible slab thickness.
    round(c, {34, 112, 632, 663}, 15, Color(35, 20, 12));
    Paint frame;
    frame.setLinearGradient(36, 103, 664, 766, Color(123, 77, 43), Color(60, 34, 21));
    c.drawRoundRect(RectF(36, 102, 628, 662), 13, frame);
    c.drawRoundRect(RectF(39, 105, 622, 654), 11, stroke(Color(187, 139, 82), 1));
    c.drawRoundRect(RectF(47, 113, 606, 638), 6, stroke(Color(24, 14, 8, 160), 3));
    Paint wood;
    wood.setLinearGradient(50, 116, 650, 748, Color(178, 128, 76), Color(128, 83, 45));
    c.drawRoundRect(RectF(50, 116, 600, 632), 4, wood);
    c.save(); c.clipRect(RectF(52, 118, 596, 628));
    for (int y = 117; y < 750; y += 3) {
        Path grain; grain.moveTo(50, static_cast<float>(y));
        grain.cubicTo(180, y + 5.0f + std::sin(y * 0.051f) * 13,
                      440, y - 10.0f, 650, y + std::sin(y * 0.079f) * 8);
        c.drawPath(grain, stroke(y % 9 == 0 ? Color(59, 30, 12, 25) : Color(240, 189, 112, 20), 0.65f));
    }
    c.restore();
    constexpr float x = layout::BoardX, y = layout::BoardY, s = layout::Cell;
    for (int rank = 0; rank < 8; ++rank) for (int file = 0; file < 8; ++file) {
        const bool light = (rank + file) % 2 == 0;
        const float left = x + file * s - s / 2, top = y + rank * s - s / 2;
        Paint tile;
        tile.setLinearGradient(left, top, left + s, top + s,
            light ? Color(224, 206, 167) : Color(97, 66, 45),
            light ? Color(202, 178, 136) : Color(72, 48, 33));
        c.drawRect(RectF(left, top, s, s), tile);
        for (int row = 3; row < 72; row += 7)
            line(c, left + 1, top + row, left + s - 1, top + row,
                 light ? Color(115, 74, 40, 12) : Color(236, 184, 108, 10), 0.7f);
    }
    c.drawRect(RectF(x - s / 2, y - s / 2, 8 * s, 8 * s), stroke(Color(31, 20, 13), 2));
    // Coordinate labels live OUTSIDE the walnut margin, floating on the dark
    // green desk. That gives them a high-contrast background (no wood grain
    // to fight) and stops the letters from overlapping the board edge.
    const Color coordFace(240, 232, 210);
    for (int file = 0; file < 8; ++file) {
        const std::string letter(1, static_cast<char>('a' + file));
        text(c, letter, x + file * s, 96,  15, coordFace, true);
        text(c, letter, x + file * s, 764, 15, coordFace, true);
    }
    for (int rank = 0; rank < 8; ++rank) {
        const std::string number = std::to_string(8 - rank);
        text(c, number, 26,  y + rank * s, 15, coordFace, true);
        text(c, number, 674, y + rank * s, 15, coordFace, true);
    }
    c.drawCircle(302, 797, 4, fill(Ivory));
    text(c, "You play White  ·  Move first", 319, 797, 14, Ivory);
    round(c, {708, 106, 362, 662}, 12, Color(0, 0, 0, 65));
    round(c, {708, 102, 362, 662}, 12, Panel);
    c.drawRoundRect(RectF(708.5f, 102.5f, 361, 661), 12, stroke(Color(120, 140, 155, 55), 1));
    text(c, "Think first.  Play once.", 889, 797, 14, Muted, true, true);
}
void atlas(Canvas& c) {
    for (int side = 0; side < 2; ++side) for (int kind = 1; kind <= 6; ++kind)
        drawPieceArt(c, {static_cast<Kind>(kind), side == 0 ? Side::White : Side::Black},
                     (kind - 1) * 64.0f, side * 64.0f);
}
void effects(Canvas& c) {
    // Shared Image sprites for all per-frame effects; no glyph shaping, blur
    // filters or concentric geometry is submitted during the animation itself.
    for (int r = 50; r >= 44; --r)
        c.drawCircle(64, 64, static_cast<float>(r), stroke(Color(255, 212, 121, (r == 46 ? 210 : 15)), 2));
    const char* labels[] = {"CHECK", "WHITE WINS", "BLACK WINS", "DRAW", "UNDO"};
    for (int i = 0; i < 5; ++i) {
        const float x = (i % 3) * 256.0f, y = 128 + (i / 3) * 112.0f;
        round(c, {x + 5, y + 8, 246, 96}, 10, Color(0, 0, 0, 70));
        round(c, {x + 8, y + 4, 240, 92}, 8, Color(25, 32, 36, 245));
        c.drawRoundRect(RectF(x + 13, y + 9, 230, 82), 5, stroke(Gold, 1.2f));
        text(c, labels[i], x + 128, y + 47, i == 0 ? 34 : 28, i == 0 ? Color(247, 145, 91) : Ivory, true, true);
    }
}
void sidebar(Canvas& c, const Game& game) {
    const auto& match = game.match();
    c.drawRect(RectF(720, 116, 338, 634), fill(Panel));
    text(c, "Current Match", 736, 145, 25, Ivory, false, true);
    c.drawCircle(742, 184, 4.5f, fill(Ivory));
    text(c, "White  ·  You", 756, 184, 15, Ivory);
    c.drawCircle(949, 184, 4.5f, fill(Color(38, 31, 24)));
    c.drawCircle(949, 184, 4.5f, stroke(Gold, 1));
    text(c, "Black  ·  AI", 963, 184, 15, Ivory);
    round(c, {732, 207, 314, 46}, 5, match.checked() ? Color(83, 37, 26) : Color(38, 48, 58));
    c.drawRect(RectF(732, 213, 3, 34), fill(match.checked() ? Color(211, 90, 56) : Gold));
    text(c, game.status(), 889, 230, 20, match.checked() ? Color(255, 190, 150) : Gold, true);
    for (int i = 0; i < 3; ++i) button(c, layout::DifficultyButtons[i], difficultyName(static_cast<Difficulty>(i)), static_cast<int>(match.difficulty()) == i);
    const char* descriptions[] = {
        "Casual pace  ·  friendly for beginners.",
        "Balanced play  ·  weighs attack and defense.",
        "Deep search  ·  full-board planning."};
    text(c, descriptions[static_cast<int>(match.difficulty())], 889, 352, 14, Muted, true);
    line(c, 736, 380, 1042, 380, Color(155, 175, 190, 55));
    text(c, "MOVE", 736, 409, 12, Muted);
    text(c, std::to_string(match.moves().size() / 2 + 1), 736, 447, 38, Ivory);
    line(c, 879, 405, 879, 462, Color(155, 175, 190, 45));
    text(c, "LAST MOVE", 908, 409, 12, Muted);
    if (!match.moves().empty()) {
        const Move last = match.moves().back();
        const Piece piece = match.position().board[last.to];
        const std::string from = std::string(1, static_cast<char>('a' + fileOf(last.from))) + std::to_string(8 - rankOf(last.from));
        const std::string to = std::string(1, static_cast<char>('a' + fileOf(last.to))) + std::to_string(8 - rankOf(last.to));
        text(c, std::string(piece.side == Side::White ? "White " : "Black ") + pieceName(piece), 908, 444, 19, Ivory);
        text(c, from + "  →  " + to, 974, 446, 12, Gold);
    } else text(c, "No moves yet", 908, 446, 17, Muted);
    line(c, 736, 481, 1042, 481, Color(155, 175, 190, 55));
    if (game.promoting()) {
        text(c, "Choose a promotion piece   ·   Esc to cancel", 736, 502, 14, Ivory);
        const char* labels[] = {"Queen  Q", "Rook  R", "Bishop  B", "Knight  N"};
        for (int i = 0; i < 4; ++i) button(c, layout::Promotion[i], labels[i]);
    } else {
        text(c, "Click a white piece, then a highlighted square.", 736, 513, 14, Ivory);
        text(c, "Dot to move   ·   Ring to capture   ·   Gold = last move", 736, 541, 11, Muted);
    }
    button(c, layout::Undo, match.difficulty() == Difficulty::Hard ? "Undo (Hard mode)" : "Undo    U", false, game.canUndo());
    button(c, layout::Restart, "New game    N", true);
    if (game.dialog()) {
        text(c, std::string("Start a new game on \"") + difficultyName(game.pendingDifficulty()) + "\"?", 889, 687, 13, Gold, true);
        button(c, layout::Confirm, "Confirm", true);
        button(c, layout::Cancel, "Keep playing");
    } else if (match.drawClaim().available() && match.outcome() == Outcome::Playing && !game.thinking() && !game.boardBusy() && !game.promoting()) {
        button(c, layout::Claim, "Claim draw    D");
        if (match.drawClaim().intended.valid()) text(c, "by planned move " + moveUci(match.drawClaim().intended), 889, 746, 10, Muted, true);
    } else {
        text(c, "1 / 2 / 3 difficulty     Esc to deselect", 889, 708, 12, Muted, true);
        text(c, "Stalemate = draw  ·  Castling  ·  Promotion", 889, 736, 11, Muted, true);
    }
}
} // namespace

void Renderer::prepare(wsc::Canvas& canvas, const Game& game, float pixelScale) {
    if (!effects_.isTextureValid()) { bake(canvas, effects_, 768, 352, 2, effects); ++cacheBuilds_; }
    const int scale = pixelScale > 1.15f ? 2 : 1;
    if (scale != cacheScale_) {
        bake(canvas, background_, 1120, 820, scale, background);
        bake(canvas, pieces_, 384, 128, 2, atlas);
        cacheScale_ = scale;
        sidebarScratch_.reset();
        panelRevision_ = 0;
        cacheBuilds_ += 2;
    }
    if (panelRevision_ != game.revision()) {
        if (!sidebarScratch_) {
            sidebarScratch_ = Canvas::create(Canvas::Backend::OpenGL, 338 * scale, 634 * scale);
            if (!sidebarScratch_ || !sidebarScratch_->setOutputTarget(OutputTarget::OffscreenTexture())
                || !sidebarScratch_->initializeContext())
                throw std::runtime_error("Cannot create sidebar Canvas texture");
            fonts(*sidebarScratch_);
        }
        auto& panel = *sidebarScratch_;
        panel.beginFrame();
        panel.save();
        panel.scale(static_cast<float>(scale), static_cast<float>(scale));
        panel.translate(-720, -116);
        sidebar(panel, game);
        panel.restore();
        panel.endFrame();
        if (!panel.isTextureValid()) throw std::runtime_error("Sidebar Canvas texture is unavailable");
        panelRevision_ = game.revision();
        ++cacheBuilds_;
    }
}

void Renderer::draw(wsc::Canvas& c, const Game& game, float width, float height, double now) {
    if (!background_.isTextureValid()) throw std::runtime_error("Call Renderer::prepare before beginFrame");
    const auto view = layout::Viewport::fit(width, height);
    const auto& match = game.match();
    const auto& p = match.position();
    const float dt = lastDraw_ < 0 ? 1.0f / 60 : motion::unit(now - lastDraw_);
    lastDraw_ = now;
    const float blend = 1 - std::exp(-dt * 18);
    if (lastSelection_ != game.selected()) { lastSelection_ = game.selected(); selectedAt_ = now; }
    const auto& transition = game.transition();
    const auto sample = transition.sample(now);
    const bool moving = transition.kind != TransitionKind::Opening && sample.active;
    const MotionStep* step = moving ? &transition.steps[sample.step] : nullptr;
    const auto& shown = step ? step->after : p;
    c.drawColor(Color(20, 16, 13));
    c.save();
    c.translate(view.x, view.y);
    c.scale(view.scale, view.scale);
    c.drawImage(background_, RectF(0, 0, layout::Width, layout::Height), fill(Color::WHITE));
    auto xy = [](int s) { return std::pair<float, float>{layout::BoardX + fileOf(s) * layout::Cell, layout::BoardY + rankOf(s) * layout::Cell}; };
    if (!match.moves().empty()) for (int s : {match.moves().back().from, match.moves().back().to}) {
        auto [x, y] = xy(s);
        c.drawRoundRect(RectF(x - 29, y - 29, 58, 58), 8, stroke(Gold, 2));
    }
    auto sprite = [&](RectF src, RectF dst, float alpha = 1) {
        auto paint = fill(Color::WHITE); paint.setAlpha(alpha);
        c.drawImage(effects_, src, dst, paint);
    };
    auto halo = [&](float x, float y, float radius, float alpha) {
        sprite(RectF(0, 0, 256, 256), RectF(x - radius, y - radius, radius * 2, radius * 2), alpha);
    };
    auto drawPiece = [&](Piece piece, float x, float y, float alpha = 1) {
        const float sx = (static_cast<int>(piece.kind) - 1) * 128.0f;
        const float sy = piece.side == Side::White ? 0.0f : 128.0f;
        auto paint = fill(Color::WHITE); paint.setAlpha(alpha);
        c.drawImage(pieces_, RectF(sx, sy, 128, 128),
                    RectF(x - 32, y - 30, 64, 64), paint);
    };
    const int hoverSquare = !game.thinking() && !game.boardBusy() && !game.dialog()
                          ? layout::hitSquare(pointerX_, pointerY_) : -1;
    const float selectionIn = motion::smooth(motion::unit((now - selectedAt_) / 0.16));
    for (int s = 0; s < 64; ++s) {
        const Piece piece = shown.board[s];
        const float target = s == game.selected() ? 1.0f : s == hoverSquare && piece.side == Side::White ? 0.28f : 0;
        focus_[s] += (target - focus_[s]) * blend;
        if (focus_[s] < 0.002f) focus_[s] = 0;
        if (!piece || (step && (s == step->move.to || s == step->companion.to || (step->reverse && s == step->victim)))) continue;
        auto [x, y] = xy(s);
        float alpha = 1;
        if (transition.kind == TransitionKind::Opening && transition.active(now)) {
            const double delay = (rankOf(s) + std::abs(fileOf(s) - 4)) * 0.015;
            const float entered = motion::smooth(motion::unit((now - transition.started - delay) / 0.34));
            alpha = entered;
        }
        drawPiece(piece, x, y, alpha);
        if (focus_[s] > 0 && s != game.selected()) halo(x, y, 40, focus_[s]);
    }
    if (step) {
        const auto from = step->startFile >= 0
            ? std::pair<float, float>{layout::BoardX + step->startFile * layout::Cell, layout::BoardY + step->startRank * layout::Cell}
            : xy(step->move.from);
        const auto to = xy(step->move.to);
        const Piece captured = step->captured;
        if (captured) {
            const float alpha = step->reverse ? step->restoredAlpha + (1 - step->restoredAlpha) * sample.travel : 1 - sample.landing;
            const auto at = xy(step->victim);
            drawPiece(captured, at.first, at.second, alpha);
        }
        const float x = from.first + (to.first - from.first) * sample.travel;
        const float y = from.second + (to.second - from.second) * sample.travel;
        const Piece before = step->before.board[step->move.from], after = step->after.board[step->move.to];
        if (before.kind != after.kind && sample.travel >= 1) {
            drawPiece(before, x, y, 1 - sample.landing);
            drawPiece(after, x, y, sample.landing);
        } else drawPiece(before, x, y);
        if (step->companion.valid()) {
            auto rookFrom = xy(step->companion.from);
            if (step->companionStartFile >= 0) rookFrom.first = layout::BoardX + step->companionStartFile * layout::Cell;
            const auto rookTo = xy(step->companion.to);
            drawPiece(step->before.board[step->companion.from], rookFrom.first + (rookTo.first - rookFrom.first) * sample.travel,
                      rookFrom.second + (rookTo.second - rookFrom.second) * sample.travel);
        }
        if (sample.travel >= 1) halo(x, y, 41, (1 - sample.landing) * 0.65f);
    }
    if (game.selected() >= 0) {
        auto [x, y] = xy(game.selected());
        halo(x, y, 40, selectionIn * 0.85f);
    }
    for (int s : game.destinations()) {
        auto [x, y] = xy(s);
        if (p.board[s] || (game.selected() >= 0 && capturedSquare(p, {game.selected(), s}) >= 0))
            c.drawCircle(x, y, 29, stroke(Color(180, 49, 28, static_cast<int>(230 * selectionIn)), 2.8f));
        else c.drawCircle(x, y, 5, fill(Color(51, 29, 15, static_cast<int>(180 * selectionIn))));
    }
    if (match.checked() && !moving) for (int s = 0; s < 64; ++s)
        if (p.board[s].kind == Kind::King && p.board[s].side == p.turn) {
            auto [x, y] = xy(s);
            halo(x, y, 43, 0.7f + 0.2f * std::sin(static_cast<float>(now - transition.started) * 5));
        }
    if (game.rejectedSquare() >= 0 && now - game.rejectionTime() < 0.3) {
        const auto at = xy(game.rejectedSquare());
        const float t = motion::unit((now - game.rejectionTime()) / 0.3);
        c.drawCircle(at.first, at.second, 20, stroke(Color(190, 47, 25, static_cast<int>((1 - t) * 220)), 2));
    }
    const double afterMove = now - transition.started - transition.duration();
    int banner = -1;
    float bannerAlpha = 0;
    if (!moving && afterMove >= 0) {
        if (match.outcome() != Outcome::Playing) {
            banner = match.outcome() == Outcome::WhiteWins ? 1 : match.outcome() == Outcome::BlackWins ? 2 : 3;
            bannerAlpha = motion::smooth(motion::unit(afterMove / 0.25));
        } else if (match.checked() && afterMove < 1.15) {
            banner = 0; bannerAlpha = std::min(motion::unit(afterMove / 0.18), motion::unit((1.15 - afterMove) / 0.25));
        }
    }
    if (banner >= 0) sprite(RectF((banner % 3) * 512.0f, 256 + (banner / 3) * 224.0f, 512, 224),
                            RectF(222, 382, 256, 112), bannerAlpha);
    c.drawImage(*sidebarScratch_, RectF(720, 116, 338, 634), fill(Color::WHITE));
    if (game.thinking() && !game.dialog()) for (int i = 0; i < 3; ++i) {
        const float pulse = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(static_cast<float>(now - transition.started) * 5 - i * 0.9f));
        c.drawCircle(869.0f + i * 20, 265.0f, 2.2f, fill(Color(211, 171, 101, static_cast<int>(210 * pulse))));
    }
    const int control = game.controlAt(pointerX_, pointerY_);
    for (int i = 0; i < 12; ++i) {
        hover_[i] += ((control == i ? 1.0f : 0.0f) - hover_[i]) * blend;
        if (hover_[i] < 0.005f) continue;
        const Rect r = i < 3 ? layout::DifficultyButtons[i] : i == 3 ? layout::Undo : i == 4 ? layout::Restart
                      : i == 5 ? layout::Confirm : i == 6 ? layout::Cancel : i < 11 ? layout::Promotion[i - 7] : layout::Claim;
        c.drawRoundRect(RectF(r.x, r.y, r.w, r.h), 6,
                        fill(Color(pressed_ && control == i ? 0 : 255, pressed_ && control == i ? 0 : 239,
                                   pressed_ && control == i ? 0 : 205, static_cast<int>(hover_[i] * (pressed_ ? 55 : 42)))));
        c.drawRoundRect(RectF(r.x + 1, r.y + 1, r.w - 2, r.h - 2), 6,
                        stroke(Color(255, 221, 153, static_cast<int>(hover_[i] * 150)), 1.2f));
    }
    c.restore();
}
} // namespace chess
