#include "SpiderGameInternal.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>

namespace spider {

void SpiderGame::setTouchInputMode(bool touch) { touchInputMode_ = touch; }

void SpiderGame::setUsePictureCache(bool use) { usePictureCache_ = use; }

void SpiderGame::setRasterizePictures(bool rasterize) { rasterizePictures_ = rasterize; }

void SpiderGame::setUseCardAtlas(bool use) { useCardAtlas_ = use; }

void SpiderGame::releaseGpuResources() {
    cardAtlas_.reset();
    backgroundImage_.reset();
    backgroundViewportWidth_ = 0;
    backgroundViewportHeight_ = 0;
    tablePicture_.reset();
    columnPictures_.fill(nullptr);
    columnPictureDirty_.fill(true);
    stockBackPicture_.reset();
    faceDownPicture_.reset();
    emptySlotPicture_.reset();
    headerChromePicture_.reset();
    headerChromeSignature_.clear();
}

SpiderGame::SpiderGame(int difficulty, std::uint32_t seed)
    : difficulty_(difficulty), requestedSeed_(seed) {
    newGame(seed, false);
}

void SpiderGame::newGame(std::uint32_t seed, bool animate) {
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
    invalidateAllColumns();

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

void SpiderGame::startIntroAnimation() {
    std::vector<CardMotion> motions;
    const RectF stock = stockRect();
    const float originX = stock.getX() + stock.getWidth() * 0.5f - CARD_W * 0.5f;
    const float originY = stock.getY() + stock.getHeight() * 0.35f - CARD_H * 0.5f;
    for (int col = 0; col < 10; ++col) {
        if (columns_[col].empty()) continue;
        const int index = static_cast<int>(columns_[col].size()) - 1;
        const auto ys = columnPositions(col);
        motions.push_back({columns_[col][index], originX, originY,
                           columnX(col), ys[index], col * motion::kIntroStagger,
                           motion::kIntroDuration, MotionKind::Deal});
    }
    startMotions(std::move(motions));
}

void SpiderGame::startCompletionDemo(bool finalRun) {
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
    invalidateAllColumns();
}

void SpiderGame::update(float dt) {
    if (!won_) elapsed_ += std::min(std::max(dt, 0.0f), 0.1f);
    phase_ += dt;
    toastTime_ = std::max(0.0f, toastTime_ - dt);
    hintTime_ = std::max(0.0f, hintTime_ - dt);
    dealPulse_ = std::max(0.0f, dealPulse_ - dt * 2.4f);
    buttonPressTime_ = std::max(0.0f, buttonPressTime_ - dt);
    if (!motions_.empty()) {
        const float previousMotionTime = motionTime_;
        motionTime_ += dt;
        float end = 0.0f;
        for (const CardMotion& motion : motions_) {
            end = std::max(end, motion.delay + motion.duration);
            const float landingTime = motion.delay + motion.duration;
            if (motionsInvalidateOnLanding_ &&
                previousMotionTime < landingTime && motionTime_ >= landingTime) {
                float x = 0.0f;
                float y = 0.0f;
                if (findColumnPosition(motion.card.id, x, y)) {
                    const int column = static_cast<int>(std::lround(
                        (x - COL_X) / COL_GAP));
                    invalidateColumn(column);
                }
            }
        }
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

void SpiderGame::setDifficulty(int suits, bool animate) {
    if (suits != 1 && suits != 2 && suits != 4) return;
    difficulty_ = suits;
    requestedSeed_ = 0;
    newGame(0, animate);
}

void SpiderGame::cycleDifficulty(bool animate) {
    setDifficulty(difficulty_ == 1 ? 2 : difficulty_ == 2 ? 4 : 1, animate);
}

int SpiderGame::difficulty() const { return difficulty_; }

bool SpiderGame::hasStock() const { return !stock_.empty(); }

bool SpiderGame::movableRun(int col, int index) const {
    if (col < 0 || col >= 10 || index < 0 || index >= static_cast<int>(columns_[col].size())) return false;
    const auto& cards = columns_[col];
    if (!cards[index].faceUp) return false;
    for (int i = index; i + 1 < static_cast<int>(cards.size()); ++i) {
        if (!cards[i + 1].faceUp || cards[i].suit != cards[i + 1].suit ||
            cards[i].rank != cards[i + 1].rank + 1) return false;
    }
    return true;
}

bool SpiderGame::canMove(int src, int index, int dest) const {
    if (src == dest || dest < 0 || dest >= 10 || !movableRun(src, index)) return false;
    const Card& first = columns_[src][index];
    return columns_[dest].empty() || columns_[dest].back().rank == first.rank + 1;
}

int SpiderGame::findBestDestination(int src, int index) const {
    if (!movableRun(src, index)) return -1;
    int bestDest = -1;
    int bestScore = -1;
    for (int dest = 0; dest < 10; ++dest) {
        if (!canMove(src, index, dest)) continue;
        int value = columns_[dest].empty() ? 5 : 20;
        if (!columns_[dest].empty() && columns_[dest].back().suit == columns_[src][index].suit) value += 40;
        if (index > 0 && !columns_[src][index - 1].faceUp) value += 80;
        if (columns_[src][index].rank == 13 && columns_[dest].empty()) value += 10;
        if (value > bestScore) {
            bestScore = value;
            bestDest = dest;
        }
    }
    return bestDest;
}

bool SpiderGame::moveRun(int src, int index, int dest) {
    if (!canMove(src, index, dest)) return false;
    finishMotions();
    std::vector<CardMotion> motions;
    const auto sourceYs = columnPositions(src);
    const float sourceX = dragging_ ? pointerX_ - dragOffsetX_ : columnX(src);
    const float sourceY = dragging_ ? pointerY_ - dragOffsetY_ : sourceYs[index];
    for (int i = index; i < static_cast<int>(columns_[src].size()); ++i) {
        motions.push_back({columns_[src][i], sourceX,
                           sourceY + (sourceYs[i] - sourceYs[index]), 0, 0, 0,
                           motion::kPlaceDuration, MotionKind::Place});
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
    // Structural change is limited to source and destination columns; keep
    // the other eight per-column pictures cached so the drag/release
    // hitch is O(2 columns) instead of O(10 columns).
    invalidateColumn(src);
    invalidateColumn(dest);
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

bool SpiderGame::dealStock() {
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
                           col * motion::kDealStagger, motion::kDealDuration,
                           MotionKind::Deal});
    }
    ++moves_;
    score_ = std::max(0, score_ - 1);
    selectedColumn_ = selectedIndex_ = -1;
    collectCompleteRuns();
    // Existing column textures already contain exactly the pre-deal tableau.
    // Reuse them while the new cards travel, then refresh one destination
    // column as each staggered card lands.
    startMotions(std::move(motions), false, true);
    dealPulse_ = 1.0f;
    setToast("A new row was dealt", 1.7f);
    std::cout << "DEAL remaining=" << stock_.size() << '\n';
    return true;
}

bool SpiderGame::undo() {
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
    invalidateAllColumns();
    setToast("Move undone", 1.5f);
    std::cout << "UNDO history=" << history_.size() << '\n';
    return true;
}

void SpiderGame::hint() {
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

std::pair<float, float> SpiderGame::toDesign(float windowX, float windowY) const {
    return {(windowX - renderOffsetX_) / std::max(renderScale_, 0.0001f),
            (windowY - renderOffsetY_) / std::max(renderScale_, 0.0001f)};
}

std::pair<float, float> SpiderGame::toWindow(float designX, float designY) const {
    return {renderOffsetX_ + designX * renderScale_,
            renderOffsetY_ + designY * renderScale_};
}

bool SpiderGame::difficultyHoverMatches() const {
    return inside(difficultyButton(), pointerX_, pointerY_) &&
           !inside(hintButton(), pointerX_, pointerY_);
}

bool SpiderGame::runSelfTests() {
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
    ok &= expect(game.motions_.size() == 10, "deal creates ten card motions");
    ok &= expect(std::all_of(game.motions_.begin(), game.motions_.end(),
                            [](const CardMotion& item) {
                                return item.kind == MotionKind::Deal &&
                                       item.duration >= 0.2f;
                            }),
                 "deal motions retain enough frames for smooth playback");
    for (const auto& col : game.columns_) ok &= expect(col.back().faceUp, "dealt cards are face up");

    std::cout << "SELF_TEST " << (ok ? "PASS" : "FAIL") << " checks=" << checks << '\n';
    return ok;
}

bool SpiderGame::runInteractionTests() {
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
    ok &= expect(std::all_of(game.motions_.begin(), game.motions_.end(),
                            [](const CardMotion& item) {
                                return item.kind == MotionKind::Place &&
                                       item.duration >= 0.15f;
                            }),
                 "landing motion is smooth and uses the place curve");

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
    ok &= expect(std::all_of(game.motions_.begin(), game.motions_.end(),
                            [](const CardMotion& item) {
                                return item.kind == MotionKind::SnapBack &&
                                       item.duration >= 0.13f;
                            }),
                 "snap-back motion is smooth and explicitly typed");

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

void SpiderGame::replayPicture(Canvas& canvas, const Picture& picture,
                   bool smallSprite) const {
    if (smallSprite || rasterizePictures_) canvas.drawPictureRasterized(picture);
    else canvas.drawPicture(picture);
}

void SpiderGame::invalidateAllColumns() {
    columnPictureDirty_.fill(true);
}

void SpiderGame::invalidateColumn(int col) {
    if (col >= 0 && col < static_cast<int>(columnPictureDirty_.size())) {
        columnPictureDirty_[static_cast<std::size_t>(col)] = true;
    }
}

bool SpiderGame::findColumnPosition(std::uint32_t id, float& x, float& y) const {
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

bool SpiderGame::isCardAnimating(std::uint32_t id) const {
    return std::any_of(motions_.begin(), motions_.end(),
                       [this, id](const CardMotion& motion) {
                           return motion.card.id == id &&
                                  motionTime_ < motion.delay + motion.duration;
                       });
}

void SpiderGame::finishMotions() {
    if (motions_.empty()) return;
    // Cards being animated are excluded from their column's cached
    // Picture via isCardAnimating(). Once motions clear we must
    // re-record only the columns those cards belonged to. Snap-back
    // motions are already excluded by selection and skip this work.
    std::array<bool, 10> touched{};
    if (motionsInvalidateOnLanding_) {
        for (const CardMotion& motion : motions_) {
            // Natural landings are invalidated incrementally in update(). Only
            // an interrupted animation still needs its destination refreshed.
            if (motionTime_ >= motion.delay + motion.duration) continue;
            for (int col = 0; col < 10; ++col) {
                for (const Card& c : columns_[col]) {
                    if (c.id == motion.card.id) { touched[col] = true; break; }
                }
            }
        }
    }
    motions_.clear();
    motionTime_ = 0.0f;
    motionsInvalidateAtStart_ = true;
    motionsInvalidateOnLanding_ = true;
    for (int col = 0; col < 10; ++col) if (touched[col]) invalidateColumn(col);
}

void SpiderGame::startMotions(std::vector<CardMotion> motions,
                              bool invalidateAtStart,
                              bool invalidateOnLanding) {
    finishMotions();
    motions_ = std::move(motions);
    motionTime_ = 0.0f;
    motionsInvalidateAtStart_ = invalidateAtStart;
    motionsInvalidateOnLanding_ = invalidateOnLanding;
    // Columns whose cards just started animating need to exclude those
    // cards from their per-column Picture.
    std::array<bool, 10> touched{};
    if (motionsInvalidateAtStart_) {
        for (const CardMotion& motion : motions_) {
            for (int col = 0; col < 10; ++col) {
                for (const Card& c : columns_[col]) {
                    if (c.id == motion.card.id) { touched[col] = true; break; }
                }
            }
        }
    }
    for (int col = 0; col < 10; ++col) if (touched[col]) invalidateColumn(col);
}

void SpiderGame::pressButton(int id) {
    pressedButton_ = id;
    buttonPressTime_ = 0.13f;
}

void SpiderGame::saveUndo() {
    history_.push_back({columns_, stock_, completed_, moves_, score_, elapsed_});
    if (history_.size() > 100) history_.erase(history_.begin());
}

void SpiderGame::setToast(std::string text, float time) {
    toast_ = std::move(text);
    const float readableTime = std::max(time, 3.4f);
    toastTime_ = readableTime;
    toastDuration_ = readableTime;
}

void SpiderGame::queueCompletionAnimation(int column, size_t start, const std::vector<float>& ys,
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
                                      baseDelay + i * 0.018f, 0.28f,
                                      MotionKind::Complete});
    }
}

void SpiderGame::collectCompleteRuns() {
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
            invalidateColumn(column);
            break;
        }
    } while (collectedAny);
}

} // namespace spider
