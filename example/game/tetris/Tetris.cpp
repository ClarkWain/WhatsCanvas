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
constexpr int GRID_X = 30;
constexpr int GRID_Y = 40;
constexpr int SIDE_X = GRID_X + COLS * CELL + 30;
constexpr int DESIGN_W = SIDE_X + 180;
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

        canvas.restore();

        if (state_ == GAME_OVER) drawOverlay(canvas, "GAME OVER", "Press R to restart", windowW, windowH);
        else if (state_ == PAUSED) drawOverlay(canvas, "PAUSED", "Press P to resume", windowW, windowH);
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
    std::vector<PieceType> bag_;

    int getDropInterval() const {
        return std::max(50, DROP_INTERVAL_MS - (level_ - 1) * 75);
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
        canvas.drawColor(Color(18, 22, 30));
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
        Paint bg;
        bg.setStyle(Paint::Style::FILL);
        bg.setFillColor(Color(10, 12, 18));
        canvas.drawRect(RectF(GRID_X - 4, GRID_Y - 4, COLS * CELL + 8, ROWS * CELL + 8), bg);
        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeColor(Color(40, 50, 70));
        border.setStrokeWidth(2.0f);
        canvas.drawRect(RectF(GRID_X - 4, GRID_Y - 4, COLS * CELL + 8, ROWS * CELL + 8), border);
        for (int row = 0; row < ROWS; ++row) {
            for (int col = 0; col < COLS; ++col) {
                int v = board_[row][col];
                if (v != 0) {
                    drawBlock(canvas, GRID_X + col * CELL, GRID_Y + row * CELL, CELL, kColors[v - 1], 1.0f);
                } else {
                    Paint gp;
                    gp.setStyle(Paint::Style::FILL);
                    gp.setFillColor(Color(25, 30, 40));
                    canvas.drawRect(RectF(GRID_X + col * CELL + 1, GRID_Y + row * CELL + 1, CELL - 2, CELL - 2), gp);
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
        float sx = SIDE_X + 10;
        float y = GRID_Y;

        Paint text;
        text.setStyle(Paint::Style::FILL);
        text.setColor(Color(200, 210, 240));

        auto advanceY = [&](const std::string& content, float gap) {
            y += canvas.measureTextMetrics(content, text).height + gap;
        };

        text.setTextSize(16.0f);
        canvas.drawText("NEXT", sx, y, text);
        advanceY("NEXT", 2.0f);

        for (const auto& b : kPieceData[nextType_][0]) {
            drawBlock(canvas, sx + b.getX() * 22, y + b.getY() * 22, 22, kColors[nextType_], 1.0f);
        }
        y += 110;

        text.setTextSize(14.0f);
        text.setColor(Color(200, 210, 240));
        canvas.drawText("SCORE", sx, y, text);
        advanceY("SCORE", 4.0f);
        text.setColor(Color(255, 255, 255));
        text.setTextSize(20.0f);
        canvas.drawText(std::to_string(score_), sx, y, text);
        advanceY(std::to_string(score_), 8.0f);

        text.setColor(Color(200, 210, 240));
        text.setTextSize(14.0f);
        canvas.drawText("LEVEL", sx, y, text);
        advanceY("LEVEL", 4.0f);
        text.setColor(Color(255, 255, 255));
        text.setTextSize(20.0f);
        canvas.drawText(std::to_string(level_), sx, y, text);
        advanceY(std::to_string(level_), 8.0f);

        text.setColor(Color(200, 210, 240));
        text.setTextSize(14.0f);
        canvas.drawText("LINES", sx, y, text);
        advanceY("LINES", 4.0f);
        text.setColor(Color(255, 255, 255));
        text.setTextSize(20.0f);
        canvas.drawText(std::to_string(lines_), sx, y, text);
        advanceY(std::to_string(lines_), 20.0f);

        text.setColor(Color(120, 130, 160));
        text.setTextSize(11.0f);
        canvas.drawText("Controls:", sx, y, text);
        advanceY("Controls:", 4.0f);
        canvas.drawText("Left/Right Move", sx, y, text);
        advanceY("Left/Right Move", 2.0f);
        canvas.drawText("Down Hard Drop", sx, y, text);
        advanceY("Down Hard Drop", 2.0f);
        canvas.drawText("Up RotCW  Z RotCCW", sx, y, text);
        advanceY("Up RotCW  Z RotCCW", 2.0f);
        canvas.drawText("P Pause  R Restart", sx, y, text);
    }

    void drawOverlay(Canvas& canvas, const std::string& title, const std::string& sub, int windowW, int windowH) {
        float scale = std::min(
            static_cast<float>(windowW) / DESIGN_W,
            static_cast<float>(windowH) / DESIGN_H
        );
        float offsetX = (windowW - DESIGN_W * scale) * 0.5f;
        float offsetY = (windowH - DESIGN_H * scale) * 0.5f;

        float gridPx = offsetX + (GRID_X - 4) * scale;
        float gridPy = offsetY + (GRID_Y - 4) * scale;
        float gridW = (COLS * CELL + 8) * scale;
        float gridH = (ROWS * CELL + 8) * scale;

        Paint ov;
        ov.setStyle(Paint::Style::FILL);
        ov.setFillColor(Color(0, 0, 0, 180));
        canvas.drawRect(RectF(gridPx, gridPy, gridW, gridH), ov);

        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeColor(Color(100, 120, 180));
        border.setStrokeWidth(2.0f);
        canvas.drawRect(RectF(gridPx, gridPy, gridW, gridH), border);

        Paint t;
        t.setStyle(Paint::Style::FILL);
        t.setColor(Color(255, 255, 255));
        t.setTextSize(28.0f * scale);
        t.setTextAlign(Paint::TextAlign::CENTER);
        float cx = gridPx + gridW * 0.5f;
        float cy = gridPy + gridH * 0.5f - 20.0f * scale;

        canvas.drawText(title, cx, cy - 10.0f * scale, t);

        t.setTextSize(16.0f * scale);
        t.setColor(Color(180, 190, 220));
        canvas.drawText(sub, cx, cy + 30.0f * scale, t);

        t.setTextAlign(Paint::TextAlign::LEFT);
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

    Canvas::initialize();
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

    Canvas::finalize();
    glfwTerminate();
    std::cout << "Final score: " << game.getScore() << std::endl;
    return 0;
}
