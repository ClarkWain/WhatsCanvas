#include <wsc/FontSystem.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>

#include "wsc/wsc.h"

using namespace wsc;

namespace {

constexpr float DESIGN_W = 1280.0f;
constexpr float DESIGN_H = 860.0f;
constexpr float CARD_W = 94.0f;
constexpr float CARD_H = 128.0f;
constexpr float COL_X = 48.0f;
constexpr float COL_GAP = 121.0f;
constexpr float TABLE_Y = 154.0f;
constexpr float TABLE_BOTTOM = 822.0f;
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

float clamp01(float value) { return std::max(0.0f, std::min(1.0f, value)); }

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
        newGame(seed);
    }

    void newGame(std::uint32_t seed = 0) {
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
        toast_ = "Build suited runs from King down to Ace";
        toastTime_ = 3.8f;

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
        std::cout << "NEW_GAME seed=" << currentSeed_ << " suits=" << difficulty_ << '\n';
    }

    void update(float dt) {
        if (!won_) elapsed_ += std::min(std::max(dt, 0.0f), 0.1f);
        phase_ += dt;
        toastTime_ = std::max(0.0f, toastTime_ - dt);
        hintTime_ = std::max(0.0f, hintTime_ - dt);
        dealPulse_ = std::max(0.0f, dealPulse_ - dt * 2.4f);
    }

    void setDifficulty(int suits) {
        if (suits != 1 && suits != 2 && suits != 4) return;
        difficulty_ = suits;
        requestedSeed_ = 0;
        newGame();
    }

    void cycleDifficulty() {
        setDifficulty(difficulty_ == 1 ? 2 : difficulty_ == 2 ? 4 : 1);
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
        selectedColumn_ = selectedIndex_ = -1;
        hintTime_ = 0.0f;
        toast_ = revealed ? "A hidden card was revealed" : "Moved " + std::to_string(count) + (count == 1 ? " card" : " cards");
        toastTime_ = 1.5f;
        std::cout << "MOVE c" << src + 1 << ':' << index << " -> c" << dest + 1
                  << " count=" << count << " runs=" << completed_.size() << '\n';
        if (static_cast<int>(completed_.size()) > before) toastTime_ = 2.5f;
        return true;
    }

    bool dealStock() {
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
        for (int col = 0; col < 10; ++col) {
            Card card = stock_.back();
            stock_.pop_back();
            card.faceUp = true;
            columns_[col].push_back(card);
        }
        ++moves_;
        score_ = std::max(0, score_ - 1);
        selectedColumn_ = selectedIndex_ = -1;
        collectCompleteRuns();
        dealPulse_ = 1.0f;
        setToast("A new row was dealt", 1.7f);
        std::cout << "DEAL remaining=" << stock_.size() << '\n';
        return true;
    }

    bool undo() {
        if (history_.empty()) {
            setToast("Nothing to undo", 1.6f);
            return false;
        }
        const Snapshot state = history_.back();
        history_.pop_back();
        columns_ = state.columns;
        stock_ = state.stock;
        completed_ = state.completed;
        moves_ = state.moves;
        score_ = state.score;
        elapsed_ = state.elapsed;
        won_ = false;
        selectedColumn_ = selectedIndex_ = -1;
        dragging_ = false;
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
            setToast("No useful moves — deal the next row", 2.2f);
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
            if (dx * dx + dy * dy > 30.0f) dragging_ = true;
        }
    }

    void pointerDown(float x, float y) {
        pointerMove(x, y);
        mouseDown_ = true;
        pressX_ = x;
        pressY_ = y;
        dragging_ = false;

        if (inside(newButton(), x, y)) { newGame(); mouseDown_ = false; return; }
        if (inside(difficultyButton(), x, y)) { cycleDifficulty(); mouseDown_ = false; return; }
        if (inside(undoButton(), x, y)) { undo(); mouseDown_ = false; return; }
        if (inside(hintButton(), x, y)) { hint(); mouseDown_ = false; return; }
        if (inside(stockRect(), x, y)) { dealStock(); mouseDown_ = false; return; }
        if (won_) { newGame(); mouseDown_ = false; return; }

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
                dragOffsetX_ = x - columnX(hit.column);
                dragOffsetY_ = y - columnPositions(hit.column)[hit.index];
            } else {
                selectedColumn_ = selectedIndex_ = -1;
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
        }
    }

    void pointerUp(float x, float y) {
        pointerMove(x, y);
        if (mouseDown_ && dragging_ && selectedColumn_ >= 0) {
            const int dest = hitColumn(x, y);
            if (dest >= 0 && dest != selectedColumn_ && moveRun(selectedColumn_, selectedIndex_, dest)) {
                // moved
            } else {
                setToast("That run cannot be placed there", 1.8f);
            }
        }
        mouseDown_ = false;
        dragging_ = false;
    }

    void cancelSelection() {
        selectedColumn_ = selectedIndex_ = -1;
        mouseDown_ = dragging_ = false;
    }

    void render(Canvas& canvas, int windowW, int windowH) {
        const float scale = std::min(windowW / DESIGN_W, windowH / DESIGN_H);
        const float ox = (windowW - DESIGN_W * scale) * 0.5f;
        const float oy = (windowH - DESIGN_H * scale) * 0.5f;
        renderScale_ = scale;
        renderOffsetX_ = ox;
        renderOffsetY_ = oy;

        drawOutside(canvas, windowW, windowH);
        canvas.save();
        canvas.translate(ox, oy);
        canvas.scale(scale, scale);
        drawTable(canvas);
        drawHeader(canvas);
        drawColumns(canvas);
        drawStock(canvas);
        drawToast(canvas);
        if (won_) drawWin(canvas);
        canvas.restore();
    }

    std::pair<float, float> toDesign(float windowX, float windowY) const {
        return {(windowX - renderOffsetX_) / std::max(renderScale_, 0.0001f),
                (windowY - renderOffsetY_) / std::max(renderScale_, 0.0001f)};
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

        game.completed_.assign(7, Suit::Spade);
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

        const size_t stockBefore = game.stock_.size();
        const RectF stock = game.stockRect();
        game.pointerDown(stock.getX() + 20, stock.getY() + 20);
        ok &= expect(game.stock_.size() == stockBefore - 10, "stock control deals through pointer handler");
        ok &= expect(game.moves_ == 1, "deal increments move counter");

        game.undo();
        game.columns_[5].clear();
        const size_t rejectedStock = game.stock_.size();
        game.pointerDown(stock.getX() + 20, stock.getY() + 20);
        ok &= expect(game.stock_.size() == rejectedStock, "stock click is rejected with an empty column");

        const RectF difficulty = game.difficultyButton();
        game.pointerDown(difficulty.getX() + 20, difficulty.getY() + 20);
        ok &= expect(game.difficulty_ == 2 && game.stock_.size() == 50, "difficulty button starts a valid two-suit deal");

        const RectF hint = game.hintButton();
        game.pointerDown(hint.getX() + 20, hint.getY() + 20);
        ok &= expect(game.hintTime_ > 0, "Hint button produces visible guidance");

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
    float hintTime_ = 0.0f;
    float dealPulse_ = 0.0f;
    std::string toast_;
    float renderScale_ = 1.0f;
    float renderOffsetX_ = 0.0f;
    float renderOffsetY_ = 0.0f;

    float columnX(int col) const { return COL_X + col * COL_GAP; }
    RectF newButton() const { return RectF(737, 25, 112, 42); }
    RectF difficultyButton() const { return RectF(858, 25, 108, 42); }
    RectF undoButton() const { return RectF(975, 25, 92, 42); }
    RectF hintButton() const { return RectF(1076, 25, 92, 42); }
    RectF stockRect() const { return RectF(1177, 14, 76, 68); }

    void saveUndo() {
        history_.push_back({columns_, stock_, completed_, moves_, score_, elapsed_});
        if (history_.size() > 100) history_.erase(history_.begin());
    }

    void setToast(std::string text, float time) {
        toast_ = std::move(text);
        toastTime_ = time;
    }

    void collectCompleteRuns() {
        bool collectedAny;
        do {
            collectedAny = false;
            for (auto& col : columns_) {
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
        p.setColor(Color(6, 15, 22));
        canvas.drawRect(RectF(0, 0, static_cast<float>(width), static_cast<float>(height)), p);
    }

    void drawTable(Canvas& canvas) {
        Paint bg;
        bg.setStyle(Paint::Style::FILL);
        bg.setLinearGradient(0, 0, DESIGN_W, DESIGN_H, {
            {0.0f, Color(8, 54, 52)}, {0.48f, Color(9, 73, 65)}, {1.0f, Color(5, 42, 45)}});
        canvas.drawRect(RectF(0, 0, DESIGN_W, DESIGN_H), bg);

        Paint glow;
        glow.setStyle(Paint::Style::FILL);
        glow.setRadialGradient(700, 330, 620, Color(35, 152, 121, 42), Color(4, 38, 41, 0));
        canvas.drawCircle(700, 330, 620, glow);

        Paint line;
        line.setStyle(Paint::Style::STROKE);
        line.setStrokeWidth(1.0f);
        line.setColor(Color(128, 222, 190, 14));
        for (int i = 0; i < 12; ++i) {
            const float y = 115.0f + i * 63.0f;
            canvas.drawLine(0.0f, y, DESIGN_W, y + 100.0f, line);
        }

        Paint header;
        header.setStyle(Paint::Style::FILL);
        header.setColor(Color(4, 30, 35, 218));
        canvas.drawRect(RectF(0, 0, DESIGN_W, 96), header);
        Paint divider;
        divider.setStyle(Paint::Style::FILL);
        divider.setLinearGradient(0, 0, DESIGN_W, 0, Color(75, 226, 178, 20), Color(88, 233, 192, 130));
        canvas.drawRect(RectF(0, 95, DESIGN_W, 1), divider);
    }

    void drawSpiderMark(Canvas& canvas, float cx, float cy, float s, const Color& color) const {
        Paint stroke;
        stroke.setStyle(Paint::Style::STROKE);
        stroke.setStrokeWidth(std::max(1.0f, s * 0.075f));
        stroke.setStrokeCap(Paint::StrokeCap::ROUND);
        stroke.setColor(color);
        for (int side : {-1, 1}) {
            for (int i = 0; i < 4; ++i) {
                const float dy = (i - 1.5f) * s * 0.17f;
                const float sy = cy + dy;
                const float ex = cx + side * s * (0.50f + 0.06f * std::abs(i - 1));
                const float ey = cy + dy * 1.8f + (i < 2 ? -s * 0.10f : s * 0.10f);
                Path leg;
                leg.moveTo(cx + side * s * 0.12f, sy);
                leg.quadTo(cx + side * s * 0.35f, sy + (ey - sy) * 0.2f, ex, ey);
                canvas.drawPath(leg, stroke);
            }
        }
        Paint body;
        body.setStyle(Paint::Style::FILL);
        body.setColor(color);
        canvas.drawOval(RectF(cx - s * 0.13f, cy - s * 0.25f, s * 0.26f, s * 0.43f), body);
        canvas.drawCircle(cx, cy - s * 0.24f, s * 0.11f, body);
    }

    void drawHeader(Canvas& canvas) {
        drawSpiderMark(canvas, 48, 48, 50, Color(91, 239, 191));
        Paint title;
        title.setStyle(Paint::Style::FILL);
        title.setColor(Color(235, 250, 244));
        title.setTextSize(24);
        title.setLetterSpacing(0.8f);
        useUiFont(title, 700);
        canvas.drawText("SPIDER", 83, 23, title);
        Paint sub = title;
        sub.setTextSize(11);
        sub.setLetterSpacing(2.8f);
        sub.setColor(Color(103, 196, 169));
        canvas.drawText("SOLITAIRE", 84, 56, sub);

        drawMetric(canvas, 244, "SCORE", std::to_string(score_));
        drawMetric(canvas, 364, "MOVES", std::to_string(moves_));
        drawMetric(canvas, 484, "TIME", formatTime(elapsed_));
        drawMetric(canvas, 604, "RUNS", std::to_string(completed_.size()) + "/8");

        drawButton(canvas, newButton(), "NEW DEAL", true);
        drawButton(canvas, difficultyButton(), std::to_string(difficulty_) + " SUIT" + (difficulty_ > 1 ? "S" : ""), true);
        drawButton(canvas, undoButton(), "UNDO", !history_.empty());
        drawButton(canvas, hintButton(), "HINT", true);

        for (int i = 0; i < 8; ++i) {
            const float x = 424.0f + i * 46.0f;
            Paint slot;
            slot.setStyle(Paint::Style::FILL);
            slot.setColor(i < static_cast<int>(completed_.size()) ? Color(70, 199, 154, 35) : Color(4, 31, 34, 90));
            canvas.drawRoundRect(RectF(x, 107, 34, 30), 8, slot);
            Paint border;
            border.setStyle(Paint::Style::STROKE);
            border.setStrokeWidth(1);
            border.setColor(i < static_cast<int>(completed_.size()) ? Color(95, 235, 186, 210) : Color(85, 164, 145, 60));
            canvas.drawRoundRect(RectF(x, 107, 34, 30), 8, border);
            if (i < static_cast<int>(completed_.size())) drawSuit(canvas, completed_[i], x + 17, 122, 8, Color(112, 244, 197));
        }
        Paint runLabel;
        runLabel.setStyle(Paint::Style::FILL);
        runLabel.setColor(Color(123, 195, 174));
        runLabel.setTextSize(10);
        runLabel.setLetterSpacing(1.5f);
        useUiFont(runLabel, 600);
        canvas.drawText("COMPLETED RUNS", 270, 115, runLabel);
    }

    void drawMetric(Canvas& canvas, float x, const std::string& label, const std::string& value) {
        Paint labelPaint;
        labelPaint.setStyle(Paint::Style::FILL);
        labelPaint.setColor(Color(106, 175, 158));
        labelPaint.setTextSize(10);
        labelPaint.setLetterSpacing(1.4f);
        useUiFont(labelPaint, 600);
        canvas.drawText(label, x, 24, labelPaint);
        Paint valuePaint = labelPaint;
        valuePaint.setColor(Color(239, 250, 246));
        valuePaint.setTextSize(20);
        valuePaint.setLetterSpacing(0);
        useUiFont(valuePaint, 700);
        canvas.drawText(value, x, 45, valuePaint);
    }

    void drawButton(Canvas& canvas, const RectF& rect, const std::string& label, bool enabled) {
        const bool hover = enabled && inside(rect, pointerX_, pointerY_);
        Paint fill;
        fill.setStyle(Paint::Style::FILL);
        if (hover) fill.setLinearGradient(rect.getX(), rect.getY(), rect.getX(), rect.getY() + rect.getHeight(),
                                         Color(48, 167, 132), Color(25, 119, 102));
        else fill.setColor(enabled ? Color(15, 74, 70, 215) : Color(11, 46, 48, 150));
        canvas.drawRoundRect(rect, 12, fill);
        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeWidth(1);
        border.setColor(enabled ? Color(88, 209, 170, hover ? 210 : 100) : Color(77, 130, 121, 45));
        canvas.drawRoundRect(rect, 12, border);
        Paint text;
        text.setStyle(Paint::Style::FILL);
        text.setColor(enabled ? Color(223, 247, 239) : Color(92, 136, 128));
        text.setTextSize(11);
        text.setLetterSpacing(0.8f);
        text.setTextAlign(Paint::TextAlign::CENTER);
        text.setTextBaseline(Paint::TextBaseline::MIDDLE);
        useUiFont(text, 700);
        canvas.drawText(label, rect.getX() + rect.getWidth() * 0.5f,
                        rect.getY() + rect.getHeight() * 0.5f, text);
    }

    void drawColumns(Canvas& canvas) {
        int dragCol = dragging_ ? selectedColumn_ : -1;
        for (int col = 0; col < 10; ++col) {
            const auto ys = columnPositions(col);
            if (columns_[col].empty()) drawEmptySlot(canvas, col);
            for (int i = 0; i < static_cast<int>(columns_[col].size()); ++i) {
                if (col == dragCol && i >= selectedIndex_) continue;
                const bool selected = !dragging_ && col == selectedColumn_ && i >= selectedIndex_;
                const bool hinted = hintTime_ > 0 && col == hintSource_ && i >= hintIndex_;
                drawCard(canvas, columns_[col][i], columnX(col), ys[i], selected, hinted, 1.0f);
            }
        }

        if (dragging_ && selectedColumn_ >= 0) {
            const auto ys = columnPositions(selectedColumn_);
            float x = pointerX_ - dragOffsetX_;
            float y = pointerY_ - dragOffsetY_;
            for (int i = selectedIndex_; i < static_cast<int>(columns_[selectedColumn_].size()); ++i) {
                drawCard(canvas, columns_[selectedColumn_][i], x, y + (ys[i] - ys[selectedIndex_]), true, false, 0.96f);
            }
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
        fill.setColor(Color(1, 28, 30, 115));
        canvas.drawRoundRect(RectF(x, TABLE_Y, CARD_W, CARD_H), 12, fill);
        Paint stroke;
        stroke.setStyle(Paint::Style::STROKE);
        stroke.setStrokeWidth(1.5f);
        stroke.setColor(Color(103, 206, 174, 70));
        canvas.drawRoundRect(RectF(x, TABLE_Y, CARD_W, CARD_H), 12, stroke);
        drawSpiderMark(canvas, x + CARD_W * 0.5f, TABLE_Y + CARD_H * 0.5f, 34, Color(76, 164, 142, 55));
    }

    void drawCard(Canvas& canvas, const Card& card, float x, float y, bool selected, bool hinted, float alpha) {
        if (card.faceUp) drawFaceUp(canvas, card, x, y, selected, hinted, alpha);
        else drawFaceDown(canvas, x, y, alpha);
    }

    void drawFaceUp(Canvas& canvas, const Card& card, float x, float y, bool selected, bool hinted, float alpha) {
        const float lift = selected ? -3.0f + std::sin(phase_ * 4.0f) : 0.0f;
        y += lift;
        canvas.drawBoxShadow(RectF(x, y, CARD_W, CARD_H), 11, selected ? 3 : 1, selected ? 12 : 7,
                             0, selected ? 6 : 4, selected ? Color(0, 21, 25, 145) : Color(0, 15, 19, 100));
        Paint paper;
        paper.setStyle(Paint::Style::FILL);
        paper.setLinearGradient(x, y, x + CARD_W, y + CARD_H,
                                Color(255, 253, 245, static_cast<int>(255 * alpha)),
                                Color(226, 235, 225, static_cast<int>(255 * alpha)));
        canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 11, paper);

        Paint topSheen;
        topSheen.setStyle(Paint::Style::FILL);
        topSheen.setColor(Color(255, 255, 255, static_cast<int>(115 * alpha)));
        canvas.drawRoundRect(RectF(x + 3, y + 3, CARD_W - 6, 28), 8, topSheen);

        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeWidth(selected || hinted ? 2.5f : 1.0f);
        border.setColor(selected ? Color(255, 210, 78, 240) : hinted ? Color(255, 215, 90, 225) : Color(206, 215, 204, 230));
        canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 11, border);

        const bool red = card.suit == Suit::Heart || card.suit == Suit::Diamond;
        const Color ink = red ? Color(184, 51, 61, static_cast<int>(255 * alpha)) : Color(25, 37, 43, static_cast<int>(255 * alpha));
        Paint text;
        text.setStyle(Paint::Style::FILL);
        text.setColor(ink);
        text.setTextSize(card.rank == 10 ? 20 : 23);
        useUiFont(text, 800);
        canvas.drawText(rankText(card.rank), x + 9, y + 7, text);
        drawSuit(canvas, card.suit, x + 18, y + 43, 8.2f, ink);
        drawSuit(canvas, card.suit, x + CARD_W * 0.5f, y + 82, 17.0f,
                 Color(ink.getR(), ink.getG(), ink.getB(), static_cast<int>(50 * alpha)));
    }

    void drawFaceDown(Canvas& canvas, float x, float y, float alpha) {
        canvas.drawBoxShadow(RectF(x, y, CARD_W, CARD_H), 11, 1, 7, 0, 4, Color(0, 14, 19, 100));
        Paint edge;
        edge.setStyle(Paint::Style::FILL);
        edge.setLinearGradient(x, y, x + CARD_W, y + CARD_H,
                               Color(31, 62, 76, static_cast<int>(255 * alpha)),
                               Color(11, 33, 48, static_cast<int>(255 * alpha)));
        canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 11, edge);
        Paint inset;
        inset.setStyle(Paint::Style::FILL);
        inset.setLinearGradient(x + 5, y + 5, x + CARD_W - 5, y + CARD_H - 5,
                                Color(18, 93, 89, static_cast<int>(255 * alpha)),
                                Color(10, 49, 63, static_cast<int>(255 * alpha)));
        canvas.drawRoundRect(RectF(x + 5, y + 5, CARD_W - 10, CARD_H - 10), 8, inset);
        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeWidth(1.2f);
        border.setColor(Color(103, 218, 182, static_cast<int>(150 * alpha)));
        canvas.drawRoundRect(RectF(x + 6, y + 6, CARD_W - 12, CARD_H - 12), 7, border);

        const float cx = x + CARD_W * 0.5f;
        const float cy = y + CARD_H * 0.5f;
        Paint web;
        web.setStyle(Paint::Style::STROKE);
        web.setStrokeWidth(0.85f);
        web.setColor(Color(117, 223, 192, static_cast<int>(74 * alpha)));
        for (int i = 0; i < 8; ++i) {
            const float a = i * 3.14159265f / 4.0f;
            canvas.drawLine(cx, cy, cx + std::cos(a) * 35, cy + std::sin(a) * 48, web);
        }
        for (int r = 1; r <= 3; ++r) canvas.drawOval(RectF(cx - r * 11, cy - r * 15, r * 22, r * 30), web);
        drawSpiderMark(canvas, cx, cy, 26, Color(137, 245, 207, static_cast<int>(205 * alpha)));
    }

    void drawSuit(Canvas& canvas, Suit suit, float cx, float cy, float size, const Color& color) const {
        Paint p;
        p.setStyle(Paint::Style::FILL);
        p.setColor(color);
        if (suit == Suit::Diamond) {
            canvas.drawPolygon(std::vector<PointF>{{cx, cy - size * 1.25f}, {cx + size, cy},
                                                   {cx, cy + size * 1.25f}, {cx - size, cy}}, p);
            return;
        }
        if (suit == Suit::Club) {
            canvas.drawCircle(cx, cy - size * 0.55f, size * 0.62f, p);
            canvas.drawCircle(cx - size * 0.55f, cy + size * 0.05f, size * 0.62f, p);
            canvas.drawCircle(cx + size * 0.55f, cy + size * 0.05f, size * 0.62f, p);
            canvas.drawPolygon(std::vector<PointF>{{cx - size * 0.28f, cy + size * 0.2f},
                                                   {cx + size * 0.28f, cy + size * 0.2f},
                                                   {cx + size * 0.47f, cy + size * 1.0f},
                                                   {cx - size * 0.47f, cy + size * 1.0f}}, p);
            return;
        }
        Path heart;
        if (suit == Suit::Heart) {
            heart.moveTo(cx, cy + size * 1.05f);
            heart.cubicTo(cx - size * 1.4f, cy + size * 0.18f, cx - size * 1.0f, cy - size * 1.0f, cx, cy - size * 0.32f);
            heart.cubicTo(cx + size * 1.0f, cy - size * 1.0f, cx + size * 1.4f, cy + size * 0.18f, cx, cy + size * 1.05f);
            heart.close();
            canvas.drawPath(heart, p);
        } else {
            heart.moveTo(cx, cy - size * 1.12f);
            heart.cubicTo(cx - size * 1.4f, cy - size * 0.2f, cx - size * 0.9f, cy + size * 0.72f, cx, cy + size * 0.12f);
            heart.cubicTo(cx + size * 0.9f, cy + size * 0.72f, cx + size * 1.4f, cy - size * 0.2f, cx, cy - size * 1.12f);
            heart.close();
            canvas.drawPath(heart, p);
            canvas.drawPolygon(std::vector<PointF>{{cx - size * 0.28f, cy}, {cx + size * 0.28f, cy},
                                                   {cx + size * 0.48f, cy + size * 1.0f},
                                                   {cx - size * 0.48f, cy + size * 1.0f}}, p);
        }
    }

    void drawStock(Canvas& canvas) {
        const RectF r = stockRect();
        const int deals = static_cast<int>(stock_.size() / 10);
        if (deals > 0) {
            canvas.save();
            canvas.translate(r.getX() + 7, r.getY() + 4);
            canvas.scale(0.43f, 0.43f);
            for (int i = std::min(3, deals - 1); i >= 0; --i)
                drawFaceDown(canvas, i * 13.0f, i * 5.0f, 1.0f);
            canvas.restore();
            Paint badge;
            badge.setStyle(Paint::Style::FILL);
            badge.setColor(Color(245, 200, 78));
            canvas.drawCircle(r.getX() + r.getWidth() - 7, r.getY() + 8, 12 + dealPulse_ * 3, badge);
            Paint count;
            count.setStyle(Paint::Style::FILL);
            count.setColor(Color(33, 38, 35));
            count.setTextSize(11);
            count.setTextAlign(Paint::TextAlign::CENTER);
            count.setTextBaseline(Paint::TextBaseline::MIDDLE);
            useUiFont(count, 800);
            canvas.drawText(std::to_string(deals), r.getX() + r.getWidth() - 7, r.getY() + 8, count);
        } else {
            Paint empty;
            empty.setStyle(Paint::Style::STROKE);
            empty.setStrokeWidth(1.4f);
            empty.setColor(Color(91, 190, 161, 70));
            canvas.drawRoundRect(r, 11, empty);
        }
        Paint label;
        label.setStyle(Paint::Style::FILL);
        label.setColor(Color(147, 211, 191));
        label.setTextSize(8);
        label.setLetterSpacing(0.6f);
        label.setTextAlign(Paint::TextAlign::CENTER);
        useUiFont(label, 700);
        canvas.drawText(deals > 0 ? "DEAL" : "EMPTY", r.getX() + r.getWidth() * 0.5f, r.getY() + 56, label);
        if (hintTime_ > 0 && hintDest_ == 10) {
            Paint h;
            h.setStyle(Paint::Style::STROKE);
            h.setStrokeWidth(3);
            h.setColor(Color(255, 211, 81, 220));
            canvas.drawRoundRect(RectF(r.getX() - 4, r.getY() - 4, r.getWidth() + 8, r.getHeight() + 8), 15, h);
        }
    }

    void drawToast(Canvas& canvas) {
        if (toastTime_ <= 0 || won_) return;
        const float alpha = std::min(1.0f, toastTime_ * 3.0f);
        Paint text;
        text.setStyle(Paint::Style::FILL);
        text.setTextSize(12);
        text.setTextAlign(Paint::TextAlign::CENTER);
        text.setTextBaseline(Paint::TextBaseline::MIDDLE);
        useUiFont(text, 650);
        const float w = canvas.measureTextMetrics(toast_, text).width + 46;
        const RectF rect((DESIGN_W - w) * 0.5f, 800, w, 38);
        Paint bg;
        bg.setStyle(Paint::Style::FILL);
        bg.setColor(Color(4, 29, 33, static_cast<int>(220 * alpha)));
        canvas.drawRoundRect(rect, 19, bg);
        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeWidth(1);
        border.setColor(Color(92, 215, 178, static_cast<int>(120 * alpha)));
        canvas.drawRoundRect(rect, 19, border);
        text.setColor(Color(223, 245, 237, static_cast<int>(255 * alpha)));
        canvas.drawText(toast_, DESIGN_W * 0.5f, 819, text);
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
        canvas.drawText("Final score  " + std::to_string(score_) + "   •   Click anywhere for a new deal", DESIGN_W * 0.5f, 520, sub);
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
    ctx->canvas->setSize(width, height);
    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowContentScale(window, &sx, &sy);
    const float dpr = sx > 0.0f ? sx : 1.0f;
    ctx->canvas->setDevicePixelRatio(dpr);
    ctx->windowW = static_cast<int>(width / dpr);
    ctx->windowH = static_cast<int>(height / dpr);
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
    } else if (key == GLFW_KEY_N || key == GLFW_KEY_R) ctx->game->newGame();
    else if (key == GLFW_KEY_U) ctx->game->undo();
    else if (key == GLFW_KEY_H) ctx->game->hint();
    else if (key == GLFW_KEY_1) ctx->game->setDifficulty(1);
    else if (key == GLFW_KEY_2) ctx->game->setDifficulty(2);
    else if (key == GLFW_KEY_4) ctx->game->setDifficulty(4);
}

} // namespace

int main(int argc, char** argv) {
    bool selfTest = false;
    bool playTest = false;
    bool exitAfterFrame = false;
    std::string capturePath;
    std::uint32_t seed = 0;
    int difficulty = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--self-test") selfTest = true;
        else if (arg == "--play-test") playTest = true;
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
                                          "Spider Solitaire — WhatsCanvas", nullptr, nullptr);
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
    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowContentScale(window, &sx, &sy);
    const float dpr = sx > 0.0f ? sx : 1.0f;
    canvas.setDevicePixelRatio(dpr);

    SpiderGame game(difficulty, seed);
    GameContext ctx{&game, &canvas, static_cast<int>(fbw / dpr), static_cast<int>(fbh / dpr)};
    glfwSetWindowUserPointer(window, &ctx);
    glfwSetFramebufferSizeCallback(window, framebufferCallback);
    glfwSetCursorPosCallback(window, cursorCallback);
    glfwSetMouseButtonCallback(window, mouseCallback);
    glfwSetKeyCallback(window, keyCallback);

    double last = glfwGetTime();
    bool captured = false;
    bool captureFailed = false;
    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - last);
        last = now;
        glClear(GL_COLOR_BUFFER_BIT);
        canvas.beginFrame();
        game.update(dt);
        game.render(canvas, ctx.windowW, ctx.windowH);
        canvas.endFrame();
        if (!captured && !capturePath.empty()) {
            captured = true;
            if (!canvas.savePixelsPPM(capturePath)) {
                std::cerr << "Failed to save capture: " << capturePath << '\n';
                captureFailed = true;
            } else {
                std::cout << "CAPTURED " << capturePath << '\n';
            }
        }
        glfwSwapBuffers(window);
        glfwPollEvents();
        if (exitAfterFrame && (capturePath.empty() || captured)) glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    canvas.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return captureFailed ? 1 : 0;
}
