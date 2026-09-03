#include "SpiderGameInternal.h"

namespace spider {

void SpiderGame::pointerMove(float x, float y) {
    pointerX_ = x; pointerY_ = y;
    if (mouseDown_ && selectedColumn_ >= 0) {
        const float dx = x - pressX_, dy = y - pressY_;
        const float threshold = touchInputMode_ ? 220.0f : 30.0f;
        if (!dragging_ && dx * dx + dy * dy > threshold) dragging_ = true;
    }
}

void SpiderGame::pointerDown(float x, float y) {
    finishMotions(); pointerMove(x, y); mouseDown_ = true;
    pressX_ = x; pressY_ = y; dragging_ = false;
    if (inside(newButton(), x, y)) { pressButton(1); newGame(0, true); mouseDown_ = false; return; }
    if (inside(difficultyButton(), x, y)) { pressButton(2); cycleDifficulty(true); mouseDown_ = false; return; }
    if (inside(undoButton(), x, y)) { pressButton(3); undo(); mouseDown_ = false; return; }
    if (inside(hintButton(), x, y)) { pressButton(4); hint(); mouseDown_ = false; return; }
    if (inside(stockRect(), x, y)) { pressButton(5); dealStock(); mouseDown_ = false; return; }
    if (won_) { newGame(0, true); mouseDown_ = false; return; }
    HitCard hit = hitCard(x, y);
    if (hit.column >= 0 && hit.index >= 0) {
        if (selectedColumn_ >= 0 && hit.column != selectedColumn_ && canMove(selectedColumn_, selectedIndex_, hit.column)) { moveRun(selectedColumn_, selectedIndex_, hit.column); mouseDown_ = false; return; }
        if (movableRun(hit.column, hit.index)) {
            const int prev = selectedColumn_; selectedColumn_ = hit.column; selectedIndex_ = hit.index;
            invalidateColumn(prev); invalidateColumn(hit.column);
            dragOffsetX_ = x - columnX(hit.column); dragOffsetY_ = y - columnPositions(hit.column)[hit.index];
        } else {
            if (selectedColumn_ >= 0) { const int prev = selectedColumn_; selectedColumn_ = selectedIndex_ = -1; invalidateColumn(prev); }
            setToast("Only a same-suit descending run can move", 2.0f);
        }
        return;
    }
    const int empty = hitColumn(x, y);
    if (empty >= 0 && selectedColumn_ >= 0 && canMove(selectedColumn_, selectedIndex_, empty)) { moveRun(selectedColumn_, selectedIndex_, empty); mouseDown_ = false; }
    else if (selectedColumn_ >= 0) { const int prev = selectedColumn_; selectedColumn_ = selectedIndex_ = -1; invalidateColumn(prev); }
}

void SpiderGame::pointerUp(float x, float y) {
    pointerMove(x, y);
    if (mouseDown_ && dragging_ && selectedColumn_ >= 0) {
        const int dest = hitColumn(x, y);
        if (!(dest >= 0 && dest != selectedColumn_ && moveRun(selectedColumn_, selectedIndex_, dest))) {
            std::vector<CardMotion> motions; const auto ys = columnPositions(selectedColumn_);
            const float fromX = pointerX_ - dragOffsetX_, fromY = pointerY_ - dragOffsetY_;
            for (int i = selectedIndex_; i < static_cast<int>(columns_[selectedColumn_].size()); ++i)
                motions.push_back({columns_[selectedColumn_][i], fromX, fromY + (ys[i] - ys[selectedIndex_]), columnX(selectedColumn_), ys[i], 0.0f, motion::kSnapBackDuration, MotionKind::SnapBack});
            startMotions(std::move(motions), false, false); setToast("That run cannot be placed there", 1.8f);
        }
    } else if (touchInputMode_ && mouseDown_ && !dragging_ && selectedColumn_ >= 0) {
        const int dest = findBestDestination(selectedColumn_, selectedIndex_); if (dest >= 0) moveRun(selectedColumn_, selectedIndex_, dest);
    }
    mouseDown_ = false; dragging_ = false;
}

void SpiderGame::cancelSelection() {
    finishMotions(); const int prev = selectedColumn_; selectedColumn_ = selectedIndex_ = -1; mouseDown_ = dragging_ = false;
    if (prev >= 0) invalidateColumn(prev);
}

} // namespace spider
