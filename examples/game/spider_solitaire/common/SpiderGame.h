// Shared Spider Solitaire game model used by the desktop and Android hosts.
//
// Keep this file as the readable map of the example. Implementation details are
// grouped by responsibility in SpiderGame.cpp, SpiderGameRenderer.cpp, and
// SpiderCardAtlas.cpp.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "wsc/wsc.h"

namespace spider {

using wsc::Canvas;
using wsc::Color;
using wsc::Image;
using wsc::Paint;
using wsc::Path;
using wsc::Picture;
using wsc::PointF;
using wsc::RectF;

constexpr float DESIGN_W = 1280.0f;
constexpr float DESIGN_H = 860.0f;
constexpr float CARD_W = 98.0f;
constexpr float CARD_H = 136.0f;
constexpr float COL_X = 34.0f;
constexpr float COL_GAP = 124.0f;
constexpr float TABLE_Y = 176.0f;
constexpr float TABLE_BOTTOM = 814.0f;

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

enum class MotionKind {
    Deal,
    Place,
    SnapBack,
    Complete,
};

struct CardMotion {
    Card card;
    float fromX = 0.0f;
    float fromY = 0.0f;
    float toX = 0.0f;
    float toY = 0.0f;
    float delay = 0.0f;
    float duration = 0.2f;
    MotionKind kind = MotionKind::Place;
};

class SpiderGame {
public:
    // Configuration and lifecycle.
    void setTouchInputMode(bool touch);
    void setUsePictureCache(bool use);
    void setRasterizePictures(bool rasterize);
    void setUseCardAtlas(bool use);
    void releaseGpuResources();
    explicit SpiderGame(int difficulty = 1, std::uint32_t seed = 0);
    void newGame(std::uint32_t seed = 0, bool animate = true);
    void startIntroAnimation();
    void startCompletionDemo(bool finalRun = false);
    void update(float dt);
    void setDifficulty(int suits, bool animate = true);
    void cycleDifficulty(bool animate = true);
    int difficulty() const;
    bool hasStock() const;
    bool movableRun(int col, int index) const;
    bool canMove(int src, int index, int dest) const;
    int findBestDestination(int src, int index) const;
    bool moveRun(int src, int index, int dest);
    bool dealStock();
    bool undo();
    void hint();
    void pointerMove(float x, float y);
    void pointerDown(float x, float y);
    void pointerUp(float x, float y);
    void cancelSelection();
    void updateRenderTransform(int windowW, int windowH);
    void render(Canvas& canvas, int windowW, int windowH);
    std::pair<float, float> toDesign(float windowX, float windowY) const;
    std::pair<float, float> toWindow(float designX, float designY) const;
    bool difficultyHoverMatches() const;
    static bool runSelfTests();
    static bool runInteractionTests();

private:
    // Game model.
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

    // Pointer interaction and animation state.
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
    bool motionsInvalidateAtStart_ = true;
    bool motionsInvalidateOnLanding_ = true;
    std::vector<CardMotion> completionMotions_;
    float completionMotionTime_ = 0.0f;
    std::string toast_;
    std::string measuredToast_;
    float measuredToastWidth_ = 0.0f;

    // Reusable rendering resources and viewport transform.
    std::unique_ptr<Image> backgroundImage_;
    int backgroundViewportWidth_ = 0;
    int backgroundViewportHeight_ = 0;
    std::shared_ptr<const Picture> tablePicture_;
    std::array<std::shared_ptr<const Picture>, 10> columnPictures_{};
    std::array<bool, 10> columnPictureDirty_{{true, true, true, true, true,
                                              true, true, true, true, true}};
    std::shared_ptr<const Picture> stockBackPicture_;
    std::shared_ptr<const Picture> faceDownPicture_;
    std::shared_ptr<const Picture> emptySlotPicture_;
    std::shared_ptr<const Picture> headerChromePicture_;
    std::string headerChromeSignature_;
    float renderScale_ = 1.0f;
    float renderOffsetX_ = 0.0f;
    float renderOffsetY_ = 0.0f;
    bool touchInputMode_ = false;
    bool usePictureCache_ = true;
    bool rasterizePictures_ = true;
    bool useCardAtlas_ = false;
    std::unique_ptr<Image> cardAtlas_;

    // Game and input helpers.
    // Small always-static Pictures (face-down card, empty slot, stock back,
    // header chrome) benefit from offscreen rasterization: a small texture
    // is blitted per replay. The large tableau and background pictures
    // change too often on mobile — one invalidation of the offscreen cache
    // costs ~30 ms of stall. This helper differentiates: `smallSprite=true`
    // always rasterizes, `false` follows the rasterizePictures_ flag.
    void replayPicture(Canvas& canvas, const Picture& picture,
                           bool smallSprite = false) const;
    void invalidateAllColumns();
    void invalidateColumn(int col);
    float columnX(int col) const;
    RectF newButton() const;
    RectF difficultyButton() const;
    RectF undoButton() const;
    RectF hintButton() const;
    RectF stockRect() const;
    bool findColumnPosition(std::uint32_t id, float& x, float& y) const;
    bool isCardAnimating(std::uint32_t id) const;
    void finishMotions();
    void startMotions(std::vector<CardMotion> motions,
                      bool invalidateAtStart = true,
                      bool invalidateOnLanding = true);
    void pressButton(int id);
    void saveUndo();
    void setToast(std::string text, float time);
    void queueCompletionAnimation(int column, size_t start, const std::vector<float>& ys,
                                      int completedSlot);
    void collectCompleteRuns();
    std::vector<float> columnPositions(int col) const;
    int hitColumn(float x, float y) const;
    HitCard hitCard(float x, float y) const;

    // Renderer helpers (implemented in SpiderGameRenderer.cpp).
    void drawOutside(Canvas& canvas, int width, int height);
    void drawOutsideContents(Canvas& canvas, int width, int height);
    bool ensureBackgroundImage(Canvas& canvas, int width, int height);
    void drawTable(Canvas& canvas);
    void drawSpiderMark(Canvas& canvas, float cx, float cy, float s, const Color& color) const;
    void drawHeader(Canvas& canvas);
    void drawHeaderDynamic(Canvas& canvas);
    void drawMetricValue(Canvas& canvas, float x, const std::string& value);
    void ensureHeaderChrome(Canvas& canvas);
    void drawHeaderChromeContents(Canvas& canvas);
    void drawMetricLabel(Canvas& canvas, float x, const std::string& label);
    void drawButton(Canvas& canvas, const RectF& rect, const std::string& label,
                        bool enabled, int id, bool primary);
    void drawColumnBase(Canvas& canvas, int col);
    void drawTableauBase(Canvas& canvas);
    void drawColumns(Canvas& canvas);
    void drawEmptySlot(Canvas& canvas, int col);
    void drawEmptySlotContents(Canvas& canvas, float x, float y);

    // Shared card atlas (implemented in SpiderCardAtlas.cpp).
    void drawFaceBaseContents(Canvas& canvas, float x, float y, float alpha);
    bool ensureCardAtlas(Canvas& canvas);
    void drawAtlasSprite(Canvas& canvas, const RectF& src, const RectF& dst,
                             const Color& tint, float alpha = 1.0f);
    void drawAtlasFace(Canvas& canvas, const Card& card, float x, float y,
                           bool selected, bool hinted, float alpha);
    void drawAtlasBack(Canvas& canvas, float x, float y, float alpha);
    void drawCard(Canvas& canvas, const Card& card, float x, float y, bool selected, bool hinted, float alpha);
    void drawMotionCard(Canvas& canvas, const Card& card, float x, float y, float alpha);
    void drawCardShadow(Canvas& canvas, float x, float y, bool selected, float alpha) const;
    void drawFaceUp(Canvas& canvas, const Card& card, float x, float y, bool selected, bool hinted, float alpha);
    void drawFaceDown(Canvas& canvas, float x, float y, float alpha);
    void drawFaceDownContents(Canvas& canvas, float x, float y, float alpha);
    void drawSuit(Canvas& canvas, Suit suit, float cx, float cy, float size, const Color& color) const;
    void drawStockBack(Canvas& canvas, float x, float y);
    void drawStockBackContents(Canvas& canvas, float x, float y);
    void drawStock(Canvas& canvas);
    void drawToast(Canvas& canvas);
    void drawWin(Canvas& canvas);
};

} // namespace spider
