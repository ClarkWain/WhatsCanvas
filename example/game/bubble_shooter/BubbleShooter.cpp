#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "canvas/Canvas.h"
#include "canvas/Paint.h"
#include "canvas/base.h"

namespace {
constexpr float kPi = 3.1415926535f;
constexpr int GRID_COLS = 10;
constexpr int GRID_ROWS = 17;
constexpr float BUBBLE_RADIUS = 18.0f;
constexpr float BUBBLE_DIAMETER = BUBBLE_RADIUS * 2.0f;
constexpr float ROW_STEP = 31.0f;
constexpr float GRID_LEFT = 46.0f;
constexpr float GRID_TOP = 56.0f;
constexpr float BOARD_WIDTH = GRID_COLS * BUBBLE_DIAMETER + BUBBLE_RADIUS;
constexpr float BOARD_HEIGHT = (GRID_ROWS - 1) * ROW_STEP + BUBBLE_DIAMETER;
constexpr float SHOOTER_Y = GRID_TOP + BOARD_HEIGHT + 94.0f;
constexpr float SIDE_X = GRID_LEFT + BOARD_WIDTH + 48.0f;
constexpr int DESIGN_W = 860;
constexpr int DESIGN_H = 820;
constexpr float SHOT_SPEED = 720.0f;
constexpr float AIM_SPEED = 2.2f;
constexpr float MIN_AIM_ANGLE = 22.0f * kPi / 180.0f;
constexpr float MAX_AIM_ANGLE = 158.0f * kPi / 180.0f;
constexpr int TURNS_PER_DROP = 7;
constexpr float SNAP_SEARCH_RADIUS = BUBBLE_DIAMETER * 1.35f;
constexpr float COLLISION_DISTANCE = BUBBLE_DIAMETER - 2.0f;

const std::array<Color, 6> kBubblePalette = {
    Color(244, 86, 86),
    Color(248, 186, 66),
    Color(102, 214, 118),
    Color(74, 211, 216),
    Color(91, 129, 246),
    Color(180, 97, 238),
};

int clampByte(int value)
{
    return std::clamp(value, 0, 255);
}

Color mixColor(const Color &from, const Color &to, float t, int alpha = -1)
{
    const float amount = std::clamp(t, 0.0f, 1.0f);
    const int red = clampByte(static_cast<int>(std::lround(from.getR() + (to.getR() - from.getR()) * amount)));
    const int green = clampByte(static_cast<int>(std::lround(from.getG() + (to.getG() - from.getG()) * amount)));
    const int blue = clampByte(static_cast<int>(std::lround(from.getB() + (to.getB() - from.getB()) * amount)));
    const int resolvedAlpha = alpha >= 0
        ? clampByte(alpha)
        : clampByte(static_cast<int>(std::lround(from.getA() + (to.getA() - from.getA()) * amount)));
    return Color(red, green, blue, resolvedAlpha);
}

Color lightenColor(const Color &color, float amount, int alpha = -1)
{
    return mixColor(color, Color(255, 255, 255, color.getA()), amount, alpha);
}

Color darkenColor(const Color &color, float amount, int alpha = -1)
{
    return mixColor(color, Color(0, 0, 0, color.getA()), amount, alpha);
}

struct HexCell {
    int row = -1;
    int col = -1;

    HexCell() = default;
    HexCell(int rowValue, int colValue) : row(rowValue), col(colValue) {}

    bool isValid() const
    {
        return row >= 0 && col >= 0;
    }
};

struct ActiveShot {
    bool active = false;
    int color = 0;
    PointF position;
    PointF velocity;
};

class BubbleShooterGame {
public:
    BubbleShooterGame()
        : board_(GRID_ROWS, std::vector<int>(GRID_COLS, -1)),
          rng_(static_cast<unsigned int>(std::time(nullptr)))
    {
        restart();
    }

    void update(float dt)
    {
        updatePerformanceStats(dt);

        if (state_ != PLAYING) {
            return;
        }

        updateAim(dt);

        if (!activeShot_.active) {
            return;
        }

        float remaining = std::min(0.05f, std::max(0.0f, dt));
        while (remaining > 0.0f && activeShot_.active) {
            const float step = std::min(0.008f, remaining);
            remaining -= step;
            updateShot(step);
        }
    }

    void render(Canvas &canvas, int windowW, int windowH)
    {
        drawBackground(canvas);

        const float scale = std::min(
            static_cast<float>(windowW) / static_cast<float>(DESIGN_W),
            static_cast<float>(windowH) / static_cast<float>(DESIGN_H)
        );
        const float offsetX = (windowW - DESIGN_W * scale) * 0.5f;
        const float offsetY = (windowH - DESIGN_H * scale) * 0.5f;

        canvas.save();
        canvas.translate(offsetX, offsetY);
        canvas.scale(scale, scale);

        drawPanels(canvas);
        drawSockets(canvas);
        drawPlacedBubbles(canvas);
        if (activeShot_.active) {
            drawBubble(canvas, activeShot_.position, BUBBLE_RADIUS, kBubblePalette[activeShot_.color], 1.0f);
        } else {
            drawAimGuide(canvas);
        }
        drawShooter(canvas);
        drawSidebar(canvas);

        if (state_ == GAME_OVER) {
            drawOverlay(canvas, "GAME OVER", "Press R to restart");
        }

        canvas.restore();
    }

    void setAimLeft(bool held)
    {
        aimLeftHeld_ = held;
    }

    void setAimRight(bool held)
    {
        aimRightHeld_ = held;
    }

    void fire()
    {
        if (state_ != PLAYING || activeShot_.active) {
            return;
        }

        const PointF direction = aimDirection();
        activeShot_.active = true;
        activeShot_.color = currentBubble_;
        activeShot_.position = shooterOrigin() + direction * (BUBBLE_RADIUS + 8.0f);
        activeShot_.velocity = direction * SHOT_SPEED;
        currentBubble_ = nextBubble_;
        nextBubble_ = chooseUpcomingBubbleColor();
    }

    void restart()
    {
        score_ = 0;
        level_ = 1;
        turnsUntilDrop_ = TURNS_PER_DROP;
        aimAngle_ = kPi * 0.5f;
        aimLeftHeld_ = false;
        aimRightHeld_ = false;
        activeShot_ = ActiveShot();
        state_ = PLAYING;
        setupStage();
    }

    int getScore() const
    {
        return score_;
    }

private:
    enum State {
        PLAYING,
        GAME_OVER
    };

    std::vector<std::vector<int>> board_;
    std::mt19937 rng_;
    ActiveShot activeShot_;
    State state_ = PLAYING;
    int score_ = 0;
    int level_ = 1;
    int currentBubble_ = 0;
    int nextBubble_ = 0;
    int turnsUntilDrop_ = TURNS_PER_DROP;
    float displayedFps_ = 0.0f;
    float latestFrameMs_ = 0.0f;
    float fpsAccumulatedTime_ = 0.0f;
    int fpsAccumulatedFrames_ = 0;
    float aimAngle_ = kPi * 0.5f;
    bool aimLeftHeld_ = false;
    bool aimRightHeld_ = false;

    void updatePerformanceStats(float dt)
    {
        if (!std::isfinite(dt) || dt <= 0.0f) {
            return;
        }

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

    void setupStage()
    {
        clearBoard();

        const int seededRows = std::min(5 + (level_ - 1), 8);
        for (int row = 0; row < seededRows; ++row) {
            int filledInRow = 0;
            for (int col = 0; col < GRID_COLS; ++col) {
                const int chance = row < 2 ? 100 : std::max(48, 84 - row * 7);
                if (randomChance(chance)) {
                    board_[row][col] = randomPaletteColor();
                    ++filledInRow;
                }
            }

            if (filledInRow == 0) {
                board_[row][randomInt(0, GRID_COLS - 1)] = randomPaletteColor();
            }
        }

        currentBubble_ = chooseUpcomingBubbleColor();
        nextBubble_ = chooseUpcomingBubbleColor();
        activeShot_ = ActiveShot();
        state_ = PLAYING;
    }

    void clearBoard()
    {
        for (auto &row : board_) {
            std::fill(row.begin(), row.end(), -1);
        }
    }

    int activePaletteCount() const
    {
        return std::min(static_cast<int>(kBubblePalette.size()), 4 + (level_ - 1) / 2);
    }

    int randomInt(int minValue, int maxValue)
    {
        std::uniform_int_distribution<int> distribution(minValue, maxValue);
        return distribution(rng_);
    }

    bool randomChance(int percent)
    {
        return randomInt(1, 100) <= std::clamp(percent, 0, 100);
    }

    int randomPaletteColor()
    {
        return randomInt(0, activePaletteCount() - 1);
    }

    bool boardHasAnyBubbles() const
    {
        return countOccupied() > 0;
    }

    int countOccupied() const
    {
        int count = 0;
        for (const auto &row : board_) {
            for (int value : row) {
                if (value >= 0) {
                    ++count;
                }
            }
        }
        return count;
    }

    bool boardHasColor(int color) const
    {
        for (const auto &row : board_) {
            for (int value : row) {
                if (value == color) {
                    return true;
                }
            }
        }
        return false;
    }

    int chooseUpcomingBubbleColor()
    {
        if (!boardHasAnyBubbles()) {
            return randomPaletteColor();
        }

        std::vector<int> colors;
        std::array<bool, kBubblePalette.size()> seen = {};
        for (const auto &row : board_) {
            for (int value : row) {
                if (value >= 0 && value < static_cast<int>(kBubblePalette.size()) && !seen[static_cast<size_t>(value)]) {
                    seen[static_cast<size_t>(value)] = true;
                    colors.push_back(value);
                }
            }
        }

        if (colors.empty()) {
            return randomPaletteColor();
        }

        return colors[static_cast<size_t>(randomInt(0, static_cast<int>(colors.size()) - 1))];
    }

    void refreshQueuedColors()
    {
        if (!boardHasAnyBubbles()) {
            return;
        }

        if (!boardHasColor(currentBubble_)) {
            currentBubble_ = chooseUpcomingBubbleColor();
        }
        if (!boardHasColor(nextBubble_)) {
            nextBubble_ = chooseUpcomingBubbleColor();
        }
    }

    PointF shooterOrigin() const
    {
        return PointF(GRID_LEFT + BOARD_WIDTH * 0.5f, SHOOTER_Y);
    }

    PointF aimDirection() const
    {
        return PointF(std::cos(aimAngle_), -std::sin(aimAngle_));
    }

    PointF cellCenter(int row, int col) const
    {
        const float offsetX = (row % 2 == 0) ? 0.0f : BUBBLE_RADIUS;
        return PointF(
            GRID_LEFT + BUBBLE_RADIUS + col * BUBBLE_DIAMETER + offsetX,
            GRID_TOP + BUBBLE_RADIUS + row * ROW_STEP
        );
    }

    bool inBounds(int row, int col) const
    {
        return row >= 0 && row < GRID_ROWS && col >= 0 && col < GRID_COLS;
    }

    std::vector<HexCell> neighborsOf(int row, int col) const
    {
        static const std::array<std::array<int, 2>, 6> evenOffsets = {{
            {{0, -1}}, {{0, 1}}, {{-1, -1}}, {{-1, 0}}, {{1, -1}}, {{1, 0}}
        }};
        static const std::array<std::array<int, 2>, 6> oddOffsets = {{
            {{0, -1}}, {{0, 1}}, {{-1, 0}}, {{-1, 1}}, {{1, 0}}, {{1, 1}}
        }};

        const auto &offsets = (row % 2 == 0) ? evenOffsets : oddOffsets;
        std::vector<HexCell> result;
        result.reserve(offsets.size());
        for (const auto &offset : offsets) {
            const int nextRow = row + offset[0];
            const int nextCol = col + offset[1];
            if (inBounds(nextRow, nextCol)) {
                result.emplace_back(nextRow, nextCol);
            }
        }
        return result;
    }

    bool isAttachableCell(int row, int col) const
    {
        if (!inBounds(row, col) || board_[row][col] != -1) {
            return false;
        }

        if (row == 0) {
            return true;
        }

        for (const HexCell &neighbor : neighborsOf(row, col)) {
            if (board_[neighbor.row][neighbor.col] >= 0) {
                return true;
            }
        }
        return false;
    }

    bool findNearestAttachCell(const PointF &position, HexCell &cell) const
    {
        float bestDistanceSq = std::numeric_limits<float>::max();
        HexCell bestCell;

        for (int row = 0; row < GRID_ROWS; ++row) {
            for (int col = 0; col < GRID_COLS; ++col) {
                if (!isAttachableCell(row, col)) {
                    continue;
                }

                const PointF center = cellCenter(row, col);
                const float dx = center.getX() - position.getX();
                const float dy = center.getY() - position.getY();
                const float distanceSq = dx * dx + dy * dy;
                if (distanceSq < bestDistanceSq) {
                    bestDistanceSq = distanceSq;
                    bestCell = HexCell(row, col);
                }
            }
        }

        if (!bestCell.isValid()) {
            return false;
        }

        if (bestDistanceSq > SNAP_SEARCH_RADIUS * SNAP_SEARCH_RADIUS && boardHasAnyBubbles()) {
            return false;
        }

        cell = bestCell;
        return true;
    }

    void updateAim(float dt)
    {
        if (aimLeftHeld_ == aimRightHeld_) {
            return;
        }

        const float direction = aimLeftHeld_ ? 1.0f : -1.0f;
        aimAngle_ = std::clamp(aimAngle_ + direction * AIM_SPEED * dt, MIN_AIM_ANGLE, MAX_AIM_ANGLE);
    }

    void updateShot(float dt)
    {
        activeShot_.position += activeShot_.velocity * dt;

        const float minX = GRID_LEFT + BUBBLE_RADIUS;
        const float maxX = GRID_LEFT + BOARD_WIDTH - BUBBLE_RADIUS;
        if (activeShot_.position.getX() <= minX) {
            activeShot_.position.setX(minX);
            activeShot_.velocity.setX(std::abs(activeShot_.velocity.getX()));
        } else if (activeShot_.position.getX() >= maxX) {
            activeShot_.position.setX(maxX);
            activeShot_.velocity.setX(-std::abs(activeShot_.velocity.getX()));
        }

        if (activeShot_.position.getY() <= GRID_TOP + BUBBLE_RADIUS) {
            activeShot_.position.setY(GRID_TOP + BUBBLE_RADIUS);
            settleActiveShot();
            return;
        }

        if (collidesWithPlacedBubble(activeShot_.position)) {
            settleActiveShot();
        }
    }

    bool collidesWithPlacedBubble(const PointF &position) const
    {
        const float collisionDistanceSq = COLLISION_DISTANCE * COLLISION_DISTANCE;
        for (int row = 0; row < GRID_ROWS; ++row) {
            for (int col = 0; col < GRID_COLS; ++col) {
                if (board_[row][col] < 0) {
                    continue;
                }

                const PointF center = cellCenter(row, col);
                const float dx = center.getX() - position.getX();
                const float dy = center.getY() - position.getY();
                if (dx * dx + dy * dy <= collisionDistanceSq) {
                    return true;
                }
            }
        }
        return false;
    }

    void settleActiveShot()
    {
        HexCell target;
        if (!findNearestAttachCell(activeShot_.position, target)) {
            activeShot_ = ActiveShot();
            state_ = GAME_OVER;
            return;
        }

        board_[target.row][target.col] = activeShot_.color;
        activeShot_ = ActiveShot();

        const bool popped = resolvePlacement(target.row, target.col);
        if (countOccupied() == 0) {
            score_ += 250 * level_;
            ++level_;
            turnsUntilDrop_ = TURNS_PER_DROP;
            setupStage();
            return;
        }

        if (popped) {
            turnsUntilDrop_ = TURNS_PER_DROP;
        } else {
            --turnsUntilDrop_;
            if (turnsUntilDrop_ <= 0) {
                pushCeilingRow();
                turnsUntilDrop_ = TURNS_PER_DROP;
            }
        }

        refreshQueuedColors();
        if (hasReachedLoseLine()) {
            state_ = GAME_OVER;
        }
    }

    bool resolvePlacement(int row, int col)
    {
        const int color = board_[row][col];
        const std::vector<HexCell> cluster = collectCluster(row, col, color);
        if (cluster.size() < 3) {
            return false;
        }

        for (const HexCell &cell : cluster) {
            board_[cell.row][cell.col] = -1;
        }
        score_ += static_cast<int>(cluster.size()) * 35 * level_;

        const int floatingRemoved = removeFloatingClusters();
        if (floatingRemoved > 0) {
            score_ += floatingRemoved * 55 * level_;
        }

        return true;
    }

    std::vector<HexCell> collectCluster(int startRow, int startCol, int color) const
    {
        std::vector<HexCell> cluster;
        if (!inBounds(startRow, startCol) || color < 0 || board_[startRow][startCol] != color) {
            return cluster;
        }

        std::vector<std::vector<bool>> visited(GRID_ROWS, std::vector<bool>(GRID_COLS, false));
        std::queue<HexCell> frontier;
        frontier.emplace(startRow, startCol);
        visited[startRow][startCol] = true;

        while (!frontier.empty()) {
            const HexCell cell = frontier.front();
            frontier.pop();
            cluster.push_back(cell);

            for (const HexCell &neighbor : neighborsOf(cell.row, cell.col)) {
                if (visited[neighbor.row][neighbor.col]) {
                    continue;
                }
                visited[neighbor.row][neighbor.col] = true;
                if (board_[neighbor.row][neighbor.col] == color) {
                    frontier.push(neighbor);
                }
            }
        }

        return cluster;
    }

    int removeFloatingClusters()
    {
        std::vector<std::vector<bool>> anchored(GRID_ROWS, std::vector<bool>(GRID_COLS, false));
        std::queue<HexCell> frontier;

        for (int col = 0; col < GRID_COLS; ++col) {
            if (board_[0][col] >= 0) {
                anchored[0][col] = true;
                frontier.emplace(0, col);
            }
        }

        while (!frontier.empty()) {
            const HexCell cell = frontier.front();
            frontier.pop();
            for (const HexCell &neighbor : neighborsOf(cell.row, cell.col)) {
                if (anchored[neighbor.row][neighbor.col] || board_[neighbor.row][neighbor.col] < 0) {
                    continue;
                }
                anchored[neighbor.row][neighbor.col] = true;
                frontier.push(neighbor);
            }
        }

        int removed = 0;
        for (int row = 0; row < GRID_ROWS; ++row) {
            for (int col = 0; col < GRID_COLS; ++col) {
                if (board_[row][col] >= 0 && !anchored[row][col]) {
                    board_[row][col] = -1;
                    ++removed;
                }
            }
        }
        return removed;
    }

    void pushCeilingRow()
    {
        for (int row = GRID_ROWS - 1; row > 0; --row) {
            board_[row] = board_[row - 1];
        }
        board_[0] = makeCeilingRow();
    }

    std::vector<int> makeCeilingRow()
    {
        std::vector<int> row(GRID_COLS, -1);
        std::vector<int> columns(GRID_COLS);
        std::iota(columns.begin(), columns.end(), 0);
        std::shuffle(columns.begin(), columns.end(), rng_);
        const int gaps = randomInt(1, 2);

        for (int index = 0; index < GRID_COLS; ++index) {
            const bool isGap = index < gaps;
            row[columns[static_cast<size_t>(index)]] = isGap ? -1 : randomPaletteColor();
        }
        return row;
    }

    bool hasReachedLoseLine() const
    {
        const float loseLine = shooterOrigin().getY() - 70.0f;
        for (int row = 0; row < GRID_ROWS; ++row) {
            for (int col = 0; col < GRID_COLS; ++col) {
                if (board_[row][col] < 0) {
                    continue;
                }
                if (cellCenter(row, col).getY() + BUBBLE_RADIUS >= loseLine) {
                    return true;
                }
            }
        }
        return false;
    }

    RectF boardPanelRect() const
    {
        return RectF(GRID_LEFT - 18.0f, GRID_TOP - 18.0f, BOARD_WIDTH + 36.0f, SHOOTER_Y - GRID_TOP + 108.0f);
    }

    RectF sidebarRect() const
    {
        return RectF(SIDE_X - 16.0f, GRID_TOP - 18.0f, DESIGN_W - SIDE_X - 36.0f, SHOOTER_Y - GRID_TOP + 108.0f);
    }

    void drawBackground(Canvas &canvas)
    {
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
        glow.setFillColor(Color(72, 112, 224, 26));
        canvas.drawCircle(PointF(128.0f, 114.0f), 108.0f, glow);
        glow.setFillColor(Color(117, 74, 214, 24));
        canvas.drawCircle(PointF(760.0f, 152.0f), 124.0f, glow);
        glow.setFillColor(Color(49, 177, 182, 24));
        canvas.drawCircle(PointF(710.0f, 672.0f), 154.0f, glow);
    }

    void drawPanels(Canvas &canvas)
    {
        const RectF boardRect = boardPanelRect();
        Paint boardFill;
        boardFill.setStyle(Paint::Style::FILL);
        boardFill.setLinearGradient(boardRect.getX(), boardRect.getY(), boardRect.getX(), boardRect.getY() + boardRect.getHeight(), {
            Paint::ColorStop(0.0f, Color(24, 34, 60, 235)),
            Paint::ColorStop(1.0f, Color(13, 18, 33, 235))
        });
        canvas.drawRoundRect(boardRect, 28.0f, boardFill);

        Paint boardStroke;
        boardStroke.setStyle(Paint::Style::STROKE);
        boardStroke.setStrokeWidth(3.0f);
        boardStroke.setStrokeColor(Color(88, 116, 176, 180));
        canvas.drawRoundRect(boardRect, 28.0f, boardStroke);

        const RectF playRect(GRID_LEFT - 6.0f, GRID_TOP - 6.0f, BOARD_WIDTH + 12.0f, BOARD_HEIGHT + 12.0f);
        Paint playFill;
        playFill.setStyle(Paint::Style::FILL);
        playFill.setLinearGradient(playRect.getX(), playRect.getY(), playRect.getX(), playRect.getY() + playRect.getHeight(), {
            Paint::ColorStop(0.0f, Color(10, 15, 29)),
            Paint::ColorStop(1.0f, Color(15, 18, 29))
        });
        canvas.drawRoundRect(playRect, 22.0f, playFill);

        Paint playStroke;
        playStroke.setStyle(Paint::Style::STROKE);
        playStroke.setStrokeWidth(2.0f);
        playStroke.setStrokeColor(Color(54, 76, 120));
        canvas.drawRoundRect(playRect, 22.0f, playStroke);

        const RectF sideRect = sidebarRect();
        Paint sideFill;
        sideFill.setStyle(Paint::Style::FILL);
        sideFill.setLinearGradient(sideRect.getX(), sideRect.getY(), sideRect.getX(), sideRect.getY() + sideRect.getHeight(), {
            Paint::ColorStop(0.0f, Color(22, 31, 56, 230)),
            Paint::ColorStop(1.0f, Color(13, 18, 33, 230))
        });
        canvas.drawRoundRect(sideRect, 28.0f, sideFill);

        Paint sideStroke;
        sideStroke.setStyle(Paint::Style::STROKE);
        sideStroke.setStrokeWidth(3.0f);
        sideStroke.setStrokeColor(Color(72, 99, 152, 170));
        canvas.drawRoundRect(sideRect, 28.0f, sideStroke);

        Paint danger;
        danger.setStyle(Paint::Style::STROKE);
        danger.setStrokeColor(Color(255, 112, 112, 120));
        danger.setStrokeWidth(3.0f);
        danger.setDashPathEffect({10.0f, 8.0f});
        const float loseY = shooterOrigin().getY() - 70.0f;
        canvas.drawLine(GRID_LEFT + 10.0f, loseY, GRID_LEFT + BOARD_WIDTH - 10.0f, loseY, danger);
    }

    void drawSockets(Canvas &canvas)
    {
        Paint socketFill;
        socketFill.setStyle(Paint::Style::FILL);
        socketFill.setFillColor(Color(33, 41, 58));

        Paint socketStroke;
        socketStroke.setStyle(Paint::Style::STROKE);
        socketStroke.setStrokeWidth(1.0f);
        socketStroke.setStrokeColor(Color(62, 76, 103, 150));

        for (int row = 0; row < GRID_ROWS; ++row) {
            for (int col = 0; col < GRID_COLS; ++col) {
                const PointF center = cellCenter(row, col);
                canvas.drawCircle(center, BUBBLE_RADIUS - 1.5f, socketFill);
                canvas.drawCircle(center, BUBBLE_RADIUS - 1.5f, socketStroke);
            }
        }
    }

    void drawBubble(Canvas &canvas, const PointF &center, float radius, const Color &baseColor, float alpha)
    {
        const int alphaByte = clampByte(static_cast<int>(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)));
        Paint fill;
        fill.setStyle(Paint::Style::FILL);
        fill.setAlpha(alpha);
        fill.setRadialGradient(center.getX() - radius * 0.35f, center.getY() - radius * 0.38f, radius * 1.2f, {
            Paint::ColorStop(0.0f, lightenColor(baseColor, 0.62f, alphaByte)),
            Paint::ColorStop(0.55f, Color(baseColor.getR(), baseColor.getG(), baseColor.getB(), alphaByte)),
            Paint::ColorStop(1.0f, darkenColor(baseColor, 0.45f, alphaByte))
        });
        canvas.drawCircle(center, radius, fill);

        Paint rim;
        rim.setStyle(Paint::Style::STROKE);
        rim.setStrokeWidth(2.0f);
        rim.setStrokeColor(Color(255, 255, 255, clampByte(alphaByte / 2 + 40)));
        canvas.drawCircle(center, radius - 1.0f, rim);

        Paint shadowRim;
        shadowRim.setStyle(Paint::Style::STROKE);
        shadowRim.setStrokeWidth(2.0f);
        shadowRim.setStrokeColor(Color(14, 18, 28, clampByte(alphaByte / 2 + 70)));
        canvas.drawCircle(center, radius, shadowRim);

        Paint gloss;
        gloss.setStyle(Paint::Style::FILL);
        gloss.setFillColor(Color(255, 255, 255, clampByte(alphaByte / 3 + 24)));
        canvas.drawCircle(PointF(center.getX() - radius * 0.34f, center.getY() - radius * 0.34f), radius * 0.28f, gloss);
    }

    void drawPlacedBubbles(Canvas &canvas)
    {
        for (int row = 0; row < GRID_ROWS; ++row) {
            for (int col = 0; col < GRID_COLS; ++col) {
                const int value = board_[row][col];
                if (value < 0) {
                    continue;
                }
                drawBubble(canvas, cellCenter(row, col), BUBBLE_RADIUS, kBubblePalette[static_cast<size_t>(value)], 1.0f);
            }
        }
    }

    void drawAimGuide(Canvas &canvas)
    {
        if (state_ != PLAYING || activeShot_.active) {
            return;
        }

        std::vector<PointF> points;
        points.reserve(5);
        points.push_back(shooterOrigin());

        PointF cursor = shooterOrigin();
        PointF direction = aimDirection();
        const float minX = GRID_LEFT + BUBBLE_RADIUS;
        const float maxX = GRID_LEFT + BOARD_WIDTH - BUBBLE_RADIUS;
        const float topY = GRID_TOP + BUBBLE_RADIUS;
        float remaining = 520.0f;

        while (remaining > 0.0f && points.size() < 5) {
            float wallDistance = std::numeric_limits<float>::max();
            if (direction.getX() < -0.0001f) {
                wallDistance = (minX - cursor.getX()) / direction.getX();
            } else if (direction.getX() > 0.0001f) {
                wallDistance = (maxX - cursor.getX()) / direction.getX();
            }

            float topDistance = std::numeric_limits<float>::max();
            if (direction.getY() < -0.0001f) {
                topDistance = (topY - cursor.getY()) / direction.getY();
            }

            float travel = std::min(wallDistance, topDistance);
            if (!std::isfinite(travel) || travel <= 0.0f) {
                travel = remaining;
            }
            travel = std::min(travel, remaining);

            cursor += direction * travel;
            points.push_back(cursor);
            remaining -= travel;

            if (topDistance <= wallDistance) {
                break;
            }
            direction.setX(-direction.getX());
        }

        Paint guide;
        guide.setStyle(Paint::Style::STROKE);
        guide.setStrokeWidth(3.0f);
        guide.setStrokeColor(Color(191, 225, 255, 130));
        guide.setDashPathEffect({10.0f, 9.0f});
        canvas.drawPolyline(points, guide);
    }

    void drawShooter(Canvas &canvas)
    {
        const PointF origin = shooterOrigin();
        const PointF tip = origin + aimDirection() * 62.0f;

        Paint barrel;
        barrel.setStyle(Paint::Style::STROKE);
        barrel.setStrokeWidth(14.0f);
        barrel.setStrokeCap(Paint::StrokeCap::ROUND);
        barrel.setStrokeColor(Color(78, 102, 144));
        canvas.drawLine(origin, tip, barrel);

        barrel.setStrokeWidth(8.0f);
        barrel.setStrokeColor(Color(162, 194, 255));
        canvas.drawLine(origin, tip, barrel);

        Paint base;
        base.setStyle(Paint::Style::FILL);
        base.setLinearGradient(origin.getX(), origin.getY() - 30.0f, origin.getX(), origin.getY() + 30.0f, {
            Paint::ColorStop(0.0f, Color(49, 61, 92)),
            Paint::ColorStop(1.0f, Color(24, 30, 46))
        });
        canvas.drawCircle(origin, 28.0f, base);

        Paint ring;
        ring.setStyle(Paint::Style::STROKE);
        ring.setStrokeWidth(3.0f);
        ring.setStrokeColor(Color(138, 162, 217));
        canvas.drawCircle(origin, 28.0f, ring);

        drawBubble(canvas, origin, BUBBLE_RADIUS, kBubblePalette[static_cast<size_t>(currentBubble_)], 1.0f);

        Paint tray;
        tray.setStyle(Paint::Style::STROKE);
        tray.setStrokeWidth(2.0f);
        tray.setStrokeColor(Color(82, 102, 146));
        canvas.drawCircle(PointF(origin.getX() - 82.0f, origin.getY() + 4.0f), 22.0f, tray);
        drawBubble(canvas, PointF(origin.getX() - 82.0f, origin.getY() + 4.0f), 16.0f, kBubblePalette[static_cast<size_t>(nextBubble_)], 0.95f);
    }

    void drawSidebar(Canvas &canvas)
    {
        const RectF rect = sidebarRect();
        float x = rect.getX() + 24.0f;
        float y = rect.getY() + 24.0f;

        Paint title;
        title.setStyle(Paint::Style::FILL);
        title.setColor(Color(238, 244, 255));
        title.setTextSize(28.0f);
        title.setLetterSpacing(1.0f);
        canvas.drawText("BUBBLE SHOT", x, y, title);
        y += canvas.measureTextMetrics("BUBBLE SHOT", title).height + 18.0f;

        Paint subtitle;
        subtitle.setStyle(Paint::Style::FILL);
        subtitle.setColor(Color(152, 173, 214));
        subtitle.setTextSize(12.0f);
        canvas.drawText("Match 3 to pop clusters", x, y, subtitle);
        y += canvas.measureTextMetrics("Match 3 to pop clusters", subtitle).height + 28.0f;

        Paint label;
        label.setStyle(Paint::Style::FILL);
        label.setColor(Color(180, 198, 234));
        label.setTextSize(14.0f);

        Paint value = label;
        value.setColor(Color(255, 255, 255));
        value.setTextSize(24.0f);

        auto drawMetric = [&](const std::string &heading, const std::string &content, float gap) {
            canvas.drawText(heading, x, y, label);
            y += canvas.measureTextMetrics(heading, label).height + 6.0f;
            canvas.drawText(content, x, y, value);
            y += canvas.measureTextMetrics(content, value).height + gap;
        };

        drawMetric("SCORE", std::to_string(score_), 18.0f);
        drawMetric("LEVEL", std::to_string(level_), 18.0f);
        drawMetric("DROP IN", std::to_string(turnsUntilDrop_), 26.0f);

        Paint perfValue = label;
        perfValue.setColor(Color(244, 248, 255));
        perfValue.setTextSize(16.0f);
        const int fps = std::max(0, static_cast<int>(std::lround(displayedFps_)));
        const int frameMs = std::max(0, static_cast<int>(std::lround(latestFrameMs_)));
        canvas.drawText("PERF", x, y, label);
        y += canvas.measureTextMetrics("PERF", label).height + 6.0f;
        const std::string perfText = std::to_string(fps) + " FPS / " + std::to_string(frameMs) + " ms";
        canvas.drawText(perfText, x, y, perfValue);
        y += canvas.measureTextMetrics(perfText, perfValue).height + 24.0f;

        canvas.drawText("NEXT", x, y, label);
        y += canvas.measureTextMetrics("NEXT", label).height + 12.0f;
        const PointF previewCenter(x + 28.0f, y + 26.0f);

        Paint previewRing;
        previewRing.setStyle(Paint::Style::STROKE);
        previewRing.setStrokeWidth(2.0f);
        previewRing.setStrokeColor(Color(94, 115, 160));
        canvas.drawCircle(previewCenter, 30.0f, previewRing);
        drawBubble(canvas, previewCenter, 22.0f, kBubblePalette[static_cast<size_t>(nextBubble_)], 1.0f);
        y += 82.0f;

        Paint body;
        body.setStyle(Paint::Style::FILL);
        body.setColor(Color(154, 171, 205));
        body.setTextSize(12.0f);
        canvas.drawText("Controls", x, y, label);
        y += canvas.measureTextMetrics("Controls", label).height + 10.0f;
        canvas.drawText("Left/Right  Aim", x, y, body);
        y += canvas.measureTextMetrics("Left/Right  Aim", body).height + 4.0f;
        canvas.drawText("Space or Up Fire", x, y, body);
        y += canvas.measureTextMetrics("Space or Up Fire", body).height + 4.0f;
        canvas.drawText("R           Restart", x, y, body);
        y += canvas.measureTextMetrics("R           Restart", body).height + 18.0f;

        Paint note;
        note.setStyle(Paint::Style::FILL);
        note.setColor(Color(129, 144, 177));
        note.setTextSize(11.0f);
        canvas.drawTextBox(
            "Every few missed turns, a fresh ceiling row drops in. Clear the board to advance the level.",
            RectF(x, y, rect.getWidth() - 48.0f, 120.0f),
            16.0f,
            note
        );
    }

    void drawOverlay(Canvas &canvas, const std::string &titleText, const std::string &subtitleText)
    {
        const RectF rect = boardPanelRect();
        Paint shade;
        shade.setStyle(Paint::Style::FILL);
        shade.setFillColor(Color(4, 6, 12, 184));
        canvas.drawRoundRect(rect, 28.0f, shade);

        Paint title;
        title.setStyle(Paint::Style::FILL);
        title.setColor(Color(255, 255, 255));
        title.setTextSize(34.0f);
        title.setTextAlign(Paint::TextAlign::CENTER);
        title.setTextBaseline(Paint::TextBaseline::MIDDLE);

        Paint subtitle;
        subtitle.setStyle(Paint::Style::FILL);
        subtitle.setColor(Color(194, 207, 234));
        subtitle.setTextSize(16.0f);
        subtitle.setTextAlign(Paint::TextAlign::CENTER);
        subtitle.setTextBaseline(Paint::TextBaseline::MIDDLE);

        const float centerX = rect.getX() + rect.getWidth() * 0.5f;
        const float centerY = rect.getY() + rect.getHeight() * 0.5f;
        canvas.drawText(titleText, centerX, centerY - 20.0f, title);
        canvas.drawText(subtitleText, centerX, centerY + 28.0f, subtitle);
    }
};

struct InputState {
    bool leftArrow = false;
    bool rightArrow = false;
    bool aKey = false;
    bool dKey = false;
};

struct GameContext {
    BubbleShooterGame *game = nullptr;
    Canvas *canvas = nullptr;
    int windowW = 0;
    int windowH = 0;
    InputState input;
};

void syncAimState(GameContext &context)
{
    context.game->setAimLeft(context.input.leftArrow || context.input.aKey);
    context.game->setAimRight(context.input.rightArrow || context.input.dKey);
}

void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    auto *context = static_cast<GameContext *>(glfwGetWindowUserPointer(window));
    if (context == nullptr || context->game == nullptr) {
        return;
    }

    if (action == GLFW_PRESS || action == GLFW_RELEASE) {
        const bool held = action != GLFW_RELEASE;
        switch (key) {
        case GLFW_KEY_LEFT:
            context->input.leftArrow = held;
            syncAimState(*context);
            break;
        case GLFW_KEY_RIGHT:
            context->input.rightArrow = held;
            syncAimState(*context);
            break;
        case GLFW_KEY_A:
            context->input.aKey = held;
            syncAimState(*context);
            break;
        case GLFW_KEY_D:
            context->input.dKey = held;
            syncAimState(*context);
            break;
        default:
            break;
        }
    }

    if (action != GLFW_PRESS) {
        return;
    }

    switch (key) {
    case GLFW_KEY_SPACE:
    case GLFW_KEY_UP:
    case GLFW_KEY_ENTER:
        context->game->fire();
        break;
    case GLFW_KEY_R:
        context->game->restart();
        break;
    default:
        break;
    }
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    glViewport(0, 0, width, height);
    auto *context = static_cast<GameContext *>(glfwGetWindowUserPointer(window));
    if (context != nullptr && context->canvas != nullptr && width > 0 && height > 0) {
        context->canvas->setSize(width, height);
        context->windowW = width;
        context->windowH = height;
    }
}
}

int main()
{
    std::cout << "Starting Bubble Shooter..." << std::endl;

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);

    GLFWwindow *window = glfwCreateWindow(DESIGN_W, DESIGN_H, "Bubble Shooter", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0) {
        framebufferWidth = DESIGN_W;
    }
    if (framebufferHeight <= 0) {
        framebufferHeight = DESIGN_H;
    }

    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glEnable(GL_MULTISAMPLE);

    Canvas canvas;
    canvas.setSize(framebufferWidth, framebufferHeight);

    BubbleShooterGame game;
    GameContext context;
    context.game = &game;
    context.canvas = &canvas;
    context.windowW = framebufferWidth;
    context.windowH = framebufferHeight;

    glfwSetWindowUserPointer(window, &context);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);

    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - lastTime);
        lastTime = now;

        glClear(GL_COLOR_BUFFER_BIT);

        canvas.beginFrame();
        game.update(dt);
        game.render(canvas, context.windowW, context.windowH);
        canvas.endFrame();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    canvas.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "Final score: " << game.getScore() << std::endl;
    return 0;
}