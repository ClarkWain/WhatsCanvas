#include "SpiderGameInternal.h"

namespace spider {

float SpiderGame::columnX(int col) const { return COL_X + col * COL_GAP; }
RectF SpiderGame::newButton() const { return RectF(760, 28, 116, 44); }
RectF SpiderGame::difficultyButton() const { return RectF(884, 28, 112, 44); }
RectF SpiderGame::undoButton() const { return RectF(1004, 28, 98, 44); }
RectF SpiderGame::hintButton() const { return RectF(1110, 28, 104, 44); }
RectF SpiderGame::stockRect() const { return RectF(1148, 676, 100, 148); }

std::vector<float> SpiderGame::columnPositions(int col) const {
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

int SpiderGame::hitColumn(float x, float y) const {
    if (y < TABLE_Y - 16.0f || y > TABLE_BOTTOM + 18.0f) return -1;
    for (int col = 0; col < 10; ++col)
        if (x >= columnX(col) - 8.0f && x <= columnX(col) + CARD_W + 8.0f) return col;
    return -1;
}

HitCard SpiderGame::hitCard(float x, float y) const {
    const int col = hitColumn(x, y);
    if (col < 0 || columns_[col].empty()) return {};
    const auto ys = columnPositions(col);
    for (int i = static_cast<int>(ys.size()) - 1; i >= 0; --i) {
        const float bottom = i + 1 == static_cast<int>(ys.size()) ? ys[i] + CARD_H : ys[i + 1];
        if (y >= ys[i] && y <= bottom) return {col, i};
    }
    return {};
}

} // namespace spider
