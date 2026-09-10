#pragma once
#include "Game.h"
#include <wsc/Canvas.h>
#include <wsc/Image.h>
#include <array>

namespace chess {
// Board and 12 piece faces are Canvas-generated Images. The sidebar is a retained
// Canvas texture, refreshed only when match/dialog state changes, without readback.
class Renderer {
public:
    void prepare(wsc::Canvas& canvas, const Game& game, float pixelScale);
    void draw(wsc::Canvas& canvas, const Game& game, float width, float height, double now);
    std::size_t cacheBuilds() const { return cacheBuilds_; }
    void pointerMove(float x, float y) { pointerX_ = x; pointerY_ = y; }
    void pointerPressed(bool pressed) { pressed_ = pressed; }
private:
    wsc::Image background_, pieces_, effects_;
    std::unique_ptr<wsc::Canvas> sidebarScratch_;
    int cacheScale_ = 0;
    std::uint64_t panelRevision_ = 0;
    std::size_t cacheBuilds_ = 0;
    std::array<float, 64> focus_{};
    std::array<float, 12> hover_{};
    float pointerX_ = -100, pointerY_ = -100;
    bool pressed_ = false;
    int lastSelection_ = -1;
    double selectedAt_ = 0, lastDraw_ = -1;
};
} // namespace chess
