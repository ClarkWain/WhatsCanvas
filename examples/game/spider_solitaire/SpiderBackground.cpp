#include "SpiderGameInternal.h"

namespace spider {

namespace {

constexpr int kBackgroundBakeWidth = 512;

void configureFeltGradient(Paint& paint, float centerX, float centerY, float radius) {
    paint.setStyle(Paint::Style::FILL);
    paint.setRadialGradient(centerX, centerY, radius,
                            std::vector<Paint::ColorStop>{
                                {0.0f, Color(23, 116, 77)},
                                {0.48f, Color(11, 78, 54)},
                                {1.0f, Color(4, 42, 30)},
                            });
}

void configureTopLight(Paint& paint, float top, float bottom) {
    paint.setStyle(Paint::Style::FILL);
    paint.setLinearGradient(0, top, 0, bottom,
                            Color(83, 158, 111, 14), Color(4, 42, 30, 0));
}

} // namespace

void SpiderGame::drawOutsideContents(Canvas& canvas, int width, int height) {
    const float centerX = renderOffsetX_ + DESIGN_W * renderScale_ * 0.5f;
    const float centerY = renderOffsetY_ + DESIGN_H * renderScale_ * 0.5f;

    Paint felt;
    configureFeltGradient(felt, centerX, centerY, 790.0f * renderScale_);
    canvas.drawRect(RectF(0, 0, static_cast<float>(width), static_cast<float>(height)), felt);

    const float contentTop = renderOffsetY_ + 104.0f * renderScale_;
    const float lightBottom = renderOffsetY_ + 520.0f * renderScale_;
    Paint topLight;
    configureTopLight(topLight, contentTop, lightBottom);
    canvas.drawRect(RectF(0, contentTop, static_cast<float>(width),
                          std::max(0.0f, lightBottom - contentTop)), topLight);

    Paint header;
    header.setStyle(Paint::Style::FILL);
    header.setColor(Color(4, 29, 23, 238));
    canvas.drawRect(RectF(0, renderOffsetY_, static_cast<float>(width),
                          104.0f * renderScale_), header);

    Paint divider;
    divider.setStyle(Paint::Style::FILL);
    divider.setLinearGradient(0, 0, static_cast<float>(width), 0,
                              Color(193, 157, 98, 20), Color(222, 188, 126, 170));
    canvas.drawRect(RectF(0, renderOffsetY_ + 103.0f * renderScale_,
                          static_cast<float>(width), std::max(1.0f, renderScale_)), divider);
}

bool SpiderGame::ensureBackgroundImage(Canvas& canvas, int width, int height) {
    if (backgroundImage_ && backgroundImage_->isTextureValid() &&
        backgroundViewportWidth_ == width && backgroundViewportHeight_ == height) {
        return true;
    }
    if (width <= 0 || height <= 0) return false;

    const int bakeHeight = std::max(1, static_cast<int>(std::lround(
        kBackgroundBakeWidth * static_cast<float>(height) / static_cast<float>(width))));
    auto scratch = Canvas::create(Canvas::Backend::Software,
                                  kBackgroundBakeWidth, bakeHeight);
    if (!scratch || !scratch->initializeContext()) return false;

    const float bakeScale = static_cast<float>(kBackgroundBakeWidth) / width;
    scratch->beginFrame();
    scratch->save();
    scratch->scale(bakeScale, bakeScale);
    drawOutsideContents(*scratch, width, height);
    scratch->restore();
    scratch->endFrame();

    std::vector<unsigned char> pixels;
    const bool read = scratch->readPixelsRGBA(pixels);
    scratch->finalizeContext();
    if (!read || pixels.size() != static_cast<std::size_t>(
            kBackgroundBakeWidth * bakeHeight * 4)) {
        return false;
    }

    auto image = std::make_unique<Image>();
    if (!image->loadFromRGBA(canvas, pixels, kBackgroundBakeWidth, bakeHeight, false)) {
        return false;
    }
    backgroundImage_ = std::move(image);
    backgroundViewportWidth_ = width;
    backgroundViewportHeight_ = height;
    return true;
}

void SpiderGame::drawOutside(Canvas& canvas, int width, int height) {
    if (!ensureBackgroundImage(canvas, width, height)) {
        drawOutsideContents(canvas, width, height);
        return;
    }
    Paint image;
    image.setColor(Color::WHITE);
    canvas.drawImage(*backgroundImage_,
                     RectF(0, 0, static_cast<float>(backgroundImage_->getWidth()),
                           static_cast<float>(backgroundImage_->getHeight())),
                     RectF(0, 0, static_cast<float>(width), static_cast<float>(height)),
                     image);
}

} // namespace spider
