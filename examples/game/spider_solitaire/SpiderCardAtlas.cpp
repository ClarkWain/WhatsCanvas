#include "SpiderGameInternal.h"

namespace spider {

namespace {

constexpr const char* kWebLatinFontPath = "/fonts/Roboto-Regular.ttf";
constexpr const char* kWebCjkFontPath = "/fonts/Mplus1p-Regular.ttf";
constexpr const char* kWebEmojiFontPath = "/fonts/NotoColorEmoji.demo.subset.ttf";

}

void SpiderGame::drawFaceBaseContents(Canvas& canvas, float x, float y, float alpha) {
    drawCardShadow(canvas, x, y, false, alpha);
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
    border.setStrokeWidth(0.8f);
    border.setColor(Color(178, 168, 151, static_cast<int>(210 * alpha)));
    canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 10, border);
}

bool SpiderGame::ensureCardAtlas(Canvas& canvas) {
    if (cardAtlas_ && cardAtlas_->isTextureValid()) return true;

    // Bake all faces into one shared image. This is still a single GPU texture
    // (not 52 separately managed images), but reduces each visible card from
    // six atlas draws to one. A 1024 texture is deliberate: cards are only
    // about 116 physical pixels wide on the reference phone, while a 2048
    // atlas quadruples bake time and hurts the older GPU's texture cache.
    constexpr int atlasW = 1024;
    constexpr int atlasH = 1024;
    constexpr int columns = 8;
    constexpr int cellW = 112;
    constexpr int cellH = 144;
    auto scratch = Canvas::create(Canvas::Backend::Software, atlasW, atlasH);
    if (!scratch || !scratch->initializeContext()) return false;

    // Keep card-glyph font selection aligned with the main Web canvas.
    scratch->registerFontFace(wsc::FontFace::fromFile(
        wsc::FontDescriptor(wsc::FontSystem::kDefaultPrimaryFamily, 400),
        kWebLatinFontPath));
    scratch->registerFontFace(wsc::FontFace::fromFile(
        wsc::FontDescriptor(wsc::FontSystem::kDefaultCjkFamily, 400),
        kWebCjkFontPath));
    scratch->registerFontFace(wsc::FontFace::fromFile(
        wsc::FontDescriptor(wsc::FontSystem::kDefaultSymbolFamily, 400),
        kWebEmojiFontPath));

    for (const wsc::FontFace& face : wsc::FontSystem::defaultSystemFontFaces()) {
        scratch->registerFontFace(face);
    }
    wsc::FontFallbackChain primary(wsc::FontSystem::kDefaultPrimaryFamily);
    primary.addFallbackFamily(wsc::FontSystem::kDefaultCjkFamily);
    primary.addFallbackFamily(wsc::FontSystem::kDefaultSymbolFamily);
    scratch->setFontFallbackChain(primary);
    wsc::FontFallbackChain cjk(wsc::FontSystem::kDefaultCjkFamily);
    cjk.addFallbackFamily(wsc::FontSystem::kDefaultSymbolFamily);
    scratch->setFontFallbackChain(cjk);
    scratch->beginFrame();

    const bool atlasWasEnabled = useCardAtlas_;
    useCardAtlas_ = false;
    for (int suit = 0; suit < 4; ++suit) {
        for (int rank = 1; rank <= 13; ++rank) {
            const int index = suit * 13 + rank - 1;
            const float x = static_cast<float>((index % columns) * cellW);
            const float y = static_cast<float>((index / columns) * cellH);
            drawFaceUp(*scratch,
                       Card{rank, static_cast<Suit>(suit), true, 0},
                       x, y, false, false, 1.0f);
        }
    }
    constexpr int backIndex = 52;
    drawFaceDownContents(*scratch,
                         static_cast<float>((backIndex % columns) * cellW),
                         static_cast<float>((backIndex / columns) * cellH),
                         1.0f);
    useCardAtlas_ = atlasWasEnabled;

    scratch->endFrame();
    std::vector<unsigned char> pixels;
    if (!scratch->readPixelsRGBA(pixels)
        || pixels.size() != static_cast<std::size_t>(atlasW * atlasH * 4)) {
        scratch->finalizeContext();
        return false;
    }
    scratch->finalizeContext();

    auto atlas = std::make_unique<Image>();
    if (!atlas->loadFromRGBA(canvas, pixels, atlasW, atlasH, false)) {
        return false;
    }
    cardAtlas_ = std::move(atlas);
    return true;
}

void SpiderGame::drawAtlasSprite(Canvas& canvas, const RectF& src, const RectF& dst,
                     const Color& tint, float alpha) {
    if (!cardAtlas_ || !cardAtlas_->isTextureValid()) return;
    Paint image;
    image.setColor(tint);
    image.setAlpha(alpha);
    canvas.drawImage(*cardAtlas_, src, dst, image);
}

void SpiderGame::drawAtlasFace(Canvas& canvas, const Card& card, float x, float y,
                   bool selected, bool hinted, float alpha) {
    constexpr int columns = 8;
    constexpr int cellW = 112;
    constexpr int cellH = 144;
    const int index = static_cast<int>(card.suit) * 13 +
                      std::clamp(card.rank, 1, 13) - 1;
    drawAtlasSprite(canvas,
                    RectF(static_cast<float>((index % columns) * cellW),
                          static_cast<float>((index / columns) * cellH), 98, 136),
                    RectF(x, y, CARD_W, CARD_H), Color::WHITE, alpha);
    if (selected || hinted) {
        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeWidth(2.2f);
        border.setColor(selected ? Color(232, 188, 103, static_cast<int>(245 * alpha))
                                 : Color(238, 190, 96, static_cast<int>(230 * alpha)));
        canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 10, border);
    }
}

void SpiderGame::drawAtlasBack(Canvas& canvas, float x, float y, float alpha) {
    constexpr int backIndex = 52;
    constexpr int columns = 8;
    constexpr int cellW = 112;
    constexpr int cellH = 144;
    drawAtlasSprite(canvas,
                    RectF(static_cast<float>((backIndex % columns) * cellW),
                          static_cast<float>((backIndex / columns) * cellH), 98, 136),
                    RectF(x, y, CARD_W, CARD_H), Color::WHITE, alpha);
}

} // namespace spider
