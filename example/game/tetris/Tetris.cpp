#include <iostream>
#include <vector>
#include <array>
#include <algorithm>
#include <string>
#include <cstdlib>
#include <cmath>
#include <thread>
#include <chrono>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "canvas/Canvas.h"
#include "canvas/Paint.h"
#include "canvas/base.h"

constexpr int COLS = 10;
constexpr int ROWS = 20;
constexpr int CELL = 30;
constexpr int GRID_X = 42;
constexpr int GRID_Y = 40;
constexpr int SIDE_X = GRID_X + COLS * CELL + 70;
constexpr int DESIGN_W = SIDE_X + 220;
constexpr int DESIGN_H = GRID_Y + ROWS * CELL + 30;
constexpr int DROP_INTERVAL_MS = 800;

enum PieceType : int { PIECE_I=0, PIECE_O, PIECE_T, PIECE_S, PIECE_Z, PIECE_J, PIECE_L, PIECE_COUNT };

static const Color kColors[PIECE_COUNT] = {
    Color(0, 240, 240),
    Color(240, 240, 0),
    Color(160, 0, 240),
    Color(0, 240, 0),
    Color(240, 0, 0),
    Color(0, 0, 240),
    Color(240, 160, 0),
};

struct Piece {
    PieceType type;
    int rotation;
    int x;
    int y;
};

static const std::array<Point, 4> kPieceData[PIECE_COUNT][4] = {{
    {Point(0,1),Point(1,1),Point(2,1),Point(3,1)},
    {Point(2,0),Point(2,1),Point(2,2),Point(2,3)},
    {Point(0,2),Point(1,2),Point(2,2),Point(3,2)},
    {Point(1,0),Point(1,1),Point(1,2),Point(1,3)},
},{
    {Point(1,0),Point(2,0),Point(1,1),Point(2,1)},
    {Point(1,0),Point(2,0),Point(1,1),Point(2,1)},
    {Point(1,0),Point(2,0),Point(1,1),Point(2,1)},
    {Point(1,0),Point(2,0),Point(1,1),Point(2,1)},
},{
    {Point(1,0),Point(0,1),Point(1,1),Point(2,1)},
    {Point(1,0),Point(1,1),Point(2,1),Point(1,2)},
    {Point(0,1),Point(1,1),Point(2,1),Point(1,2)},
    {Point(1,0),Point(0,1),Point(1,1),Point(1,2)},
},{
    {Point(1,0),Point(2,0),Point(0,1),Point(1,1)},
    {Point(1,0),Point(1,1),Point(2,1),Point(2,2)},
    {Point(1,1),Point(2,1),Point(0,2),Point(1,2)},
    {Point(0,0),Point(0,1),Point(1,1),Point(1,2)},
},{
    {Point(0,0),Point(1,0),Point(1,1),Point(2,1)},
    {Point(2,0),Point(1,1),Point(2,1),Point(1,2)},
    {Point(0,1),Point(1,1),Point(1,2),Point(2,2)},
    {Point(1,0),Point(0,1),Point(1,1),Point(0,2)},
},{
    {Point(0,0),Point(0,1),Point(1,1),Point(2,1)},
    {Point(1,0),Point(2,0),Point(1,1),Point(1,2)},
    {Point(0,1),Point(1,1),Point(2,1),Point(2,2)},
    {Point(1,0),Point(1,1),Point(0,2),Point(1,2)},
},{
    {Point(2,0),Point(0,1),Point(1,1),Point(2,1)},
    {Point(1,0),Point(1,1),Point(1,2),Point(2,2)},
    {Point(0,1),Point(1,1),Point(2,1),Point(0,2)},
    {Point(0,0),Point(1,0),Point(1,1),Point(1,2)},
}};

class TetrisGame {
public:
    TetrisGame() : board_(ROWS, std::vector<int>(COLS, 0)) {
        std::srand(static_cast<unsigned>(std::time(nullptr)));
        bag_ = {PIECE_I,PIECE_O,PIECE_T,PIECE_S,PIECE_Z,PIECE_J,PIECE_L};
        shuffleBag();
        nextType_ = drawFromBag();
        spawnPiece();
    }

    void update(float dt) {
        updatePerformanceStats(dt);
        if (state_ != PLAYING) return;
        dropTimer_ += dt;
        float targetSec = getDropInterval() / 1000.0f;
        if (dropTimer_ >= targetSec) {
            dropTimer_ -= targetSec;
            if (!movePiece(0, 1)) {
                lockPiece();
            }
        }
    }

    void moveLeft() {
        if (state_ != PLAYING) return;
        movePiece(-1, 0);
    }

    void moveRight() {
        if (state_ != PLAYING) return;
        movePiece(1, 0);
    }

    void hardDrop() {
        if (state_ != PLAYING) return;
        int cells = 0;
        while (movePiece(0, 1)) cells++;
        score_ += cells * 2;
        lockPiece();
    }

    void rotateCW() {
        if (state_ != PLAYING) return;
        int newRot = (current_.rotation + 1) % 4;
        if (tryRotation(newRot)) {
            current_.rotation = newRot;
        }
    }

    void rotateCCW() {
        if (state_ != PLAYING) return;
        int newRot = (current_.rotation + 3) % 4;
        if (tryRotation(newRot)) {
            current_.rotation = newRot;
        }
    }

    void togglePause() {
        if (state_ == PLAYING) state_ = PAUSED;
        else if (state_ == PAUSED) state_ = PLAYING;
    }

    void restart() {
        for (auto& row : board_) std::fill(row.begin(), row.end(), 0);
        score_ = 0;
        lines_ = 0;
        level_ = 1;
        state_ = PLAYING;
        dropTimer_ = 0;
        bag_ = {PIECE_I,PIECE_O,PIECE_T,PIECE_S,PIECE_Z,PIECE_J,PIECE_L};
        shuffleBag();
        nextType_ = drawFromBag();
        spawnPiece();
    }

    void render(Canvas& canvas, int windowW, int windowH) {
        drawBackground(canvas);

        float scale = std::min(
            static_cast<float>(windowW) / DESIGN_W,
            static_cast<float>(windowH) / DESIGN_H
        );
        float offsetX = (windowW - DESIGN_W * scale) * 0.5f;
        float offsetY = (windowH - DESIGN_H * scale) * 0.5f;

        canvas.save();
        canvas.translate(offsetX, offsetY);
        canvas.scale(scale, scale);

        drawBoard(canvas);
        drawGhostPiece(canvas);
        drawCurrentPiece(canvas);
        drawSidebar(canvas);

        if (state_ == GAME_OVER) drawOverlay(canvas, "GAME OVER", "Press R to restart");
        else if (state_ == PAUSED) drawOverlay(canvas, "PAUSED", "Press P to resume");

        canvas.restore();
    }

    bool isGameOver() const { return state_ == GAME_OVER; }
    int getScore() const { return score_; }

private:
    enum State { PLAYING, PAUSED, GAME_OVER };
    std::vector<std::vector<int>> board_;
    Piece current_;
    PieceType nextType_;
    State state_ = PLAYING;
    int score_ = 0;
    int lines_ = 0;
    int level_ = 1;
    float dropTimer_ = 0;
    float displayedFps_ = 0.0f;
    float latestFrameMs_ = 0.0f;
    float fpsAccumulatedTime_ = 0.0f;
    int fpsAccumulatedFrames_ = 0;
    std::vector<PieceType> bag_;

    static void applyGameFont(Paint& paint) {
        paint.setFont("Consolas");
    }

    void updatePerformanceStats(float dt) {
        if (!std::isfinite(dt) || dt <= 0.0f) return;

        const float clampedDt = std::min(dt, 0.25f);
        latestFrameMs_ = clampedDt * 1000.0f;
        fpsAccumulatedTime_ += clampedDt;
        ++fpsAccumulatedFrames_;

        if (fpsAccumulatedTime_ >= 0.25f) {
            displayedFps_ = static_cast<float>(fpsAccumulatedFrames_) / fpsAccumulatedTime_;
            fpsAccumulatedTime_ = 0.0f;
            fpsAccumulatedFrames_ = 0;
        }
    }

    int getDropInterval() const {
        return std::max(50, DROP_INTERVAL_MS - (level_ - 1) * 75);
    }

    RectF boardPanelRect() const {
        return RectF(GRID_X - 18.0f, GRID_Y - 18.0f, COLS * CELL + 36.0f, ROWS * CELL + 36.0f);
    }

    RectF playRect() const {
        return RectF(GRID_X - 6.0f, GRID_Y - 6.0f, COLS * CELL + 12.0f, ROWS * CELL + 12.0f);
    }

    RectF sidebarPanelRect() const {
        return RectF(SIDE_X - 18.0f, GRID_Y - 18.0f, DESIGN_W - SIDE_X - 24.0f, ROWS * CELL + 36.0f);
    }

    void shuffleBag() {
        for (size_t i = bag_.size() - 1; i > 0; --i) {
            size_t j = std::rand() % (i + 1);
            std::swap(bag_[i], bag_[j]);
        }
    }

    PieceType drawFromBag() {
        if (bag_.empty()) {
            bag_ = {PIECE_I,PIECE_O,PIECE_T,PIECE_S,PIECE_Z,PIECE_J,PIECE_L};
            shuffleBag();
        }
        PieceType t = bag_.back();
        bag_.pop_back();
        return t;
    }

    bool isValid(int x, int y, PieceType type, int rotation) const {
        for (const auto& b : kPieceData[type][rotation]) {
            int bx = x + b.getX();
            int by = y + b.getY();
            if (bx < 0 || bx >= COLS || by >= ROWS) return false;
            if (by >= 0 && board_[by][bx] != 0) return false;
        }
        return true;
    }

    bool movePiece(int dx, int dy) {
        if (isValid(current_.x + dx, current_.y + dy, current_.type, current_.rotation)) {
            current_.x += dx;
            current_.y += dy;
            return true;
        }
        return false;
    }

    bool tryRotation(int newRot) {
        if (isValid(current_.x, current_.y, current_.type, newRot)) return true;
        static const int kicksI[] = {-2,1, -1,1, -2,0, -1,0, 1,0, 2,0, 1,-1, 2,-1};
        static const int kicks[]  = {-1,0, -1,-1, 0,-1, 1,0, 1,-1, 0,1, -1,1, 1,1};
        const int* wk = (current_.type == PIECE_I) ? kicksI : kicks;
        for (int i = 0; i < 8; i += 2) {
            if (isValid(current_.x + wk[i], current_.y + wk[i+1], current_.type, newRot)) {
                current_.x += wk[i];
                current_.y += wk[i+1];
                return true;
            }
        }
        return false;
    }

    void spawnPiece() {
        current_.type = nextType_;
        current_.rotation = 0;
        current_.x = 3;
        current_.y = -1;
        nextType_ = drawFromBag();
        if (!isValid(current_.x, current_.y, current_.type, current_.rotation)) {
            state_ = GAME_OVER;
        }
    }

    void lockPiece() {
        for (const auto& b : kPieceData[current_.type][current_.rotation]) {
            int bx = current_.x + b.getX();
            int by = current_.y + b.getY();
            if (by >= 0 && by < ROWS && bx >= 0 && bx < COLS) {
                board_[by][bx] = static_cast<int>(current_.type) + 1;
            }
        }
        clearLines();
        spawnPiece();
    }

    void clearLines() {
        int cleared = 0;
        for (int row = ROWS - 1; row >= 0;) {
            bool full = true;
            for (int col = 0; col < COLS; ++col) {
                if (board_[row][col] == 0) { full = false; break; }
            }
            if (full) {
                for (int r = row; r > 0; --r) board_[r] = board_[r - 1];
                std::fill(board_[0].begin(), board_[0].end(), 0);
                cleared++;
            } else {
                row--;
            }
        }
        if (cleared > 0) {
            static const int lineScores[] = {0, 100, 300, 500, 800};
            int idx = (cleared > 4) ? 4 : cleared;
            score_ += lineScores[idx] * level_;
            lines_ += cleared;
            level_ = 1 + lines_ / 10;
        }
    }

    int getGhostY() const {
        int gy = current_.y;
        while (isValid(current_.x, gy + 1, current_.type, current_.rotation)) gy++;
        return gy;
    }

    void drawBackground(Canvas& canvas) {
        Paint backdrop;
        backdrop.setStyle(Paint::Style::FILL);
        backdrop.setLinearGradient(0.0f, 0.0f, 0.0f, static_cast<float>(DESIGN_H), {
            Paint::ColorStop(0.0f, Color(18, 28, 55)),
            Paint::ColorStop(0.55f, Color(12, 19, 38)),
            Paint::ColorStop(1.0f, Color(8, 11, 24))
        });
        canvas.drawRect(RectF(0.0f, 0.0f, static_cast<float>(DESIGN_W), static_cast<float>(DESIGN_H)), backdrop);

        Paint glow;
        glow.setStyle(Paint::Style::FILL);
        glow.setFillColor(Color(72, 112, 224, 24));
        canvas.drawCircle(PointF(116.0f, 104.0f), 92.0f, glow);
        glow.setFillColor(Color(117, 74, 214, 22));
        canvas.drawCircle(PointF(static_cast<float>(DESIGN_W) - 92.0f, 146.0f), 108.0f, glow);
        glow.setFillColor(Color(49, 177, 182, 18));
        canvas.drawCircle(PointF(static_cast<float>(DESIGN_W) - 78.0f, static_cast<float>(DESIGN_H) - 86.0f), 136.0f, glow);
    }

    void drawBlock(Canvas& canvas, float px, float py, float sz, const Color& c, float alpha) {
        int r = static_cast<int>(c.getR() * alpha);
        int g = static_cast<int>(c.getG() * alpha);
        int b = static_cast<int>(c.getB() * alpha);
        int a = static_cast<int>(255 * alpha);
        Color fc(r, g, b, a);
        Paint p;
        p.setStyle(Paint::Style::FILL);
        p.setFillColor(fc);
        canvas.drawRect(RectF(px + 2, py + 2, sz - 4, sz - 4), p);
        int ha = static_cast<int>(60 * alpha);
        int sa = static_cast<int>(100 * alpha);
        p.setFillColor(Color(255, 255, 255, ha));
        canvas.drawRect(RectF(px + 2, py + 2, sz - 4, 4), p);
        canvas.drawRect(RectF(px + 2, py + 4, 4, sz - 6), p);
        p.setFillColor(Color(0, 0, 0, sa));
        canvas.drawRect(RectF(px + 2, py + sz - 6, sz - 4, 4), p);
        canvas.drawRect(RectF(px + sz - 6, py + 4, 4, sz - 8), p);
    }

    void drawBoard(Canvas& canvas) {
        const RectF outerRect = boardPanelRect();
        Paint outerFill;
        outerFill.setStyle(Paint::Style::FILL);
        outerFill.setLinearGradient(outerRect.getX(), outerRect.getY(), outerRect.getX(), outerRect.getY() + outerRect.getHeight(), {
            Paint::ColorStop(0.0f, Color(24, 34, 60, 235)),
            Paint::ColorStop(1.0f, Color(13, 18, 33, 235))
        });
        canvas.drawRoundRect(outerRect, 24.0f, outerFill);

        Paint outerStroke;
        outerStroke.setStyle(Paint::Style::STROKE);
        outerStroke.setStrokeColor(Color(88, 116, 176, 180));
        outerStroke.setStrokeWidth(3.0f);
        canvas.drawRoundRect(outerRect, 24.0f, outerStroke);

        const RectF innerRect = playRect();
        Paint innerFill;
        innerFill.setStyle(Paint::Style::FILL);
        innerFill.setLinearGradient(innerRect.getX(), innerRect.getY(), innerRect.getX(), innerRect.getY() + innerRect.getHeight(), {
            Paint::ColorStop(0.0f, Color(10, 15, 29)),
            Paint::ColorStop(1.0f, Color(15, 18, 29))
        });
        canvas.drawRoundRect(innerRect, 20.0f, innerFill);

        Paint innerStroke;
        innerStroke.setStyle(Paint::Style::STROKE);
        innerStroke.setStrokeColor(Color(54, 76, 120));
        innerStroke.setStrokeWidth(2.0f);
        canvas.drawRoundRect(innerRect, 20.0f, innerStroke);

        Paint emptyCell;
        emptyCell.setStyle(Paint::Style::FILL);
        emptyCell.setFillColor(Color(26, 33, 49));

        for (int row = 0; row < ROWS; ++row) {
            for (int col = 0; col < COLS; ++col) {
                int v = board_[row][col];
                if (v != 0) {
                    drawBlock(canvas, GRID_X + col * CELL, GRID_Y + row * CELL, CELL, kColors[v - 1], 1.0f);
                } else {
                    canvas.drawRect(RectF(GRID_X + col * CELL + 1, GRID_Y + row * CELL + 1, CELL - 2, CELL - 2), emptyCell);
                }
            }
        }
    }

    void drawGhostPiece(Canvas& canvas) {
        if (state_ == GAME_OVER) return;
        int gy = getGhostY();
        if (gy == current_.y) return;
        for (const auto& b : kPieceData[current_.type][current_.rotation]) {
            int bx = current_.x + b.getX();
            int by = gy + b.getY();
            if (by >= 0 && by < ROWS && bx >= 0 && bx < COLS) {
                Color c = kColors[current_.type];
                float px = GRID_X + bx * CELL;
                float py = GRID_Y + by * CELL;
                Paint p;
                p.setStyle(Paint::Style::FILL);
                p.setFillColor(Color(
                    static_cast<int>(c.getR() * 0.3f),
                    static_cast<int>(c.getG() * 0.3f),
                    static_cast<int>(c.getB() * 0.3f),
                    180
                ));
                canvas.drawRect(RectF(px + 2, py + 2, CELL - 4, CELL - 4), p);
            }
        }
    }

    void drawCurrentPiece(Canvas& canvas) {
        if (state_ == GAME_OVER) return;
        Color c = kColors[current_.type];
        for (const auto& b : kPieceData[current_.type][current_.rotation]) {
            int bx = current_.x + b.getX();
            int by = current_.y + b.getY();
            if (by >= 0 && by < ROWS && bx >= 0 && bx < COLS) {
                drawBlock(canvas, GRID_X + bx * CELL, GRID_Y + by * CELL, CELL, c, 1.0f);
            }
        }
    }

    void drawSidebar(Canvas& canvas) {
        const RectF rect = sidebarPanelRect();

        Paint panelFill;
        panelFill.setStyle(Paint::Style::FILL);
        panelFill.setLinearGradient(rect.getX(), rect.getY(), rect.getX(), rect.getY() + rect.getHeight(), {
            Paint::ColorStop(0.0f, Color(22, 31, 56, 230)),
            Paint::ColorStop(1.0f, Color(13, 18, 33, 230))
        });
        canvas.drawRoundRect(rect, 24.0f, panelFill);

        Paint panelStroke;
        panelStroke.setStyle(Paint::Style::STROKE);
        panelStroke.setStrokeWidth(3.0f);
        panelStroke.setStrokeColor(Color(72, 99, 152, 170));
        canvas.drawRoundRect(rect, 24.0f, panelStroke);

        float sx = rect.getX() + 22.0f;
        float y = rect.getY() + 22.0f;
        const RectF contentRect(sx - 4.0f, rect.getY() + 10.0f, rect.getWidth() - 36.0f, rect.getHeight() - 20.0f);
        const int saveCount = canvas.save();
        canvas.clipRect(contentRect);

        Paint text;
        text.setStyle(Paint::Style::FILL);
        text.setColor(Color(200, 210, 240));
        applyGameFont(text);

        auto advanceY = [&](const std::string& content, float gap) {
            y += canvas.measureTextMetrics(content, text).height + gap;
        };

        Paint title = text;
        title.setColor(Color(238, 244, 255));
        title.setTextSize(28.0f);
        title.setLetterSpacing(1.0f);
        canvas.drawText("TETRIS", sx, y, title);
        y += canvas.measureTextMetrics("TETRIS", title).height + 8.0f;

        Paint subtitle = text;
        subtitle.setColor(Color(152, 173, 214));
        subtitle.setTextSize(12.0f);
        canvas.drawText("Stack rows. Clear lines.", sx, y, subtitle);
        y += canvas.measureTextMetrics("Stack rows. Clear lines.", subtitle).height + 18.0f;

        text.setTextSize(14.0f);
        text.setColor(Color(200, 210, 240));
        Paint value = text;
        value.setColor(Color(255, 255, 255));
        value.setTextSize(24.0f);

        auto drawMetric = [&](const std::string& heading, const std::string& content, float gap) {
            canvas.drawText(heading, sx, y, text);
            y += canvas.measureTextMetrics(heading, text).height + 6.0f;
            canvas.drawText(content, sx, y, value);
            y += canvas.measureTextMetrics(content, value).height + gap;
        };

        drawMetric("SCORE", std::to_string(score_), 10.0f);
        drawMetric("LEVEL", std::to_string(level_), 10.0f);
        drawMetric("LINES", std::to_string(lines_), 14.0f);

        Paint perfValue = text;
        perfValue.setColor(Color(244, 248, 255));
        perfValue.setTextSize(16.0f);
        const int fps = std::max(0, static_cast<int>(std::lround(displayedFps_)));
        const int frameMs = std::max(0, static_cast<int>(std::lround(latestFrameMs_)));
        canvas.drawText("PERF", sx, y, text);
        y += canvas.measureTextMetrics("PERF", text).height + 6.0f;
        const std::string perfText = std::to_string(fps) + " FPS / " + std::to_string(frameMs) + " ms";
        canvas.drawText(perfText, sx, y, perfValue);
        y += canvas.measureTextMetrics(perfText, perfValue).height + 16.0f;

        canvas.drawText("NEXT", sx, y, text);
        advanceY("NEXT", 4.0f);

        RectF previewBox(sx, y, rect.getWidth() - 44.0f, 78.0f);
        Paint previewFill;
        previewFill.setStyle(Paint::Style::FILL);
        previewFill.setFillColor(Color(14, 20, 35, 210));
        canvas.drawRoundRect(previewBox, 18.0f, previewFill);

        Paint previewStroke;
        previewStroke.setStyle(Paint::Style::STROKE);
        previewStroke.setStrokeWidth(2.0f);
        previewStroke.setStrokeColor(Color(94, 115, 160));
        canvas.drawRoundRect(previewBox, 18.0f, previewStroke);

        const auto& previewData = kPieceData[nextType_][0];
        int minX = previewData[0].getX();
        int maxX = previewData[0].getX();
        int minY = previewData[0].getY();
        int maxY = previewData[0].getY();
        for (const auto& block : previewData) {
            minX = std::min(minX, block.getX());
            maxX = std::max(maxX, block.getX());
            minY = std::min(minY, block.getY());
            maxY = std::max(maxY, block.getY());
        }

        const float previewCell = 20.0f;
        const float previewWidth = (maxX - minX + 1) * previewCell;
        const float previewHeight = (maxY - minY + 1) * previewCell;
        const float previewOriginX = previewBox.getX() + (previewBox.getWidth() - previewWidth) * 0.5f;
        const float previewOriginY = previewBox.getY() + (previewBox.getHeight() - previewHeight) * 0.5f;
        for (const auto& block : previewData) {
            drawBlock(canvas,
                previewOriginX + (block.getX() - minX) * previewCell,
                previewOriginY + (block.getY() - minY) * previewCell,
                previewCell,
                kColors[nextType_],
                1.0f);
        }
            y += previewBox.getHeight() + 16.0f;

        text.setColor(Color(120, 130, 160));
            text.setTextSize(10.0f);
        canvas.drawText("Controls", sx, y, text);
        advanceY("Controls", 6.0f);
        canvas.drawText("Left/Right Move", sx, y, text);
        advanceY("Left/Right Move", 2.0f);
        canvas.drawText("Down Hard Drop", sx, y, text);
        advanceY("Down Hard Drop", 2.0f);
        canvas.drawText("Up RotCW  Z RotCCW", sx, y, text);
        advanceY("Up RotCW  Z RotCCW", 2.0f);
        canvas.drawText("P Pause  R Restart", sx, y, text);

            canvas.restoreToCount(saveCount);
    }

    void drawOverlay(Canvas& canvas, const std::string& title, const std::string& sub) {
        const RectF rect = playRect();
        Paint ov;
        ov.setStyle(Paint::Style::FILL);
        ov.setFillColor(Color(0, 0, 0, 180));
        canvas.drawRoundRect(rect, 20.0f, ov);

        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeColor(Color(100, 120, 180));
        border.setStrokeWidth(2.0f);
        canvas.drawRoundRect(rect, 20.0f, border);

        Paint t;
        t.setStyle(Paint::Style::FILL);
        t.setColor(Color(255, 255, 255));
        t.setTextSize(28.0f);
        t.setTextAlign(Paint::TextAlign::CENTER);
        t.setTextBaseline(Paint::TextBaseline::MIDDLE);
        applyGameFont(t);
        float cx = rect.getX() + rect.getWidth() * 0.5f;
        float cy = rect.getY() + rect.getHeight() * 0.5f;

        canvas.drawText(title, cx, cy - 20.0f, t);

        t.setTextSize(16.0f);
        t.setColor(Color(180, 190, 220));
        canvas.drawText(sub, cx, cy + 24.0f, t);

        t.setTextAlign(Paint::TextAlign::LEFT);
        t.setTextBaseline(Paint::TextBaseline::TOP);
    }
};

struct GameContext {
    TetrisGame* game;
    Canvas* canvas;
    int windowW;
    int windowH;
};

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* ctx = static_cast<GameContext*>(glfwGetWindowUserPointer(window));
    if (!ctx || !ctx->game) return;
    TetrisGame& g = *ctx->game;

    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }

    switch (key) {
        case GLFW_KEY_LEFT:
            g.moveLeft();
            break;
        case GLFW_KEY_RIGHT:
            g.moveRight();
            break;
        case GLFW_KEY_DOWN:
            if (action == GLFW_PRESS) g.hardDrop();
            break;
        case GLFW_KEY_UP:
            if (action == GLFW_PRESS) g.rotateCW();
            break;
        case GLFW_KEY_Z:
            if (action == GLFW_PRESS) g.rotateCCW();
            break;
        case GLFW_KEY_P:
            if (action == GLFW_PRESS) g.togglePause();
            break;
        case GLFW_KEY_R:
            if (action == GLFW_PRESS) g.restart();
            break;
    }
}

static void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    auto* ctx = static_cast<GameContext*>(glfwGetWindowUserPointer(window));
    if (ctx && ctx->canvas && width > 0 && height > 0) {
        ctx->canvas->setSize(width, height);
        ctx->windowW = width;
        ctx->windowH = height;
    }
}

int main() {
    std::cout << "Starting Tetris..." << std::endl;

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);

    GLFWwindow* window = glfwCreateWindow(DESIGN_W, DESIGN_H, "Tetris", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    std::cout << "OpenGL " << glGetString(GL_VERSION) << " loaded." << std::endl;

    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    if (fbw <= 0) fbw = DESIGN_W;
    if (fbh <= 0) fbh = DESIGN_H;
    glViewport(0, 0, fbw, fbh);
    glEnable(GL_MULTISAMPLE);

    Canvas canvas;
    canvas.setSize(fbw, fbh);

    TetrisGame game;
    GameContext ctx;
    ctx.game = &game;
    ctx.canvas = &canvas;
    ctx.windowW = fbw;
    ctx.windowH = fbh;

    glfwSetWindowUserPointer(window, &ctx);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);

    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();
        float dt = static_cast<float>(now - lastTime);
        lastTime = now;

        glClear(GL_COLOR_BUFFER_BIT);

        canvas.beginFrame();
        game.update(dt);
        game.render(canvas, ctx.windowW, ctx.windowH);
        canvas.endFrame();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    canvas.shutdown();
    glfwTerminate();
    std::cout << "Final score: " << game.getScore() << std::endl;
    return 0;
}
