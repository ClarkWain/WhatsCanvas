#include <wsc/FontSystem.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>

#include "wsc/wsc.h"

using namespace wsc;

namespace {

constexpr float DESIGN_W = 1280.0f;
constexpr float DESIGN_H = 860.0f;
constexpr float CARD_W = 98.0f;
constexpr float CARD_H = 136.0f;
constexpr float COL_X = 34.0f;
constexpr float COL_GAP = 124.0f;
constexpr float TABLE_Y = 176.0f;
constexpr float TABLE_BOTTOM = 814.0f;
constexpr unsigned int kOpenGLMultisample = 0x809D;

enum class Suit : int { Spade = 0, Heart = 1, Club = 2, Diamond = 3 };

struct Card {
    int rank = 1;
    Suit suit = Suit::Spade;
    bool faceUp = false;
    std::uint32_t id = 0;
};

struct Snapshot {
    std::array<std::vector<Card>, 10> columns;
    std::vector<Card> stock;
    std::vector<Suit> completed;
    int moves = 0;
    int score = 500;
    float elapsed = 0.0f;
};

struct HitCard {
    int column = -1;
    int index = -1;
};

struct CardMotion {
    Card card;
    float fromX = 0.0f;
    float fromY = 0.0f;
    float toX = 0.0f;
    float toY = 0.0f;
    float delay = 0.0f;
    float duration = 0.2f;
};

float clamp01(float value) { return std::max(0.0f, std::min(1.0f, value)); }

float easeOutQuint(float value) {
    const float t = clamp01(value);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv * inv * inv;
}

float easeInOutCubic(float value) {
    const float t = clamp01(value);
    return t < 0.5f ? 4.0f * t * t * t
                    : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}

Color mix(const Color& a, const Color& b, float t, int alpha = -1) {
    t = clamp01(t);
    auto c = [t](int x, int y) { return static_cast<int>(std::lround(x + (y - x) * t)); };
    return Color(c(a.getR(), b.getR()), c(a.getG(), b.getG()), c(a.getB(), b.getB()),
                 alpha >= 0 ? alpha : c(a.getA(), b.getA()));
}

void useUiFont(Paint& paint, int weight = 500) {
    paint.setFont(FontSystem::kDefaultPrimaryFamily);
    paint.setFontWeight(weight);
}

std::string rankText(int rank) {
    if (rank == 1) return "A";
    if (rank == 11) return "J";
    if (rank == 12) return "Q";
    if (rank == 13) return "K";
    return std::to_string(rank);
}

std::string formatTime(float seconds) {
    int total = std::max(0, static_cast<int>(seconds));
    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << total / 60 << ':' << std::setw(2) << total % 60;
    return out.str();
}

bool inside(const RectF& r, float x, float y) {
    return x >= r.getX() && x <= r.getX() + r.getWidth() &&
           y >= r.getY() && y <= r.getY() + r.getHeight();
}

class SpiderGame {
public:
    explicit SpiderGame(int difficulty = 1, std::uint32_t seed = 0)
        : difficulty_(difficulty), requestedSeed_(seed) {
        newGame(seed, false);
    }

    void newGame(std::uint32_t seed = 0, bool animate = true) {
        if (seed == 0) {
            seed = requestedSeed_ != 0 ? requestedSeed_ : static_cast<std::uint32_t>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count());
        }
        currentSeed_ = seed;
        std::mt19937 rng(seed);
        std::vector<Card> deck;
        deck.reserve(104);
        std::uint32_t id = 1;
        if (difficulty_ == 1) {
            for (int copy = 0; copy < 8; ++copy)
                for (int rank = 1; rank <= 13; ++rank)
                    deck.push_back({rank, Suit::Spade, false, id++});
        } else if (difficulty_ == 2) {
            for (int copy = 0; copy < 4; ++copy)
                for (Suit suit : {Suit::Spade, Suit::Heart})
                    for (int rank = 1; rank <= 13; ++rank)
                        deck.push_back({rank, suit, false, id++});
        } else {
            for (int copy = 0; copy < 2; ++copy)
                for (Suit suit : {Suit::Spade, Suit::Heart, Suit::Club, Suit::Diamond})
                    for (int rank = 1; rank <= 13; ++rank)
                        deck.push_back({rank, suit, false, id++});
        }
        std::shuffle(deck.begin(), deck.end(), rng);
        for (auto& column : columns_) column.clear();
        stock_.clear();
        completed_.clear();
        history_.clear();
        selectedColumn_ = selectedIndex_ = -1;
        hintSource_ = hintDest_ = -1;
        dragging_ = false;
        won_ = false;
        moves_ = 0;
        score_ = 500;
        elapsed_ = 0.0f;
        motions_.clear();
        completionMotions_.clear();
        completionMotionTime_ = 0.0f;
        setToast("Build suited runs from King down to Ace", 3.8f);
        tableauPictureDirty_ = true;

        for (int row = 0; row < 6; ++row) {
            for (int col = 0; col < 10; ++col) {
                const int target = col < 4 ? 6 : 5;
                if (row >= target) continue;
                columns_[col].push_back(deck.back());
                deck.pop_back();
            }
        }
        for (auto& column : columns_) column.back().faceUp = true;
        stock_ = std::move(deck);
        if (animate) startIntroAnimation();
        std::cout << "NEW_GAME seed=" << currentSeed_ << " suits=" << difficulty_ << '\n';
    }

    void startIntroAnimation() {
        std::vector<CardMotion> motions;
        const RectF stock = stockRect();
        const float originX = stock.getX() + stock.getWidth() * 0.5f - CARD_W * 0.5f;
        const float originY = stock.getY() + stock.getHeight() * 0.35f - CARD_H * 0.5f;
        for (int col = 0; col < 10; ++col) {
            if (columns_[col].empty()) continue;
            const int index = static_cast<int>(columns_[col].size()) - 1;
            const auto ys = columnPositions(col);
            motions.push_back({columns_[col][index], originX, originY,
                               columnX(col), ys[index], col * 0.025f, 0.24f});
        }
        startMotions(std::move(motions));
    }

    void startCompletionDemo(bool finalRun = false) {
        for (auto& column : columns_) column.clear();
        stock_.clear();
        completed_.clear();
        if (finalRun) completed_.assign(7, Suit::Spade);
        history_.clear();
        motions_.clear();
        completionMotions_.clear();
        completionMotionTime_ = 0.0f;
        won_ = false;
        for (int rank = 13; rank >= 1; --rank)
            columns_[4].push_back({rank, Suit::Spade, true,
                                   static_cast<std::uint32_t>(500 + rank)});
        collectCompleteRuns();
        tableauPictureDirty_ = true;
    }

    void update(float dt) {
        if (!won_) elapsed_ += std::min(std::max(dt, 0.0f), 0.1f);
        phase_ += dt;
        toastTime_ = std::max(0.0f, toastTime_ - dt);
        hintTime_ = std::max(0.0f, hintTime_ - dt);
        dealPulse_ = std::max(0.0f, dealPulse_ - dt * 2.4f);
        buttonPressTime_ = std::max(0.0f, buttonPressTime_ - dt);
        if (!motions_.empty()) {
            motionTime_ += dt;
            float end = 0.0f;
            for (const CardMotion& motion : motions_)
                end = std::max(end, motion.delay + motion.duration);
            if (motionTime_ >= end) finishMotions();
        }
        if (!completionMotions_.empty()) {
            completionMotionTime_ += dt;
            float end = 0.0f;
            for (const CardMotion& motion : completionMotions_)
                end = std::max(end, motion.delay + motion.duration);
            if (completionMotionTime_ >= end) {
                completionMotions_.clear();
                completionMotionTime_ = 0.0f;
            }
        }
    }

    void setDifficulty(int suits, bool animate = true) {
        if (suits != 1 && suits != 2 && suits != 4) return;
        difficulty_ = suits;
        requestedSeed_ = 0;
        newGame(0, animate);
    }

    void cycleDifficulty(bool animate = true) {
        setDifficulty(difficulty_ == 1 ? 2 : difficulty_ == 2 ? 4 : 1, animate);
    }

    bool movableRun(int col, int index) const {
        if (col < 0 || col >= 10 || index < 0 || index >= static_cast<int>(columns_[col].size())) return false;
        const auto& cards = columns_[col];
        if (!cards[index].faceUp) return false;
        for (int i = index; i + 1 < static_cast<int>(cards.size()); ++i) {
            if (!cards[i + 1].faceUp || cards[i].suit != cards[i + 1].suit ||
                cards[i].rank != cards[i + 1].rank + 1) return false;
        }
        return true;
    }

    bool canMove(int src, int index, int dest) const {
        if (src == dest || dest < 0 || dest >= 10 || !movableRun(src, index)) return false;
        const Card& first = columns_[src][index];
        return columns_[dest].empty() || columns_[dest].back().rank == first.rank + 1;
    }

    bool moveRun(int src, int index, int dest) {
        if (!canMove(src, index, dest)) return false;
        finishMotions();
        std::vector<CardMotion> motions;
        const auto sourceYs = columnPositions(src);
        const float sourceX = dragging_ ? pointerX_ - dragOffsetX_ : columnX(src);
        const float sourceY = dragging_ ? pointerY_ - dragOffsetY_ : sourceYs[index];
        for (int i = index; i < static_cast<int>(columns_[src].size()); ++i) {
            motions.push_back({columns_[src][i], sourceX,
                               sourceY + (sourceYs[i] - sourceYs[index]), 0, 0, 0, 0.2f});
        }
        saveUndo();
        auto& from = columns_[src];
        auto& to = columns_[dest];
        const int count = static_cast<int>(from.size()) - index;
        to.insert(to.end(), from.begin() + index, from.end());
        from.erase(from.begin() + index, from.end());
        bool revealed = false;
        if (!from.empty() && !from.back().faceUp) {
            from.back().faceUp = true;
            revealed = true;
        }
        ++moves_;
        score_ = std::max(0, score_ - 1);
        const int before = static_cast<int>(completed_.size());
        collectCompleteRuns();
        for (CardMotion& motion : motions) {
            if (!findColumnPosition(motion.card.id, motion.toX, motion.toY)) motion.duration = 0.0f;
        }
        motions.erase(std::remove_if(motions.begin(), motions.end(),
                                     [](const CardMotion& motion) { return motion.duration <= 0.0f; }),
                      motions.end());
        startMotions(std::move(motions));
        tableauPictureDirty_ = true;
        selectedColumn_ = selectedIndex_ = -1;
        hintTime_ = 0.0f;
        if (static_cast<int>(completed_.size()) > before)
            setToast("Run complete  +100", 2.5f);
        else
            setToast(revealed ? "A hidden card was revealed" :
                     "Moved " + std::to_string(count) + (count == 1 ? " card" : " cards"), 1.5f);
        std::cout << "MOVE c" << src + 1 << ':' << index << " -> c" << dest + 1
                  << " count=" << count << " runs=" << completed_.size() << '\n';
        return true;
    }

    bool dealStock() {
        finishMotions();
        if (stock_.empty()) {
            setToast("No stock cards remain", 1.8f);
            return false;
        }
        for (const auto& column : columns_) {
            if (column.empty()) {
                setToast("Fill every empty column before dealing", 2.8f);
                return false;
            }
        }
        if (stock_.size() < 10) return false;
        saveUndo();
        std::vector<CardMotion> motions;
        const RectF stockBounds = stockRect();
        const float originX = stockBounds.getX() + stockBounds.getWidth() * 0.5f - CARD_W * 0.5f;
        const float originY = stockBounds.getY() + stockBounds.getHeight() * 0.35f - CARD_H * 0.5f;
        for (int col = 0; col < 10; ++col) {
            Card card = stock_.back();
            stock_.pop_back();
            card.faceUp = true;
            columns_[col].push_back(card);
            const auto ys = columnPositions(col);
            motions.push_back({card, originX, originY, columnX(col), ys.back(),
                               col * 0.018f, 0.22f});
        }
        ++moves_;
        score_ = std::max(0, score_ - 1);
        selectedColumn_ = selectedIndex_ = -1;
        collectCompleteRuns();
        startMotions(std::move(motions));
        tableauPictureDirty_ = true;
        dealPulse_ = 1.0f;
        setToast("A new row was dealt", 1.7f);
        std::cout << "DEAL remaining=" << stock_.size() << '\n';
        return true;
    }

    bool undo() {
        finishMotions();
        if (history_.empty()) {
            setToast("Nothing to undo", 1.6f);
            return false;
        }
        const Snapshot state = history_.back();
        history_.pop_back();
        columns_ = state.columns;
        stock_ = state.stock;
        completed_ = state.completed;
        completionMotions_.clear();
        completionMotionTime_ = 0.0f;
        moves_ = state.moves;
        score_ = state.score;
        elapsed_ = state.elapsed;
        won_ = false;
        selectedColumn_ = selectedIndex_ = -1;
        dragging_ = false;
        tableauPictureDirty_ = true;
        setToast("Move undone", 1.5f);
        std::cout << "UNDO history=" << history_.size() << '\n';
        return true;
    }

    void hint() {
        hintSource_ = hintDest_ = hintIndex_ = -1;
        int bestScore = -100000;
        for (int src = 0; src < 10; ++src) {
            for (int i = 0; i < static_cast<int>(columns_[src].size()); ++i) {
                if (!movableRun(src, i)) continue;
                for (int dest = 0; dest < 10; ++dest) {
                    if (!canMove(src, i, dest)) continue;
                    int value = columns_[dest].empty() ? 5 : 20;
                    if (!columns_[dest].empty() && columns_[dest].back().suit == columns_[src][i].suit) value += 40;
                    if (i > 0 && !columns_[src][i - 1].faceUp) value += 80;
                    if (columns_[src][i].rank == 13 && columns_[dest].empty()) value += 10;
                    if (value > bestScore) {
                        bestScore = value;
                        hintSource_ = src;
                        hintDest_ = dest;
                        hintIndex_ = i;
                    }
                }
            }
        }
        hintTime_ = 3.2f;
        if (hintSource_ >= 0) {
            setToast("Try the highlighted move", 2.2f);
        } else if (!stock_.empty()) {
            setToast("No useful moves - deal the next row", 2.2f);
            hintDest_ = 10;
        } else {
            setToast("No legal moves remain", 2.2f);
        }
    }

    void pointerMove(float x, float y) {
        pointerX_ = x;
        pointerY_ = y;
        if (mouseDown_ && selectedColumn_ >= 0) {
            const float dx = x - pressX_;
            const float dy = y - pressY_;
            if (!dragging_ && dx * dx + dy * dy > 30.0f) {
                dragging_ = true;
                tableauPictureDirty_ = true;
            }
        }
    }

    void pointerDown(float x, float y) {
        finishMotions();
        pointerMove(x, y);
        mouseDown_ = true;
        pressX_ = x;
        pressY_ = y;
        dragging_ = false;

        if (inside(newButton(), x, y)) { pressButton(1); newGame(0, true); mouseDown_ = false; return; }
        if (inside(difficultyButton(), x, y)) { pressButton(2); cycleDifficulty(true); mouseDown_ = false; return; }
        if (inside(undoButton(), x, y)) { pressButton(3); undo(); mouseDown_ = false; return; }
        if (inside(hintButton(), x, y)) { pressButton(4); hint(); mouseDown_ = false; return; }
        if (inside(stockRect(), x, y)) { pressButton(5); dealStock(); mouseDown_ = false; return; }
        if (won_) { newGame(0, true); mouseDown_ = false; return; }

        HitCard hit = hitCard(x, y);
        if (hit.column >= 0 && hit.index >= 0) {
            if (selectedColumn_ >= 0 && hit.column != selectedColumn_ &&
                canMove(selectedColumn_, selectedIndex_, hit.column)) {
                moveRun(selectedColumn_, selectedIndex_, hit.column);
                mouseDown_ = false;
                return;
            }
            if (movableRun(hit.column, hit.index)) {
                selectedColumn_ = hit.column;
                selectedIndex_ = hit.index;
                tableauPictureDirty_ = true;
                dragOffsetX_ = x - columnX(hit.column);
                dragOffsetY_ = y - columnPositions(hit.column)[hit.index];
            } else {
                selectedColumn_ = selectedIndex_ = -1;
                tableauPictureDirty_ = true;
                setToast("Only a same-suit descending run can move", 2.0f);
            }
            return;
        }

        const int empty = hitColumn(x, y);
        if (empty >= 0 && selectedColumn_ >= 0 && canMove(selectedColumn_, selectedIndex_, empty)) {
            moveRun(selectedColumn_, selectedIndex_, empty);
            mouseDown_ = false;
        } else {
            selectedColumn_ = selectedIndex_ = -1;
            tableauPictureDirty_ = true;
        }
    }

    void pointerUp(float x, float y) {
        pointerMove(x, y);
        if (mouseDown_ && dragging_ && selectedColumn_ >= 0) {
            const int dest = hitColumn(x, y);
            if (dest >= 0 && dest != selectedColumn_ && moveRun(selectedColumn_, selectedIndex_, dest)) {
                // moved
            } else {
                std::vector<CardMotion> motions;
                const auto ys = columnPositions(selectedColumn_);
                const float fromX = pointerX_ - dragOffsetX_;
                const float fromY = pointerY_ - dragOffsetY_;
                for (int i = selectedIndex_; i < static_cast<int>(columns_[selectedColumn_].size()); ++i) {
                    motions.push_back({columns_[selectedColumn_][i], fromX,
                                       fromY + (ys[i] - ys[selectedIndex_]),
                                       columnX(selectedColumn_), ys[i], 0.0f, 0.16f});
                }
                startMotions(std::move(motions));
                setToast("That run cannot be placed there", 1.8f);
            }
        }
        mouseDown_ = false;
        dragging_ = false;
        tableauPictureDirty_ = true;
    }

    void cancelSelection() {
        finishMotions();
        selectedColumn_ = selectedIndex_ = -1;
        mouseDown_ = dragging_ = false;
        tableauPictureDirty_ = true;
    }

    void updateRenderTransform(int windowW, int windowH) {
        const float scale = std::min(windowW / DESIGN_W, windowH / DESIGN_H);
        const float ox = (windowW - DESIGN_W * scale) * 0.5f;
        const float oy = (windowH - DESIGN_H * scale) * 0.5f;
        renderScale_ = scale;
        renderOffsetX_ = ox;
        renderOffsetY_ = oy;
    }

    void render(Canvas& canvas, int windowW, int windowH) {
        updateRenderTransform(windowW, windowH);
        const float scale = renderScale_;
        const float ox = renderOffsetX_;
        const float oy = renderOffsetY_;

        drawOutside(canvas, windowW, windowH);
        canvas.save();
        canvas.translate(ox, oy);
        canvas.scale(scale, scale);
        if (!tablePicture_) {
            tablePicture_ = canvas.recordPicture([this](Canvas& recording) {
                drawTable(recording);
            });
        }
        if (tablePicture_) canvas.drawPictureRasterized(*tablePicture_);
        else drawTable(canvas);
        drawHeader(canvas);
        drawColumns(canvas);
        drawStock(canvas);
        drawToast(canvas);
        if (won_ && completionMotions_.empty()) drawWin(canvas);
        canvas.restore();
    }

    std::pair<float, float> toDesign(float windowX, float windowY) const {
        return {(windowX - renderOffsetX_) / std::max(renderScale_, 0.0001f),
                (windowY - renderOffsetY_) / std::max(renderScale_, 0.0001f)};
    }

    std::pair<float, float> toWindow(float designX, float designY) const {
        return {renderOffsetX_ + designX * renderScale_,
                renderOffsetY_ + designY * renderScale_};
    }

    bool difficultyHoverMatches() const {
        return inside(difficultyButton(), pointerX_, pointerY_) &&
               !inside(hintButton(), pointerX_, pointerY_);
    }

    static bool runSelfTests() {
        int checks = 0;
        auto expect = [&checks](bool condition, const char* label) {
            ++checks;
            if (!condition) std::cerr << "SELF_TEST FAIL: " << label << '\n';
            return condition;
        };
        bool ok = true;
        SpiderGame game(1, 424242);
        int total = static_cast<int>(game.stock_.size());
        for (const auto& col : game.columns_) total += static_cast<int>(col.size());
        ok &= expect(total == 104, "deck contains 104 cards");
        ok &= expect(game.stock_.size() == 50, "initial stock contains 50 cards");
        for (int i = 0; i < 10; ++i) {
            ok &= expect(game.columns_[i].size() == static_cast<size_t>(i < 4 ? 6 : 5), "initial tableau size");
            ok &= expect(game.columns_[i].back().faceUp, "top card face up");
        }

        for (auto& col : game.columns_) col.clear();
        game.stock_.clear();
        game.completed_.clear();
        game.history_.clear();
        game.moves_ = 0;
        game.score_ = 500;
        game.columns_[0] = {{8, Suit::Spade, true, 1}, {7, Suit::Spade, true, 2}, {6, Suit::Spade, true, 3}};
        game.columns_[1] = {{9, Suit::Heart, true, 4}};
        ok &= expect(game.movableRun(0, 0), "suited descending run is movable");
        ok &= expect(game.canMove(0, 0, 1), "run can land on next rank regardless of suit");
        ok &= expect(game.moveRun(0, 0, 1), "valid run moves");
        ok &= expect(game.columns_[1].size() == 4 && game.columns_[0].empty(), "move changes columns");
        ok &= expect(game.undo(), "move can be undone");
        ok &= expect(game.columns_[0].size() == 3 && game.columns_[1].size() == 1, "undo restores tableau");

        game.columns_[0] = {{8, Suit::Spade, true, 10}, {7, Suit::Heart, true, 11}};
        ok &= expect(!game.movableRun(0, 0), "mixed-suit run is not movable");
        ok &= expect(game.movableRun(0, 1), "single face-up card is movable");
        ok &= expect(game.canMove(0, 1, 2), "card can move to empty column");

        for (auto& col : game.columns_) col.clear();
        game.columns_[0].push_back({4, Suit::Heart, false, 20});
        for (int rank = 13; rank >= 1; --rank)
            game.columns_[0].push_back({rank, Suit::Spade, true, static_cast<std::uint32_t>(30 + rank)});
        game.collectCompleteRuns();
        ok &= expect(game.completed_.size() == 1, "K-to-A suited run is collected");
        ok &= expect(game.columns_[0].size() == 1 && game.columns_[0].back().faceUp, "collection reveals hidden card");
        ok &= expect(game.completionMotions_.size() == 13, "completed run creates thirteen collection motions");
        game.update(1.0f);
        ok &= expect(game.completionMotions_.empty(), "collection motions finish and clear");

        game.completed_.assign(7, Suit::Spade);
        game.completionMotions_.clear();
        game.completionMotionTime_ = 0.0f;
        for (auto& col : game.columns_) col.clear();
        for (int rank = 13; rank >= 1; --rank)
            game.columns_[4].push_back({rank, Suit::Spade, true, static_cast<std::uint32_t>(300 + rank)});
        game.won_ = false;
        game.collectCompleteRuns();
        ok &= expect(game.completed_.size() == 8 && game.won_, "eighth run enters the win state");

        for (int i = 0; i < 10; ++i) game.columns_[i] = {{5, Suit::Spade, true, static_cast<std::uint32_t>(100 + i)}};
        game.columns_[3].clear();
        game.stock_.assign(10, {2, Suit::Spade, false, 200});
        ok &= expect(!game.dealStock(), "deal rejected while a column is empty");
        game.columns_[3] = {{6, Suit::Spade, true, 203}};
        ok &= expect(game.dealStock(), "deal accepted when all columns are filled");
        ok &= expect(game.stock_.empty(), "deal consumes ten stock cards");
        for (const auto& col : game.columns_) ok &= expect(col.back().faceUp, "dealt cards are face up");

        std::cout << "SELF_TEST " << (ok ? "PASS" : "FAIL") << " checks=" << checks << '\n';
        return ok;
    }

    static bool runInteractionTests() {
        int checks = 0;
        auto expect = [&checks](bool condition, const char* label) {
            ++checks;
            if (!condition) std::cerr << "PLAY_TEST FAIL: " << label << '\n';
            return condition;
        };
        bool ok = true;
        SpiderGame game(1, 424242);

        // Craft a small deterministic board, then exercise the same pointer entry
        // points used by GLFW rather than calling moveRun directly.
        for (auto& col : game.columns_) col.clear();
        game.stock_.assign(20, {4, Suit::Spade, false, 900});
        game.columns_[0] = {{8, Suit::Spade, true, 1}, {7, Suit::Spade, true, 2}, {6, Suit::Spade, true, 3}};
        game.columns_[1] = {{9, Suit::Heart, true, 4}};
        for (int i = 2; i < 10; ++i) game.columns_[i] = {{12, Suit::Spade, true, static_cast<std::uint32_t>(20 + i)}};
        const auto sourceY = game.columnPositions(0);
        const auto destY = game.columnPositions(1);
        game.pointerDown(game.columnX(0) + 25, sourceY[0] + 8);
        game.pointerMove(game.columnX(1) + 25, destY.back() + 50);
        game.pointerUp(game.columnX(1) + 25, destY.back() + 50);
        ok &= expect(game.columns_[0].empty() && game.columns_[1].size() == 4, "drag moves a legal suited run");
        ok &= expect(game.moves_ == 1, "drag increments move counter");
        ok &= expect(game.motions_.size() == 3, "legal drag creates one landing motion per moved card");

        const RectF undo = game.undoButton();
        game.pointerDown(undo.getX() + undo.getWidth() * 0.5f, undo.getY() + 10);
        ok &= expect(game.columns_[0].size() == 3 && game.columns_[1].size() == 1, "Undo button restores drag");

        // Select by click and place by click.
        game.pointerDown(game.columnX(0) + 20, sourceY[0] + 8);
        game.pointerUp(game.columnX(0) + 20, sourceY[0] + 8);
        game.pointerDown(game.columnX(1) + 20, destY[0] + 20);
        ok &= expect(game.columns_[1].size() == 4, "click-select then click-destination moves run");
        game.undo();

        // Drag to a rank that cannot accept the run.
        const size_t beforeInvalid = game.columns_[0].size();
        game.pointerDown(game.columnX(0) + 20, sourceY[0] + 8);
        game.pointerMove(game.columnX(2) + 20, TABLE_Y + 70);
        game.pointerUp(game.columnX(2) + 20, TABLE_Y + 70);
        ok &= expect(game.columns_[0].size() == beforeInvalid, "illegal drag snaps back without mutation");
        ok &= expect(game.motions_.size() == 3, "illegal drag creates a short snap-back motion");

        const size_t stockBefore = game.stock_.size();
        const RectF stock = game.stockRect();
        game.pointerDown(stock.getX() + 20, stock.getY() + 20);
        ok &= expect(game.stock_.size() == stockBefore - 10, "stock control deals through pointer handler");
        ok &= expect(game.moves_ == 1, "deal increments move counter");
        ok &= expect(game.motions_.size() == 10, "stock deal staggers ten card motions");

        game.undo();
        game.columns_[5].clear();
        const size_t rejectedStock = game.stock_.size();
        game.pointerDown(stock.getX() + 20, stock.getY() + 20);
        ok &= expect(game.stock_.size() == rejectedStock, "stock click is rejected with an empty column");

        const RectF difficulty = game.difficultyButton();
        game.pointerDown(difficulty.getX() + 20, difficulty.getY() + 20);
        ok &= expect(game.difficulty_ == 2 && game.stock_.size() == 50, "difficulty button starts a valid two-suit deal");
        ok &= expect(game.motions_.size() == 10, "pointer-started new deal animates visible cards");

        const RectF hint = game.hintButton();
        game.pointerDown(hint.getX() + 20, hint.getY() + 20);
        ok &= expect(game.hintTime_ > 0, "Hint button produces visible guidance");

        // GLFW cursor positions use window-content coordinates. Verify the inverse
        // render transform stays exact for both scaled and letterboxed windows.
        const auto verifyButtonMapping = [&game, &expect](int windowW, int windowH,
                                                          const RectF& target,
                                                          const RectF& neighbor) {
            game.updateRenderTransform(windowW, windowH);
            const float designX = target.getX() + target.getWidth() * 0.5f;
            const float designY = target.getY() + target.getHeight() * 0.5f;
            const float windowX = game.renderOffsetX_ + designX * game.renderScale_;
            const float windowY = game.renderOffsetY_ + designY * game.renderScale_;
            const auto mapped = game.toDesign(windowX, windowY);
            return expect(std::abs(mapped.first - designX) < 0.01f &&
                          std::abs(mapped.second - designY) < 0.01f,
                          "window cursor maps back to button center") &&
                   expect(inside(target, mapped.first, mapped.second) &&
                          !inside(neighbor, mapped.first, mapped.second),
                          "mapped cursor highlights only the intended button");
        };
        ok &= verifyButtonMapping(1280, 860, game.difficultyButton(), game.hintButton());
        ok &= verifyButtonMapping(960, 720, game.difficultyButton(), game.hintButton());
        ok &= verifyButtonMapping(1600, 900, game.difficultyButton(), game.hintButton());

        std::cout << "PLAY_TEST " << (ok ? "PASS" : "FAIL") << " checks=" << checks << '\n';
        return ok;
    }

private:
    std::array<std::vector<Card>, 10> columns_;
    std::vector<Card> stock_;
    std::vector<Suit> completed_;
    std::vector<Snapshot> history_;
    int difficulty_ = 1;
    std::uint32_t requestedSeed_ = 0;
    std::uint32_t currentSeed_ = 0;
    int moves_ = 0;
    int score_ = 500;
    float elapsed_ = 0.0f;
    bool won_ = false;

    int selectedColumn_ = -1;
    int selectedIndex_ = -1;
    int hintSource_ = -1;
    int hintDest_ = -1;
    int hintIndex_ = -1;
    bool mouseDown_ = false;
    bool dragging_ = false;
    float pointerX_ = 0.0f;
    float pointerY_ = 0.0f;
    float pressX_ = 0.0f;
    float pressY_ = 0.0f;
    float dragOffsetX_ = 0.0f;
    float dragOffsetY_ = 0.0f;
    float phase_ = 0.0f;
    float toastTime_ = 0.0f;
    float toastDuration_ = 0.0f;
    float hintTime_ = 0.0f;
    float dealPulse_ = 0.0f;
    float buttonPressTime_ = 0.0f;
    int pressedButton_ = 0;
    std::vector<CardMotion> motions_;
    float motionTime_ = 0.0f;
    std::vector<CardMotion> completionMotions_;
    float completionMotionTime_ = 0.0f;
    std::string toast_;
    std::string measuredToast_;
    float measuredToastWidth_ = 0.0f;
    std::shared_ptr<const Picture> tablePicture_;
    std::shared_ptr<const Picture> tableauPicture_;
    bool tableauPictureDirty_ = true;
    float renderScale_ = 1.0f;
    float renderOffsetX_ = 0.0f;
    float renderOffsetY_ = 0.0f;

    float columnX(int col) const { return COL_X + col * COL_GAP; }
    RectF newButton() const { return RectF(760, 28, 116, 44); }
    RectF difficultyButton() const { return RectF(884, 28, 112, 44); }
    RectF undoButton() const { return RectF(1004, 28, 98, 44); }
    RectF hintButton() const { return RectF(1110, 28, 104, 44); }
    RectF stockRect() const { return RectF(1148, 676, 100, 148); }

    bool findColumnPosition(std::uint32_t id, float& x, float& y) const {
        for (int col = 0; col < 10; ++col) {
            const auto ys = columnPositions(col);
            for (int i = 0; i < static_cast<int>(columns_[col].size()); ++i) {
                if (columns_[col][i].id == id) {
                    x = columnX(col);
                    y = ys[i];
                    return true;
                }
            }
        }
        return false;
    }

    bool isCardAnimating(std::uint32_t id) const {
        return std::any_of(motions_.begin(), motions_.end(),
                           [id](const CardMotion& motion) { return motion.card.id == id; });
    }

    void finishMotions() {
        if (motions_.empty()) return;
        motions_.clear();
        motionTime_ = 0.0f;
        tableauPictureDirty_ = true;
    }

    void startMotions(std::vector<CardMotion> motions) {
        finishMotions();
        motions_ = std::move(motions);
        motionTime_ = 0.0f;
        if (!motions_.empty()) tableauPictureDirty_ = true;
    }

    void pressButton(int id) {
        pressedButton_ = id;
        buttonPressTime_ = 0.13f;
    }

    void saveUndo() {
        history_.push_back({columns_, stock_, completed_, moves_, score_, elapsed_});
        if (history_.size() > 100) history_.erase(history_.begin());
    }

    void setToast(std::string text, float time) {
        toast_ = std::move(text);
        const float readableTime = std::max(time, 3.4f);
        toastTime_ = readableTime;
        toastDuration_ = readableTime;
    }

    void queueCompletionAnimation(int column, size_t start, const std::vector<float>& ys,
                                  int completedSlot) {
        float baseDelay = 0.0f;
        for (const CardMotion& motion : completionMotions_)
            baseDelay = std::max(baseDelay, motion.delay + motion.duration);
        baseDelay = std::max(0.0f, baseDelay - completionMotionTime_);
        if (completionMotions_.empty()) completionMotionTime_ = 0.0f;

        const float targetX = 146.0f + completedSlot * 42.0f + 16.0f;
        const float targetY = 139.0f;
        const auto& col = columns_[column];
        for (int i = 0; i < 13; ++i) {
            const size_t index = start + static_cast<size_t>(i);
            completionMotions_.push_back({col[index], columnX(column), ys[index],
                                          targetX, targetY,
                                          baseDelay + i * 0.018f, 0.28f});
        }
    }

    void collectCompleteRuns() {
        bool collectedAny;
        do {
            collectedAny = false;
            for (int column = 0; column < 10; ++column) {
                auto& col = columns_[column];
                if (col.size() < 13) continue;
                const size_t start = col.size() - 13;
                const Suit suit = col[start].suit;
                bool complete = true;
                for (int i = 0; i < 13; ++i) {
                    const Card& card = col[start + i];
                    if (!card.faceUp || card.suit != suit || card.rank != 13 - i) {
                        complete = false;
                        break;
                    }
                }
                if (!complete) continue;
                const auto ys = columnPositions(column);
                queueCompletionAnimation(column, start, ys, static_cast<int>(completed_.size()));
                col.erase(col.begin() + static_cast<std::ptrdiff_t>(start), col.end());
                completed_.push_back(suit);
                score_ += 100;
                if (!col.empty() && !col.back().faceUp) col.back().faceUp = true;
                setToast("Complete run collected!", 2.7f);
                collectedAny = true;
                if (completed_.size() == 8) {
                    won_ = true;
                    score_ += std::max(0, 1000 - static_cast<int>(elapsed_));
                    setToast("All eight runs completed", 4.0f);
                }
                tableauPictureDirty_ = true;
                break;
            }
        } while (collectedAny);
    }

    std::vector<float> columnPositions(int col) const {
        std::vector<float> y;
        const auto& cards = columns_[col];
        y.resize(cards.size(), TABLE_Y);
        if (cards.empty()) return y;
        float raw = 0.0f;
        for (size_t i = 0; i + 1 < cards.size(); ++i) raw += cards[i].faceUp ? 29.0f : 16.0f;
        const float available = TABLE_BOTTOM - TABLE_Y - CARD_H;
        const float factor = raw > available && raw > 0.0f ? available / raw : 1.0f;
        float cursor = TABLE_Y;
        for (size_t i = 0; i < cards.size(); ++i) {
            y[i] = cursor;
            if (i + 1 < cards.size()) cursor += (cards[i].faceUp ? 29.0f : 16.0f) * factor;
        }
        return y;
    }

    int hitColumn(float x, float y) const {
        if (y < TABLE_Y - 16.0f || y > TABLE_BOTTOM + 18.0f) return -1;
        for (int col = 0; col < 10; ++col)
            if (x >= columnX(col) - 8.0f && x <= columnX(col) + CARD_W + 8.0f) return col;
        return -1;
    }

    HitCard hitCard(float x, float y) const {
        const int col = hitColumn(x, y);
        if (col < 0 || columns_[col].empty()) return {};
        const auto ys = columnPositions(col);
        for (int i = static_cast<int>(ys.size()) - 1; i >= 0; --i) {
            const float bottom = i + 1 == static_cast<int>(ys.size()) ? ys[i] + CARD_H : ys[i + 1];
            if (y >= ys[i] && y <= bottom) return {col, i};
        }
        return {};
    }

    void drawOutside(Canvas& canvas, int width, int height) {
        Paint p;
        p.setStyle(Paint::Style::FILL);
        p.setColor(Color(5, 24, 18));
        canvas.drawRect(RectF(0, 0, static_cast<float>(width), static_cast<float>(height)), p);
    }

    void drawTable(Canvas& canvas) {
        Paint bg;
        bg.setStyle(Paint::Style::FILL);
        bg.setColor(Color(5, 38, 28));
        canvas.drawRect(RectF(0, 0, DESIGN_W, DESIGN_H), bg);

        Paint felt;
        felt.setStyle(Paint::Style::FILL);
        felt.setRadialGradient(650, 430, 790,
                              Color(27, 126, 83), Color(4, 42, 30));
        canvas.drawCircle(650, 430, 790, felt);

        Paint softLight;
        softLight.setStyle(Paint::Style::FILL);
        softLight.setRadialGradient(650, 390, 470,
                                   Color(105, 190, 132, 28), Color(16, 82, 56, 0));
        canvas.drawCircle(650, 390, 470, softLight);

        Paint line;
        line.setStyle(Paint::Style::STROKE);
        line.setStrokeWidth(1.0f);
        line.setColor(Color(208, 225, 192, 10));
        for (int i = -3; i < 15; ++i) {
            const float x = i * 110.0f;
            canvas.drawLine(x, 108.0f, x + 520.0f, DESIGN_H, line);
        }

        Paint header;
        header.setStyle(Paint::Style::FILL);
        header.setColor(Color(4, 29, 23, 238));
        canvas.drawRect(RectF(0, 0, DESIGN_W, 104), header);
        Paint divider;
        divider.setStyle(Paint::Style::FILL);
        divider.setLinearGradient(0, 0, DESIGN_W, 0,
                                  Color(193, 157, 98, 20), Color(222, 188, 126, 170));
        canvas.drawRect(RectF(0, 103, DESIGN_W, 1), divider);

        Paint rail;
        rail.setStyle(Paint::Style::FILL);
        rail.setColor(Color(3, 42, 30, 128));
        canvas.drawRoundRect(RectF(24, 116, 1232, 48), 16, rail);
    }

    void drawSpiderMark(Canvas& canvas, float cx, float cy, float s, const Color& color) const {
        Paint stroke;
        stroke.setStyle(Paint::Style::STROKE);
        stroke.setStrokeWidth(std::max(1.0f, s * 0.065f));
        stroke.setStrokeCap(Paint::StrokeCap::ROUND);
        stroke.setColor(color);
        for (int side : {-1, 1}) {
            const float d = static_cast<float>(side);
            Path front;
            front.moveTo(cx + d * s * 0.10f, cy - s * 0.20f);
            front.cubicTo(cx + d * s * 0.23f, cy - s * 0.30f,
                          cx + d * s * 0.35f, cy - s * 0.48f,
                          cx + d * s * 0.52f, cy - s * 0.62f);
            canvas.drawPath(front, stroke);

            Path upper;
            upper.moveTo(cx + d * s * 0.13f, cy - s * 0.08f);
            upper.cubicTo(cx + d * s * 0.28f, cy - s * 0.10f,
                          cx + d * s * 0.45f, cy - s * 0.26f,
                          cx + d * s * 0.65f, cy - s * 0.29f);
            canvas.drawPath(upper, stroke);

            Path lower;
            lower.moveTo(cx + d * s * 0.13f, cy + s * 0.06f);
            lower.cubicTo(cx + d * s * 0.29f, cy + s * 0.09f,
                          cx + d * s * 0.46f, cy + s * 0.23f,
                          cx + d * s * 0.64f, cy + s * 0.32f);
            canvas.drawPath(lower, stroke);

            Path rear;
            rear.moveTo(cx + d * s * 0.10f, cy + s * 0.18f);
            rear.cubicTo(cx + d * s * 0.22f, cy + s * 0.30f,
                         cx + d * s * 0.34f, cy + s * 0.49f,
                         cx + d * s * 0.50f, cy + s * 0.63f);
            canvas.drawPath(rear, stroke);
        }
        Paint body;
        body.setStyle(Paint::Style::FILL);
        body.setColor(color);
        canvas.drawOval(RectF(cx - s * 0.145f, cy - s * 0.02f, s * 0.29f, s * 0.43f), body);
        canvas.drawCircle(cx, cy - s * 0.14f, s * 0.13f, body);
        canvas.drawCircle(cx, cy - s * 0.31f, s * 0.075f, body);
    }

    void drawHeader(Canvas& canvas) {
        drawSpiderMark(canvas, 48, 51, 43, Color(218, 180, 111));
        Paint title;
        title.setStyle(Paint::Style::FILL);
        title.setColor(Color(248, 243, 230));
        title.setTextSize(25);
        title.setLetterSpacing(1.0f);
        useUiFont(title, 760);
        canvas.drawText("SPIDER", 82, 25, title);
        Paint sub = title;
        sub.setTextSize(11.5f);
        sub.setLetterSpacing(1.35f);
        sub.setColor(Color(218, 197, 153));
        canvas.drawText("SOLITAIRE  -  CLASSIC TABLE", 83, 58, sub);

        drawMetric(canvas, 260, "SCORE", std::to_string(score_));
        drawMetric(canvas, 375, "MOVES", std::to_string(moves_));
        drawMetric(canvas, 490, "TIME", formatTime(elapsed_));
        drawMetric(canvas, 605, "RUNS", std::to_string(completed_.size()) + "/8");

        drawButton(canvas, newButton(), "NEW DEAL", true, 1, true);
        drawButton(canvas, difficultyButton(), std::to_string(difficulty_) + " SUIT" + (difficulty_ > 1 ? "S" : ""), true, 2, false);
        drawButton(canvas, undoButton(), "UNDO", !history_.empty(), 3, false);
        drawButton(canvas, hintButton(), "HINT", true, 4, false);

        Paint runLabel;
        runLabel.setStyle(Paint::Style::FILL);
        runLabel.setColor(Color(225, 209, 174));
        runLabel.setTextSize(13.5f);
        runLabel.setLetterSpacing(1.0f);
        useUiFont(runLabel, 700);
        canvas.drawText("COMPLETED", 44, 133, runLabel);
        for (int i = 0; i < 8; ++i) {
            const float x = 146.0f + i * 42.0f;
            Paint slot;
            slot.setStyle(Paint::Style::FILL);
            slot.setColor(i < static_cast<int>(completed_.size()) ? Color(123, 50, 68, 170) : Color(19, 23, 38, 190));
            canvas.drawRoundRect(RectF(x, 124, 32, 30), 7, slot);
            Paint border;
            border.setStyle(Paint::Style::STROKE);
            border.setStrokeWidth(1);
            border.setColor(i < static_cast<int>(completed_.size()) ? Color(223, 184, 111, 210) : Color(139, 129, 112, 52));
            canvas.drawRoundRect(RectF(x, 124, 32, 30), 7, border);
            if (i < static_cast<int>(completed_.size()))
                drawSuit(canvas, completed_[i], x + 16, 139, 7.5f, Color(236, 209, 157));
        }

        Paint rule;
        rule.setStyle(Paint::Style::FILL);
        rule.setColor(Color(226, 218, 198));
        rule.setTextSize(13.5f);
        rule.setLetterSpacing(0.15f);
        useUiFont(rule, 580);
        canvas.drawText("Build same-suit runs from King to Ace", 760, 133, rule);
    }

    void drawMetric(Canvas& canvas, float x, const std::string& label, const std::string& value) {
        Paint labelPaint;
        labelPaint.setStyle(Paint::Style::FILL);
        labelPaint.setColor(Color(215, 199, 165));
        labelPaint.setTextSize(13.0f);
        labelPaint.setLetterSpacing(0.8f);
        useUiFont(labelPaint, 650);
        canvas.drawText(label, x, 27, labelPaint);
        Paint valuePaint = labelPaint;
        valuePaint.setColor(Color(247, 241, 227));
        valuePaint.setTextSize(21);
        valuePaint.setLetterSpacing(0);
        useUiFont(valuePaint, 700);
        canvas.drawText(value, x, 49, valuePaint);
    }

    void drawButton(Canvas& canvas, const RectF& rect, const std::string& label,
                    bool enabled, int id, bool primary) {
        const bool hover = enabled && inside(rect, pointerX_, pointerY_);
        const bool pressed = enabled && pressedButton_ == id && buttonPressTime_ > 0.0f;
        const float scale = pressed ? 0.97f : hover ? 1.018f : 1.0f;
        const float cx = rect.getX() + rect.getWidth() * 0.5f;
        const float cy = rect.getY() + rect.getHeight() * 0.5f;
        canvas.save();
        canvas.translate(cx, cy);
        canvas.scale(scale, scale);
        canvas.translate(-cx, -cy);
        Paint fill;
        fill.setStyle(Paint::Style::FILL);
        if (primary) {
            fill.setLinearGradient(rect.getX(), rect.getY(), rect.getX(), rect.getY() + rect.getHeight(),
                                   hover ? Color(242, 211, 151) : Color(222, 183, 112),
                                   hover ? Color(204, 157, 83) : Color(179, 132, 69));
        } else {
            fill.setColor(enabled ? (hover ? Color(45, 43, 54, 245) : Color(24, 27, 42, 230))
                                  : Color(17, 19, 30, 160));
        }
        canvas.drawRoundRect(rect, 10, fill);
        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeWidth(1);
        border.setColor(primary ? Color(248, 221, 166, hover ? 230 : 150)
                                : enabled ? Color(185, 159, 112, hover ? 190 : 82)
                                          : Color(103, 98, 91, 42));
        canvas.drawRoundRect(rect, 10, border);
        Paint text;
        text.setStyle(Paint::Style::FILL);
        text.setColor(primary ? Color(35, 27, 24) : enabled ? Color(238, 230, 212) : Color(102, 99, 96));
        text.setTextSize(14.0f);
        text.setLetterSpacing(0.45f);
        text.setTextAlign(Paint::TextAlign::CENTER);
        text.setTextBaseline(Paint::TextBaseline::MIDDLE);
        useUiFont(text, 700);
        canvas.drawText(label, rect.getX() + rect.getWidth() * 0.5f,
                        rect.getY() + rect.getHeight() * 0.5f, text);
        canvas.restore();
    }

    void drawTableauBase(Canvas& canvas) {
        const int dynamicCol = selectedColumn_;
        for (int col = 0; col < 10; ++col) {
            const auto ys = columnPositions(col);
            if (columns_[col].empty()) drawEmptySlot(canvas, col);
            for (int i = 0; i < static_cast<int>(columns_[col].size()); ++i) {
                if (col == dynamicCol && i >= selectedIndex_) continue;
                if (isCardAnimating(columns_[col][i].id)) continue;
                drawCard(canvas, columns_[col][i], columnX(col), ys[i], false, false, 1.0f);
            }
        }
    }

    void drawColumns(Canvas& canvas) {
        if (tableauPictureDirty_ || !tableauPicture_) {
            tableauPicture_ = canvas.recordPicture([this](Canvas& recording) {
                drawTableauBase(recording);
            });
            tableauPictureDirty_ = false;
        }
        if (tableauPicture_) canvas.drawPictureRasterized(*tableauPicture_);
        else drawTableauBase(canvas);

        if (selectedColumn_ >= 0) {
            const auto ys = columnPositions(selectedColumn_);
            const float x = dragging_ ? pointerX_ - dragOffsetX_ : columnX(selectedColumn_);
            const float y = dragging_ ? pointerY_ - dragOffsetY_ : ys[selectedIndex_];
            for (int i = selectedIndex_; i < static_cast<int>(columns_[selectedColumn_].size()); ++i) {
                if (isCardAnimating(columns_[selectedColumn_][i].id)) continue;
                drawCard(canvas, columns_[selectedColumn_][i], x,
                         y + (ys[i] - ys[selectedIndex_]), true, false,
                         dragging_ ? 0.96f : 1.0f);
            }
        }

        if (!dragging_ && hintTime_ > 0 && hintSource_ >= 0 && hintSource_ != selectedColumn_) {
            const auto ys = columnPositions(hintSource_);
            for (int i = hintIndex_; i < static_cast<int>(columns_[hintSource_].size()); ++i) {
                if (isCardAnimating(columns_[hintSource_][i].id)) continue;
                drawCard(canvas, columns_[hintSource_][i], columnX(hintSource_), ys[i], false, true, 1.0f);
            }
        }

        for (const CardMotion& motion : motions_) {
            const float local = (motionTime_ - motion.delay) / std::max(0.001f, motion.duration);
            const float t = easeOutQuint(local);
            const float x = motion.fromX + (motion.toX - motion.fromX) * t;
            const float y = motion.fromY + (motion.toY - motion.fromY) * t
                            - std::sin(clamp01(local) * 3.14159265f) * 14.0f;
            const float alpha = 0.82f + 0.18f * clamp01(local * 2.5f);
            drawMotionCard(canvas, motion.card, x, y, alpha);
        }

        for (const CardMotion& motion : completionMotions_) {
            const float local = (completionMotionTime_ - motion.delay) /
                                std::max(0.001f, motion.duration);
            const float progress = clamp01(local);
            const float t = easeInOutCubic(progress);
            const float startX = motion.fromX + CARD_W * 0.5f;
            const float startY = motion.fromY + CARD_H * 0.5f;
            const float centerX = startX + (motion.toX - startX) * t;
            const float centerY = startY + (motion.toY - startY) * t
                                  - std::sin(progress * 3.14159265f) * 24.0f;
            const float scale = 1.0f - 0.72f * t;
            const float alpha = 1.0f - clamp01((progress - 0.72f) / 0.28f);
            canvas.save();
            canvas.translate(centerX, centerY);
            canvas.scale(scale, scale);
            drawMotionCard(canvas, motion.card, -CARD_W * 0.5f, -CARD_H * 0.5f, alpha);
            canvas.restore();
        }

        if (hintTime_ > 0 && hintDest_ >= 0 && hintDest_ < 10) {
            Paint h;
            h.setStyle(Paint::Style::STROKE);
            h.setStrokeWidth(3);
            h.setColor(Color(255, 210, 91, static_cast<int>(150 + 80 * std::sin(phase_ * 5.0f))));
            const auto ys = columnPositions(hintDest_);
            const float y = ys.empty() ? TABLE_Y : ys.back();
            canvas.drawRoundRect(RectF(columnX(hintDest_) - 4, y - 4, CARD_W + 8, CARD_H + 8), 14, h);
        }
    }

    void drawEmptySlot(Canvas& canvas, int col) {
        const float x = columnX(col);
        Paint fill;
        fill.setStyle(Paint::Style::FILL);
        fill.setColor(Color(8, 11, 22, 92));
        canvas.drawRoundRect(RectF(x, TABLE_Y, CARD_W, CARD_H), 11, fill);
        Paint stroke;
        stroke.setStyle(Paint::Style::STROKE);
        stroke.setStrokeWidth(1.2f);
        stroke.setColor(Color(180, 158, 118, 52));
        canvas.drawRoundRect(RectF(x, TABLE_Y, CARD_W, CARD_H), 11, stroke);
        drawSpiderMark(canvas, x + CARD_W * 0.5f, TABLE_Y + CARD_H * 0.5f, 30, Color(176, 150, 103, 38));
    }

    void drawCard(Canvas& canvas, const Card& card, float x, float y, bool selected, bool hinted, float alpha) {
        if (card.faceUp) drawFaceUp(canvas, card, x, y, selected, hinted, alpha);
        else drawFaceDown(canvas, x, y, alpha);
    }

    void drawMotionCard(Canvas& canvas, const Card& card, float x, float y, float alpha) {
        // During a short move the eye reads silhouette, rank and suit. Keeping that
        // hierarchy while omitting static ornament makes multi-card deals cheap.
        drawCardShadow(canvas, x, y, false, alpha);
        Paint paper;
        paper.setStyle(Paint::Style::FILL);
        paper.setColor(Color(248, 243, 232, static_cast<int>(255 * alpha)));
        canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 10, paper);
        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeWidth(1.1f);
        border.setColor(Color(196, 170, 124, static_cast<int>(220 * alpha)));
        canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 10, border);

        const bool red = card.suit == Suit::Heart || card.suit == Suit::Diamond;
        const Color ink = red ? Color(174, 48, 64, static_cast<int>(255 * alpha))
                              : Color(28, 31, 43, static_cast<int>(255 * alpha));
        Paint rank;
        rank.setStyle(Paint::Style::FILL);
        rank.setColor(ink);
        rank.setTextSize(card.rank == 10 ? 22 : 26);
        useUiFont(rank, 780);
        canvas.drawText(rankText(card.rank), x + 10, y + 7, rank);
        drawSuit(canvas, card.suit, x + CARD_W * 0.5f, y + CARD_H * 0.55f, 18.0f, ink);
    }

    void drawCardShadow(Canvas& canvas, float x, float y, bool selected, float alpha) const {
        // A blurred Canvas shadow performs full-canvas offscreen Gaussian passes. With 54 cards
        // visible at startup that makes the game needlessly redraw the whole framebuffer hundreds
        // of times per frame. Two offset silhouettes retain the visual depth at a tiny fraction of
        // the cost and are covered by the opaque card body everywhere except around its edges.
        Paint ambient;
        ambient.setStyle(Paint::Style::FILL);
        ambient.setColor(Color(1, 18, 12, static_cast<int>((selected ? 54 : 26) * alpha)));
        canvas.drawRoundRect(RectF(x - 0.5f, y + 1, CARD_W + 1, CARD_H + 1.5f), 11, ambient);

        Paint key;
        key.setStyle(Paint::Style::FILL);
        key.setColor(Color(1, 12, 8, static_cast<int>((selected ? 92 : 46) * alpha)));
        canvas.drawRoundRect(RectF(x + 0.5f, y + (selected ? 4 : 2), CARD_W - 1, CARD_H), 10, key);
    }

    void drawFaceUp(Canvas& canvas, const Card& card, float x, float y, bool selected, bool hinted, float alpha) {
        const float lift = selected ? -4.0f : 0.0f;
        y += lift;
        drawCardShadow(canvas, x, y, selected, alpha);
        Paint paper;
        paper.setStyle(Paint::Style::FILL);
        paper.setLinearGradient(x, y, x + CARD_W, y + CARD_H,
                                Color(255, 253, 246, static_cast<int>(255 * alpha)),
                                Color(233, 226, 213, static_cast<int>(255 * alpha)));
        canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 10, paper);

        Paint topSheen;
        topSheen.setStyle(Paint::Style::FILL);
        topSheen.setLinearGradient(x, y, x, y + 42,
                                   Color(255, 255, 255, static_cast<int>(145 * alpha)),
                                   Color(255, 255, 255, 0));
        canvas.drawRoundRect(RectF(x + 3, y + 3, CARD_W - 6, 38), 7, topSheen);

        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeWidth(selected || hinted ? 2.2f : 0.8f);
        border.setColor(selected ? Color(232, 188, 103, 245) : hinted ? Color(238, 190, 96, 230)
                                 : Color(178, 168, 151, 210));
        canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 10, border);
        const bool red = card.suit == Suit::Heart || card.suit == Suit::Diamond;
        const Color ink = red ? Color(174, 48, 64, static_cast<int>(255 * alpha))
                              : Color(28, 31, 43, static_cast<int>(255 * alpha));
        Paint text;
        text.setStyle(Paint::Style::FILL);
        text.setColor(ink);
        text.setTextSize(card.rank == 10 ? 22 : 26);
        useUiFont(text, 780);
        canvas.drawText(rankText(card.rank), x + 10, y + 7, text);
        drawSuit(canvas, card.suit, x + 19, y + 45, 8.1f, ink);
        drawSuit(canvas, card.suit, x + CARD_W * 0.5f, y + 79, 17.2f, ink);

        canvas.save();
        canvas.translate(x + CARD_W - 9, y + CARD_H - 8);
        canvas.rotate(3.14159265f);
        Paint lower = text;
        lower.setTextSize(card.rank == 10 ? 18 : 20);
        canvas.drawText(rankText(card.rank), 0, 0, lower);
        drawSuit(canvas, card.suit, 8, 31, 7.0f, ink);
        canvas.restore();
    }

    void drawFaceDown(Canvas& canvas, float x, float y, float alpha) {
        drawCardShadow(canvas, x, y, false, alpha);
        Paint paper;
        paper.setStyle(Paint::Style::FILL);
        paper.setColor(Color(190, 177, 153, static_cast<int>(255 * alpha)));
        canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 10, paper);
        Paint inset;
        inset.setStyle(Paint::Style::FILL);
        inset.setLinearGradient(x + 2.5f, y + 2.5f, x + CARD_W - 2.5f, y + CARD_H - 2.5f,
                                Color(112, 38, 60, static_cast<int>(255 * alpha)),
                                Color(49, 20, 43, static_cast<int>(255 * alpha)));
        canvas.drawRoundRect(RectF(x + 2.5f, y + 2.5f, CARD_W - 5, CARD_H - 5), 8, inset);

        Paint lattice;
        lattice.setStyle(Paint::Style::FILL);
        lattice.setColor(Color(235, 198, 128, static_cast<int>(54 * alpha)));
        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 5; ++col) {
                const float px = x + 12 + col * 18.0f + (row % 2 ? 8.0f : 0.0f);
                const float py = y + 14 + row * 15.0f;
                if (px > x + CARD_W - 8) continue;
                canvas.drawPolygon(std::vector<PointF>{{px, py - 3}, {px + 3, py},
                                                       {px, py + 3}, {px - 3, py}}, lattice);
            }
        }

        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeWidth(1.1f);
        border.setColor(Color(226, 190, 119, static_cast<int>(190 * alpha)));
        canvas.drawRoundRect(RectF(x + 4, y + 4, CARD_W - 8, CARD_H - 8), 7, border);
        Paint innerBorder = border;
        innerBorder.setColor(Color(248, 224, 174, static_cast<int>(80 * alpha)));
        canvas.drawRoundRect(RectF(x + 7, y + 7, CARD_W - 14, CARD_H - 14), 5, innerBorder);

        const float cx = x + CARD_W * 0.5f;
        const float cy = y + CARD_H * 0.5f;
        Paint crest;
        crest.setStyle(Paint::Style::FILL);
        crest.setColor(Color(232, 195, 124, static_cast<int>(230 * alpha)));
        canvas.drawPolygon(std::vector<PointF>{{cx, cy - 31}, {cx + 24, cy},
                                               {cx, cy + 31}, {cx - 24, cy}}, crest);
        Paint crestInset = crest;
        crestInset.setColor(Color(62, 24, 48, static_cast<int>(255 * alpha)));
        canvas.drawPolygon(std::vector<PointF>{{cx, cy - 27}, {cx + 20, cy},
                                               {cx, cy + 27}, {cx - 20, cy}}, crestInset);
        drawSpiderMark(canvas, cx, cy, 25, Color(242, 207, 140, static_cast<int>(255 * alpha)));
    }

    void drawSuit(Canvas& canvas, Suit suit, float cx, float cy, float size, const Color& color) const {
        Paint p;
        p.setStyle(Paint::Style::FILL);
        p.setColor(color);
        if (suit == Suit::Diamond) {
            canvas.drawPolygon(std::vector<PointF>{{cx, cy - size * 1.22f}, {cx + size * 0.82f, cy},
                                                   {cx, cy + size * 1.22f}, {cx - size * 0.82f, cy}}, p);
            return;
        }

        if (suit == Suit::Heart) {
            Path heart;
            heart.moveTo(cx, cy + size * 1.15f);
            heart.cubicTo(cx - size * 0.20f, cy + size * 0.80f,
                          cx - size * 1.10f, cy + size * 0.25f,
                          cx - size * 1.10f, cy - size * 0.38f);
            heart.cubicTo(cx - size * 1.10f, cy - size * 1.08f,
                          cx - size * 0.24f, cy - size * 1.24f,
                          cx, cy - size * 0.58f);
            heart.cubicTo(cx + size * 0.24f, cy - size * 1.24f,
                          cx + size * 1.10f, cy - size * 1.08f,
                          cx + size * 1.10f, cy - size * 0.38f);
            heart.cubicTo(cx + size * 1.10f, cy + size * 0.25f,
                          cx + size * 0.20f, cy + size * 0.80f,
                          cx, cy + size * 1.15f);
            heart.close();
            canvas.drawPath(heart, p);
            return;
        }

        if (suit == Suit::Spade) {
            Path spade;
            spade.moveTo(cx, cy - size * 1.20f);
            spade.cubicTo(cx - size * 0.18f, cy - size * 0.86f,
                          cx - size * 1.05f, cy - size * 0.38f,
                          cx - size * 1.05f, cy + size * 0.20f);
            spade.cubicTo(cx - size * 1.05f, cy + size * 0.75f,
                          cx - size * 0.44f, cy + size * 0.92f,
                          cx - size * 0.17f, cy + size * 0.58f);
            spade.lineTo(cx - size * 0.40f, cy + size * 1.18f);
            spade.lineTo(cx + size * 0.40f, cy + size * 1.18f);
            spade.lineTo(cx + size * 0.17f, cy + size * 0.58f);
            spade.cubicTo(cx + size * 0.44f, cy + size * 0.92f,
                          cx + size * 1.05f, cy + size * 0.75f,
                          cx + size * 1.05f, cy + size * 0.20f);
            spade.cubicTo(cx + size * 1.05f, cy - size * 0.38f,
                          cx + size * 0.18f, cy - size * 0.86f,
                          cx, cy - size * 1.20f);
            spade.close();
            canvas.drawPath(spade, p);
            return;
        }

        // Draw the club as one silhouette. Separate translucent circles visibly
        // darken where they overlap, especially on animated cards.
        Path club;
        club.moveTo(cx, cy - size * 1.16f);
        club.cubicTo(cx - size * 0.42f, cy - size * 1.16f,
                     cx - size * 0.68f, cy - size * 0.86f,
                     cx - size * 0.68f, cy - size * 0.52f);
        club.cubicTo(cx - size * 0.68f, cy - size * 0.34f,
                     cx - size * 0.60f, cy - size * 0.18f,
                     cx - size * 0.46f, cy - size * 0.06f);
        club.cubicTo(cx - size * 0.62f, cy - size * 0.20f,
                     cx - size * 0.84f, cy - size * 0.24f,
                     cx - size * 1.01f, cy - size * 0.14f);
        club.cubicTo(cx - size * 1.29f, cy + size * 0.02f,
                     cx - size * 1.29f, cy + size * 0.54f,
                     cx - size * 1.04f, cy + size * 0.72f);
        club.cubicTo(cx - size * 0.82f, cy + size * 0.89f,
                     cx - size * 0.48f, cy + size * 0.82f,
                     cx - size * 0.33f, cy + size * 0.58f);
        club.lineTo(cx - size * 0.42f, cy + size * 1.16f);
        club.lineTo(cx + size * 0.42f, cy + size * 1.16f);
        club.lineTo(cx + size * 0.33f, cy + size * 0.58f);
        club.cubicTo(cx + size * 0.48f, cy + size * 0.82f,
                     cx + size * 0.82f, cy + size * 0.89f,
                     cx + size * 1.04f, cy + size * 0.72f);
        club.cubicTo(cx + size * 1.29f, cy + size * 0.54f,
                     cx + size * 1.29f, cy + size * 0.02f,
                     cx + size * 1.01f, cy - size * 0.14f);
        club.cubicTo(cx + size * 0.84f, cy - size * 0.24f,
                     cx + size * 0.62f, cy - size * 0.20f,
                     cx + size * 0.46f, cy - size * 0.06f);
        club.cubicTo(cx + size * 0.60f, cy - size * 0.18f,
                     cx + size * 0.68f, cy - size * 0.34f,
                     cx + size * 0.68f, cy - size * 0.52f);
        club.cubicTo(cx + size * 0.68f, cy - size * 0.86f,
                     cx + size * 0.42f, cy - size * 1.16f,
                     cx, cy - size * 1.16f);
        club.close();
        canvas.drawPath(club, p);
    }

    void drawStockBack(Canvas& canvas, float x, float y) {
        constexpr float w = 70.0f;
        constexpr float h = 98.0f;
        Paint shadow;
        shadow.setStyle(Paint::Style::FILL);
        shadow.setColor(Color(1, 14, 9, 48));
        canvas.drawRoundRect(RectF(x + 0.5f, y + 2, w - 1, h), 8, shadow);

        Paint edge;
        edge.setStyle(Paint::Style::FILL);
        edge.setColor(Color(190, 177, 153));
        canvas.drawRoundRect(RectF(x, y, w, h), 8, edge);

        Paint back;
        back.setStyle(Paint::Style::FILL);
        back.setLinearGradient(x + 2, y + 2, x + w - 2, y + h - 2,
                               Color(112, 38, 60), Color(49, 20, 43));
        canvas.drawRoundRect(RectF(x + 2, y + 2, w - 4, h - 4), 6, back);

        Paint lattice;
        lattice.setStyle(Paint::Style::FILL);
        lattice.setColor(Color(235, 198, 128, 58));
        for (int row = 0; row < 6; ++row) {
            for (int col = 0; col < 4; ++col) {
                const float px = x + 10 + col * 16.0f + (row % 2 ? 7.0f : 0.0f);
                const float py = y + 12 + row * 14.5f;
                if (px > x + w - 7) continue;
                canvas.drawPolygon(std::vector<PointF>{{px, py - 2.5f}, {px + 2.5f, py},
                                                       {px, py + 2.5f}, {px - 2.5f, py}}, lattice);
            }
        }

        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeWidth(1.0f);
        border.setColor(Color(231, 196, 127, 210));
        canvas.drawRoundRect(RectF(x + 4, y + 4, w - 8, h - 8), 5, border);
        Paint inner = border;
        inner.setColor(Color(248, 224, 174, 92));
        canvas.drawRoundRect(RectF(x + 7, y + 7, w - 14, h - 14), 4, inner);
        const float cx = x + w * 0.5f;
        const float cy = y + h * 0.5f;
        Paint crest;
        crest.setStyle(Paint::Style::FILL);
        crest.setColor(Color(232, 195, 124, 230));
        canvas.drawPolygon(std::vector<PointF>{{cx, cy - 23}, {cx + 17, cy},
                                               {cx, cy + 23}, {cx - 17, cy}}, crest);
        Paint crestInset = crest;
        crestInset.setColor(Color(62, 24, 48));
        canvas.drawPolygon(std::vector<PointF>{{cx, cy - 20}, {cx + 14, cy},
                                               {cx, cy + 20}, {cx - 14, cy}}, crestInset);
        drawSpiderMark(canvas, cx, cy, 19, Color(242, 207, 140));
    }

    void drawStock(Canvas& canvas) {
        const RectF r = stockRect();
        const int deals = static_cast<int>(stock_.size() / 10);
        const bool hover = deals > 0 && inside(r, pointerX_, pointerY_);
        const bool pressed = pressedButton_ == 5 && buttonPressTime_ > 0.0f;
        const float stockScale = pressed ? 0.97f : hover ? 1.025f : 1.0f;
        const float cx = r.getX() + r.getWidth() * 0.5f;
        const float cy = r.getY() + r.getHeight() * 0.5f;
        canvas.save();
        canvas.translate(cx, cy);
        canvas.scale(stockScale, stockScale);
        canvas.translate(-cx, -cy);
        if (deals > 0) {
            for (int i = std::min(3, deals - 1); i >= 0; --i)
                drawStockBack(canvas, r.getX() + 7 + i * 5.5f, r.getY() + 1 + i * 2.2f);
            Paint badge;
            badge.setStyle(Paint::Style::FILL);
            badge.setColor(Color(224, 184, 110));
            canvas.drawCircle(r.getX() + r.getWidth() - 5, r.getY() + 10, 12 + dealPulse_ * 2, badge);
            Paint count;
            count.setStyle(Paint::Style::FILL);
            count.setColor(Color(39, 29, 25));
            count.setTextSize(14);
            count.setTextAlign(Paint::TextAlign::CENTER);
            count.setTextBaseline(Paint::TextBaseline::MIDDLE);
            useUiFont(count, 800);
            canvas.drawText(std::to_string(deals), r.getX() + r.getWidth() - 5, r.getY() + 10, count);
        } else {
            Paint empty;
            empty.setStyle(Paint::Style::STROKE);
            empty.setStrokeWidth(1.4f);
            empty.setColor(Color(185, 159, 112, 70));
            canvas.drawRoundRect(RectF(r.getX() + 8, r.getY() + 1, 72, 98), 9, empty);
        }
        Paint label;
        label.setStyle(Paint::Style::FILL);
        label.setColor(hover ? Color(249, 222, 169) : Color(222, 201, 160));
        label.setTextSize(13);
        label.setLetterSpacing(0.8f);
        label.setTextAlign(Paint::TextAlign::CENTER);
        useUiFont(label, 700);
        canvas.drawText(deals > 0 ? "DEAL" : "EMPTY", cx, r.getY() + 135, label);
        if (hintTime_ > 0 && hintDest_ == 10) {
            Paint h;
            h.setStyle(Paint::Style::STROKE);
            h.setStrokeWidth(3);
            h.setColor(Color(232, 188, 103, 220));
            canvas.drawRoundRect(RectF(r.getX() + 3, r.getY() - 2, 84, 112), 12, h);
        }
        canvas.restore();
    }

    void drawToast(Canvas& canvas) {
        if (toastTime_ <= 0 || won_) return;
        const float age = std::max(0.0f, toastDuration_ - toastTime_);
        const float enter = easeOutQuint(age / 0.25f);
        const float exit = clamp01(toastTime_ / 0.30f);
        const float alpha = std::min(enter, exit);
        Paint text;
        text.setStyle(Paint::Style::FILL);
        text.setTextSize(16.5f);
        text.setTextAlign(Paint::TextAlign::CENTER);
        text.setTextBaseline(Paint::TextBaseline::MIDDLE);
        useUiFont(text, 680);
        if (measuredToast_ != toast_) {
            measuredToast_ = toast_;
            measuredToastWidth_ = canvas.measureTextMetrics(toast_, text).width;
        }
        const float w = measuredToastWidth_ + 64;
        const float y = 792.0f + (1.0f - enter) * 16.0f;
        const RectF rect((DESIGN_W - w) * 0.5f, y, w, 48);
        Paint bg;
        bg.setStyle(Paint::Style::FILL);
        bg.setColor(Color(8, 10, 18, static_cast<int>(232 * alpha)));
        canvas.drawRoundRect(rect, 24, bg);
        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeWidth(1);
        border.setColor(Color(211, 174, 107, static_cast<int>(145 * alpha)));
        canvas.drawRoundRect(rect, 24, border);
        text.setColor(Color(239, 232, 216, static_cast<int>(255 * alpha)));
        canvas.drawText(toast_, DESIGN_W * 0.5f, y + 24, text);
    }

    void drawWin(Canvas& canvas) {
        Paint veil;
        veil.setStyle(Paint::Style::FILL);
        veil.setColor(Color(1, 18, 22, 220));
        canvas.drawRect(RectF(0, 96, DESIGN_W, DESIGN_H - 96), veil);
        const float pulse = 1.0f + std::sin(phase_ * 3.0f) * 0.06f;
        drawSpiderMark(canvas, DESIGN_W * 0.5f, 320, 112 * pulse, Color(104, 241, 193));
        Paint title;
        title.setStyle(Paint::Style::FILL);
        title.setColor(Color(242, 252, 247));
        title.setTextSize(42);
        title.setTextAlign(Paint::TextAlign::CENTER);
        useUiFont(title, 800);
        canvas.drawText("MASTERFUL!", DESIGN_W * 0.5f, 415, title);
        Paint sub = title;
        sub.setTextSize(17);
        sub.setColor(Color(151, 218, 197));
        useUiFont(sub, 500);
        canvas.drawText("Eight complete runs cleared in " + formatTime(elapsed_), DESIGN_W * 0.5f, 475, sub);
        sub.setTextSize(14);
        sub.setColor(Color(235, 208, 111));
        canvas.drawText("Final score  " + std::to_string(score_) + "   |   Click anywhere for a new deal", DESIGN_W * 0.5f, 520, sub);
    }
};

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
            // Recover after a resize, breakpoint or other long stall instead of trying to catch up.
            nextFrame = glfwGetTime();
        }
        glfwPollEvents();
    }

    canvas.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return captureFailed || hoverTestFailed ? 1 : 0;
}
