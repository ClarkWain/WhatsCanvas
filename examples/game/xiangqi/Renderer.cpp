#include "Renderer.h"
#include <wsc/FontSystem.h>
#include <wsc/Path.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <filesystem>
#include <stdexcept>

namespace xiangqi {
namespace {
using namespace wsc;
// Walnut, brass, boxwood and cinnabar: a small shared material palette.
const Color Ink(43, 29, 21), Muted(177, 155, 119), Red(144, 37, 24), Gold(211, 171, 101);
const Color Ivory(245, 229, 196), Panel(42, 30, 23);
Paint fill(Color color) { Paint p; p.setColor(color); return p; }
Paint stroke(Color color, float width) {
    auto p = fill(color); p.setStyle(Paint::Style::STROKE); p.setStrokeWidth(width); return p;
}
void text(Canvas& c, const std::string& label, float x, float y, float size, Color color = Ivory,
          bool center = false, bool carved = false) {
    Paint p = fill(color);
    p.setTextSize(size);
    p.setFontFamily(carved ? "Xiangqi Carved" : "Xiangqi UI");
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
    round(c, {r.x, r.y + 3, r.w, r.h}, 6, Color(10, 6, 3, 100));
    Paint surface;
    surface.setLinearGradient(r.x, r.y, r.x, r.y + r.h,
        active ? Color(227, 190, 123) : Color(65, 47, 34),
        active ? Color(182, 137, 70) : Color(46, 33, 25));
    c.drawRoundRect(RectF(r.x, r.y, r.w, r.h), 6, surface);
    c.drawRoundRect(RectF(r.x + 0.5f, r.y + 0.5f, r.w - 1, r.h - 1), 6,
                    stroke(active ? Color(245, 216, 163, 160) : Color(161, 126, 76, 80), 1));
    text(c, label, r.x + r.w / 2, r.y + r.h / 2, 18,
         !enabled ? Color(130, 111, 87) : active ? Ink : Ivory, true);
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
    registerFirst("Xiangqi UI", {"C:/Windows/Fonts/msjh.ttc", "/System/Library/Fonts/PingFang.ttc",
                               "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"});
    registerFirst("Xiangqi Carved", {"C:/Windows/Fonts/simkai.ttf", "/System/Library/Fonts/Supplemental/Kaiti.ttc",
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
    room.setLinearGradient(0, 0, 1120, 820, Color(47, 34, 26), Color(20, 16, 13));
    c.drawRect(RectF(0, 0, 1120, 820), room);
    // Subtle table grain is generated deterministically, never loaded from a bitmap.
    for (int y = 0; y < 820; y += 4) {
        Path grain; grain.moveTo(0, static_cast<float>(y));
        grain.cubicTo(320, y + std::sin(y * 0.1f) * 8, 710, y - 8.0f, 1120, y + 3.0f);
        c.drawPath(grain, stroke(Color(185, 125, 70, y % 12 == 0 ? 12 : 5), 0.6f));
    }
    round(c, {38, 28, 46, 48}, 4, Color(121, 35, 24));
    c.drawRoundRect(RectF(42, 32, 38, 40), 2, stroke(Color(233, 180, 119, 140), 1));
    text(c, "弈", 61, 51, 33, Ivory, true, true);
    text(c, "中國象棋", 107, 48, 38, Ivory, false, true);
    text(c, "楚河漢界  ·  方寸之間", 110, 82, 13, Muted);
    text(c, "人 機 對 弈", 934, 44, 17, Gold);
    text(c, "XIANGQI  /  WHATSCANVAS", 896, 72, 10, Muted);

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
    const Color grid(61, 34, 18);
    // A fine lower highlight makes the recessed playing lines read as engraving.
    for (int r = 0; r < 10; ++r)
        line(c, x, y + r * s + 1, x + 8 * s, y + r * s + 1, Color(230, 177, 107, 100), 1);
    for (int r = 0; r < 10; ++r) line(c, x, y + r * s, x + 8 * s, y + r * s, grid, r == 0 || r == 9 ? 1.8f : 1.1f);
    for (int f = 0; f < 9; ++f) {
        line(c, x + f * s, y, x + f * s, y + 4 * s, grid, 1.1f);
        line(c, x + f * s, y + 5 * s, x + f * s, y + 9 * s, grid, 1.1f);
        if (f == 0 || f == 8) line(c, x + f * s, y + 4 * s, x + f * s, y + 5 * s, grid, 1.8f);
    }
    for (int r : {0, 7}) {
        line(c, x + 3 * s, y + r * s, x + 5 * s, y + (r + 2) * s, grid);
        line(c, x + 5 * s, y + r * s, x + 3 * s, y + (r + 2) * s, grid);
    }
    for (int r : {2, 3, 6, 7}) for (int f = 0; f < 9; ++f) {
        if ((r == 2 || r == 7) ? (f != 1 && f != 7) : f % 2 != 0) continue;
        const float cx = x + f * s, cy = y + r * s;
        for (int dx : {-1, 1}) for (int dy : {-1, 1}) {
            if (f + dx < 0 || f + dx > 8) continue;
            line(c, cx + dx * 5, cy + dy * 12, cx + dx * 5, cy + dy * 5, grid);
            line(c, cx + dx * 5, cy + dy * 5, cx + dx * 12, cy + dy * 5, grid);
        }
    }
    text(c, "楚 河", x + 2 * s, y + 4.5f * s + 1, 32, Color(216, 165, 98), true, true);
    text(c, "漢 界", x + 6 * s, y + 4.5f * s + 1, 32, Color(216, 165, 98), true, true);
    text(c, "楚 河", x + 2 * s, y + 4.5f * s, 32, grid, true, true);
    text(c, "漢 界", x + 6 * s, y + 4.5f * s, 32, grid, true, true);
    for (int f = 0; f < 9; ++f)
        text(c, std::string(1, static_cast<char>('A' + f)), x + f * s, 122, 9, Color(238, 194, 125), true);
    for (int r = 0; r < 10; ++r)
        text(c, std::to_string(r + 1), 60, y + r * s, 9, Color(238, 194, 125), true);
    c.drawCircle(302, 797, 4, fill(Color(183, 67, 43)));
    text(c, "你執紅先行", 319, 797, 14, Ivory);
    round(c, {708, 106, 362, 662}, 12, Color(0, 0, 0, 65));
    round(c, {708, 102, 362, 662}, 12, Panel);
    c.drawRoundRect(RectF(708.5f, 102.5f, 361, 661), 12, stroke(Color(156, 113, 68, 75), 1));
    text(c, "靜觀棋勢，落子有度。", 889, 797, 14, Muted, true, true);
}
void atlas(Canvas& c) {
    // Logical 64px cells have transparent padding; all 14 sprites share one texture.
    for (int side = 0; side < 2; ++side) for (int kind = 1; kind <= 7; ++kind) {
        const float x = (kind - 1) * 64.0f + 32, y = side * 64.0f + 30;
        c.drawCircle(x + 1, y + 5, 28, fill(Color(24, 10, 3, 35)));
        c.drawCircle(x + 1, y + 4, 27, fill(Color(24, 10, 3, 65)));
        c.drawCircle(x, y + 3, 26.5f, fill(Color(88, 47, 21)));
        c.drawCircle(x, y + 1.5f, 26.5f, fill(Color(168, 112, 54)));
        Paint face;
        face.setLinearGradient(x - 18, y - 26, x + 14, y + 26, Color(255, 229, 173), Color(203, 153, 84));
        c.drawCircle(x, y, 26, face);
        c.drawCircle(x, y - 0.4f, 25.2f, stroke(Color(255, 240, 194, 200), 1.1f));
        c.drawCircle(x, y + 0.5f, 22.4f, stroke(Color(255, 231, 180, 190), 1.4f));
        c.drawCircle(x, y - 0.2f, 22.4f, stroke(Color(109, 64, 27, 180), 1));
        const Color color = side == 0 ? Red : Ink;
        const char* label = pieceName({static_cast<Kind>(kind), side == 0 ? Side::Red : Side::Black});
        text(c, label, x + 0.4f, y + 0.5f, 41, Color(255, 233, 187), true, true);
        text(c, label, x, y - 0.5f, 41, color, true, true);
    }
}
void effects(Canvas& c) {
    // Shared Image sprites for all per-frame effects; no glyph shaping, blur
    // filters or concentric geometry is submitted during the animation itself.
    for (int r = 50; r >= 44; --r)
        c.drawCircle(64, 64, static_cast<float>(r), stroke(Color(255, 212, 121, (r == 46 ? 210 : 15)), 2));
    const char* labels[] = {"將 軍", "紅 方 勝", "黑 方 勝", "和 棋", "悔 棋"};
    for (int i = 0; i < 5; ++i) {
        const float x = (i % 3) * 256.0f, y = 128 + (i / 3) * 112.0f;
        round(c, {x + 5, y + 8, 246, 96}, 10, Color(0, 0, 0, 70));
        round(c, {x + 8, y + 4, 240, 92}, 8, Color(45, 25, 17, 245));
        c.drawRoundRect(RectF(x + 13, y + 9, 230, 82), 5, stroke(Gold, 1.2f));
        text(c, labels[i], x + 128, y + 47, 39, i == 0 ? Color(247, 145, 91) : Ivory, true, true);
    }
}
void sidebar(Canvas& c, const Game& game) {
    const auto& match = game.match();
    c.drawRect(RectF(720, 116, 338, 634), fill(Panel));
    text(c, "本局對弈", 736, 145, 27, Ivory, false, true);
    c.drawCircle(742, 184, 4.5f, fill(Color(188, 74, 48)));
    text(c, "紅方 · 你", 756, 184, 15, Ivory);
    c.drawCircle(949, 184, 4.5f, fill(Color(222, 197, 152)));
    text(c, "黑方 · AI", 963, 184, 15, Ivory);
    round(c, {732, 207, 314, 46}, 5, match.checked() ? Color(83, 37, 26) : Color(55, 42, 28));
    c.drawRect(RectF(732, 213, 3, 34), fill(match.checked() ? Color(211, 90, 56) : Gold));
    text(c, game.status(), 889, 230, 20, match.checked() ? Color(255, 190, 150) : Gold, true);
    for (int i = 0; i < 3; ++i) button(c, layout::DifficultyButtons[i], difficultyName(static_cast<Difficulty>(i)), static_cast<int>(match.difficulty()) == i);
    const char* descriptions[] = {"隨手過招，從容入門。", "多想一步，穩中求勝。", "步步推演，落子無悔。"};
    text(c, descriptions[static_cast<int>(match.difficulty())], 889, 352, 16, Muted, true);
    line(c, 736, 380, 1042, 380, Color(160, 124, 76, 70));
    text(c, "回 合", 736, 409, 13, Muted);
    text(c, std::to_string(match.moves().size() / 2 + 1), 736, 447, 38, Ivory);
    line(c, 879, 405, 879, 462, Color(160, 124, 76, 60));
    text(c, "最 後 一 步", 908, 409, 13, Muted);
    if (!match.moves().empty()) {
        const Move last = match.moves().back();
        const Piece piece = match.position().board[last.to];
        const std::string from = std::string(1, static_cast<char>('A' + fileOf(last.from))) + std::to_string(rankOf(last.from) + 1);
        const std::string to = std::string(1, static_cast<char>('A' + fileOf(last.to))) + std::to_string(rankOf(last.to) + 1);
        text(c, std::string(piece.side == Side::Red ? "紅" : "黑") + pieceName(piece), 908, 444, 21, Ivory);
        text(c, from + " → " + to, 971, 446, 13, Gold);
    } else text(c, "尚未落子", 908, 446, 19, Muted);
    line(c, 736, 481, 1042, 481, Color(160, 124, 76, 70));
    text(c, "點選紅棋，再點提示位置落子。", 736, 513, 16, Ivory);
    text(c, "圓點可走  ·  圓環可吃  ·  金框為上步", 736, 541, 13, Muted);
    button(c, layout::Undo, match.difficulty() == Difficulty::Hard ? "不可悔棋" : "悔 棋    U", false, game.canUndo());
    button(c, layout::Restart, "重 新 開 局    N", true);
    if (game.dialog()) {
        text(c, std::string("以「") + difficultyName(game.pendingDifficulty()) + "」難度重新開局？", 889, 687, 14, Gold, true);
        button(c, layout::Confirm, "確認重開", true);
        button(c, layout::Cancel, "繼續對局");
    } else {
        text(c, "1 / 2 / 3 選擇難度    Esc 取消選取", 889, 708, 12, Muted, true);
        text(c, "困斃判負 · 三次重複局面判和", 889, 736, 12, Muted, true);
    }
}
} // namespace

void Renderer::prepare(wsc::Canvas& canvas, const Game& game, float pixelScale) {
    if (!effects_.isTextureValid()) { bake(canvas, effects_, 768, 352, 2, effects); ++cacheBuilds_; }
    const int scale = pixelScale > 1.15f ? 2 : 1;
    if (scale != cacheScale_) {
        bake(canvas, background_, 1120, 820, scale, background);
        bake(canvas, pieces_, 448, 128, 2, atlas);
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
        const float sy = piece.side == Side::Red ? 0.0f : 128.0f;
        auto paint = fill(Color::WHITE); paint.setAlpha(alpha);
        c.drawImage(pieces_, RectF(sx, sy, 128, 128),
                    RectF(x - 32, y - 30, 64, 64), paint);
    };
    const int hoverSquare = !game.thinking() && !game.boardBusy() && !game.dialog()
                          ? layout::hitSquare(pointerX_, pointerY_) : -1;
    const float selectionIn = motion::smooth(motion::unit((now - selectedAt_) / 0.16));
    for (int s = 0; s < 90; ++s) {
        const Piece piece = shown.board[s];
        const float target = s == game.selected() ? 1.0f : s == hoverSquare && piece.side == Side::Red ? 0.28f : 0;
        focus_[s] += (target - focus_[s]) * blend;
        if (focus_[s] < 0.002f) focus_[s] = 0;
        if (!piece || (step && (s == step->move.to || (step->reverse && s == step->move.from)))) continue;
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
        const Piece captured = step->reverse ? step->after.board[step->move.from] : step->before.board[step->move.to];
        if (captured) {
            const float alpha = step->reverse ? step->restoredAlpha + (1 - step->restoredAlpha) * sample.travel : 1 - sample.landing;
            const auto at = step->reverse ? xy(step->move.from) : to;
            drawPiece(captured, at.first, at.second, alpha);
        }
        const float x = from.first + (to.first - from.first) * sample.travel;
        const float y = from.second + (to.second - from.second) * sample.travel;
        drawPiece(step->before.board[step->move.from], x, y);
        if (sample.travel >= 1) halo(x, y, 41, (1 - sample.landing) * 0.65f);
    }
    if (game.selected() >= 0) {
        auto [x, y] = xy(game.selected());
        halo(x, y, 40, selectionIn * 0.85f);
    }
    for (int s : game.destinations()) {
        auto [x, y] = xy(s);
        if (p.board[s]) c.drawCircle(x, y, 29, stroke(Color(180, 49, 28, static_cast<int>(230 * selectionIn)), 2.8f));
        else c.drawCircle(x, y, 5, fill(Color(51, 29, 15, static_cast<int>(180 * selectionIn))));
    }
    if (match.checked() && !moving) for (int s = 0; s < 90; ++s)
        if (p.board[s].kind == Kind::General && p.board[s].side == p.turn) {
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
            banner = match.outcome() == Outcome::RedWins ? 1 : match.outcome() == Outcome::BlackWins ? 2 : 3;
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
    for (int i = 0; i < 7; ++i) {
        hover_[i] += ((control == i ? 1.0f : 0.0f) - hover_[i]) * blend;
        if (hover_[i] < 0.005f) continue;
        const Rect r = i < 3 ? layout::DifficultyButtons[i] : i == 3 ? layout::Undo : i == 4 ? layout::Restart
                                                                             : i == 5 ? layout::Confirm : layout::Cancel;
        c.drawRoundRect(RectF(r.x, r.y, r.w, r.h), 6,
                        fill(Color(pressed_ && control == i ? 0 : 255, pressed_ && control == i ? 0 : 239,
                                   pressed_ && control == i ? 0 : 205, static_cast<int>(hover_[i] * (pressed_ ? 55 : 42)))));
        c.drawRoundRect(RectF(r.x + 1, r.y + 1, r.w - 2, r.h - 2), 6,
                        stroke(Color(255, 221, 153, static_cast<int>(hover_[i] * 150)), 1.2f));
    }
    c.restore();
}
} // namespace xiangqi
