#include <jni.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <dlfcn.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <wsc/wsc.h>

namespace {

constexpr const char* kLogTag = "WhatsCanvas";

constexpr const char* kSansFamily = "AndroidSans";
constexpr const char* kCjkFamily = "AndroidCJK";
constexpr float kPi = 3.14159265358979323846f;

void logError(const char* message)
{
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", message);
}

void* loadOpenGlesProcedure(const char* name)
{
    const auto procedure = eglGetProcAddress(name);
    if (procedure != nullptr) {
        return reinterpret_cast<void*>(procedure);
    }
    return dlsym(RTLD_DEFAULT, name);
}

bool registerFontFile(wsc::Canvas& canvas, const char* family,
                      const char* path, int faceIndex = 0, int weight = 400,
                      wsc::FontSlant slant = wsc::FontSlant::NORMAL)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.good()) {
        return false;
    }

    const bool registered = canvas.registerFontFace(wsc::FontFace::fromFile(
        wsc::FontDescriptor(family, weight, slant), path, faceIndex));
    __android_log_print(registered ? ANDROID_LOG_INFO : ANDROID_LOG_WARN,
                        kLogTag, "Font %s: %s (%s)", family,
                        registered ? "registered" : "registration failed", path);
    return registered;
}

struct AFontMatcher;
struct AFont;

struct AndroidFontApi {
    using CreateMatcher = AFontMatcher* (*)();
    using DestroyMatcher = void (*)(AFontMatcher*);
    using SetLocales = void (*)(AFontMatcher*, const char*);
    using Match = AFont* (*)(const AFontMatcher*, const char*,
                             const std::uint16_t*, std::uint32_t,
                             std::uint32_t*);
    using CloseFont = void (*)(AFont*);
    using GetPath = const char* (*)(const AFont*);
    using GetWeight = std::uint16_t (*)(const AFont*);
    using IsItalic = bool (*)(const AFont*);
    using GetCollectionIndex = std::size_t (*)(const AFont*);

    void* library = nullptr;
    CreateMatcher createMatcher = nullptr;
    DestroyMatcher destroyMatcher = nullptr;
    SetLocales setLocales = nullptr;
    Match match = nullptr;
    CloseFont closeFont = nullptr;
    GetPath getPath = nullptr;
    GetWeight getWeight = nullptr;
    IsItalic isItalic = nullptr;
    GetCollectionIndex getCollectionIndex = nullptr;

    AndroidFontApi()
    {
        library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (!library) {
            return;
        }
        createMatcher = reinterpret_cast<CreateMatcher>(
            dlsym(library, "AFontMatcher_create"));
        destroyMatcher = reinterpret_cast<DestroyMatcher>(
            dlsym(library, "AFontMatcher_destroy"));
        setLocales = reinterpret_cast<SetLocales>(
            dlsym(library, "AFontMatcher_setLocales"));
        match = reinterpret_cast<Match>(dlsym(library, "AFontMatcher_match"));
        closeFont = reinterpret_cast<CloseFont>(dlsym(library, "AFont_close"));
        getPath = reinterpret_cast<GetPath>(
            dlsym(library, "AFont_getFontFilePath"));
        getWeight = reinterpret_cast<GetWeight>(
            dlsym(library, "AFont_getWeight"));
        isItalic = reinterpret_cast<IsItalic>(
            dlsym(library, "AFont_isItalic"));
        getCollectionIndex = reinterpret_cast<GetCollectionIndex>(
            dlsym(library, "AFont_getCollectionIndex"));
    }

    ~AndroidFontApi()
    {
        if (library) {
            dlclose(library);
        }
    }

    bool available() const
    {
        return createMatcher && destroyMatcher && setLocales && match
            && closeFont && getPath && getWeight && isItalic
            && getCollectionIndex;
    }
};

struct AndroidFontMatch {
    std::string path;
    int faceIndex = 0;
    int weight = 400;
    wsc::FontSlant slant = wsc::FontSlant::NORMAL;
};

bool findAndroidFont(const AndroidFontApi& api, std::uint16_t character,
                     const char* locales, AndroidFontMatch& result)
{
    if (!api.available()) {
        return false;
    }
    AFontMatcher* matcher = api.createMatcher();
    if (!matcher) {
        return false;
    }
    if (locales && locales[0] != '\0') {
        api.setLocales(matcher, locales);
    }
    std::uint32_t runLength = 0;
    AFont* font = api.match(matcher, "sans-serif", &character, 1, &runLength);
    if (font) {
        const char* path = api.getPath(font);
        if (path && path[0] != '\0' && runLength == 1) {
            result.path = path;
            result.faceIndex = static_cast<int>(api.getCollectionIndex(font));
            result.weight = static_cast<int>(api.getWeight(font));
            result.slant = api.isItalic(font)
                ? wsc::FontSlant::ITALIC : wsc::FontSlant::NORMAL;
        }
        api.closeFont(font);
    }
    api.destroyMatcher(matcher);
    return !result.path.empty();
}

void registerAndroidFonts(wsc::Canvas& canvas)
{
    AndroidFontApi fontApi;
    AndroidFontMatch sansMatch;
    AndroidFontMatch cjkMatch;
    bool sans = findAndroidFont(fontApi, u'A', nullptr, sansMatch)
        && registerFontFile(canvas, kSansFamily, sansMatch.path.c_str(),
                            sansMatch.faceIndex, sansMatch.weight,
                            sansMatch.slant);
    bool cjk = findAndroidFont(fontApi, 0x4E2D, "zh-CN", cjkMatch)
        && registerFontFile(canvas, kCjkFamily, cjkMatch.path.c_str(),
                            cjkMatch.faceIndex, cjkMatch.weight,
                            cjkMatch.slant);

    // AFontMatcher is API 29+. Keep compatibility with the sample's minSdk 21.
    if (!sans) {
        sans = registerFontFile(
            canvas, kSansFamily, "/system/fonts/Roboto-Regular.ttf");
    }
    if (!cjk) {
        cjk = registerFontFile(
            canvas, kCjkFamily, "/system/fonts/NotoSansCJK-Regular.ttc");
    }
    if (!cjk) {
        cjk = registerFontFile(
            canvas, kCjkFamily, "/product/fonts/NotoSansCJK-Regular.ttc");
    }

    wsc::FontFallbackChain fallback(kSansFamily);
    if (cjk) {
        fallback.addFallbackFamily(kCjkFamily);
    }
    canvas.setFontFallbackChain(fallback);

    __android_log_print((sans && cjk) ? ANDROID_LOG_INFO : ANDROID_LOG_WARN,
                        kLogTag, "Android fonts ready: latin=%s cjk=%s",
                        sans ? "yes" : "no", cjk ? "yes" : "no");
}

void createCheckerImage(wsc::Canvas& canvas,
                        std::unique_ptr<wsc::Image>& checkerImage)
{
    constexpr int imageSize = 24;
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(imageSize * imageSize * 4));
    for (int y = 0; y < imageSize; ++y) {
        for (int x = 0; x < imageSize; ++x) {
            const bool alternate = ((x / 6) + (y / 6)) % 2 != 0;
            const std::size_t offset = static_cast<std::size_t>(
                (y * imageSize + x) * 4);
            pixels[offset + 0] = alternate ? 78 : 32;
            pixels[offset + 1] = alternate ? 218 : 98;
            pixels[offset + 2] = alternate ? 206 : 230;
            pixels[offset + 3] = 255;
        }
    }

    checkerImage = std::make_unique<wsc::Image>();
    if (!canvas.loadImageFromRGBA(
            *checkerImage, pixels, imageSize, imageSize, true)) {
        logError("Checker image upload failed");
        checkerImage.reset();
    } else {
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "RGBA image upload ready: %dx%d", imageSize, imageSize);
    }
}

wsc::Paint makeTextPaint(float size, const wsc::Color& color,
                         const char* family = kSansFamily)
{
    wsc::Paint paint;
    paint.setStyle(wsc::Paint::Style::FILL);
    paint.setFillColor(color);
    paint.setTextSize(size);
    paint.setFontFamily(family);
    paint.setTextBaseline(wsc::Paint::TextBaseline::TOP);
    paint.setAntiAlias(true);
    return paint;
}

wsc::Path makeStar(float centerX, float centerY, float outerRadius,
                   float innerRadius, int points = 5)
{
    wsc::Path path;
    for (int i = 0; i < points * 2; ++i) {
        const float radius = (i % 2 == 0) ? outerRadius : innerRadius;
        const float angle = -kPi * 0.5f
            + static_cast<float>(i) * kPi / static_cast<float>(points);
        const float x = centerX + std::cos(angle) * radius;
        const float y = centerY + std::sin(angle) * radius;
        if (i == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    path.close();
    return path;
}

void drawCard(wsc::Canvas& canvas, const wsc::RectF& rect,
              const std::string& label)
{
    wsc::Paint card;
    card.setStyle(wsc::Paint::Style::FILL);
    card.setFillColor(wsc::Color(19, 28, 53, 238));
    canvas.drawRoundRect(rect, 13.0f, card);

    wsc::Paint border;
    border.setStyle(wsc::Paint::Style::STROKE);
    border.setStrokeWidth(1.0f);
    border.setStrokeColor(wsc::Color(91, 115, 173, 105));
    border.setAntiAlias(true);
    canvas.drawRoundRect(rect, 13.0f, border);

    auto labelPaint = makeTextPaint(10.0f, wsc::Color(139, 166, 226));
    labelPaint.setLetterSpacing(0.7f);
    canvas.drawText(label, rect.getX() + 11.0f, rect.getY() + 9.0f, labelPaint);
}

void drawScene(wsc::Canvas& canvas, const wsc::Image* checkerImage,
               float logicalWidth, float logicalHeight,
               float elapsedSeconds)
{
    using wsc::Color;
    using wsc::Paint;
    using wsc::Path;
    using wsc::PointF;
    using wsc::RectF;

    const float width = logicalWidth;
    const float height = logicalHeight;
    const float shortSide = std::max(1.0f, std::min(width, height));
    const float pulse = 0.5f + 0.5f * std::sin(elapsedSeconds * 2.2f);

    Paint background;
    background.setStyle(Paint::Style::FILL);
    background.setLinearGradient(0.0f, 0.0f, width, height, {
        Paint::ColorStop(0.0f, Color(8, 12, 29)),
        Paint::ColorStop(0.52f, Color(22, 31, 62)),
        Paint::ColorStop(1.0f, Color(19, 12, 43))
    });
    canvas.drawRect(RectF(0.0f, 0.0f, width, height), background);

    Paint ambientGlow;
    ambientGlow.setStyle(Paint::Style::FILL);
    ambientGlow.setFillColor(Color(58, 212, 224, 22));
    canvas.drawCircle(PointF(width * 0.12f, height * 0.2f), shortSide * 0.38f, ambientGlow);
    ambientGlow.setFillColor(Color(150, 86, 232, 20));
    canvas.drawCircle(PointF(width * 0.92f, height * 0.78f), shortSide * 0.42f, ambientGlow);

    auto title = makeTextPaint(23.0f, Color(242, 247, 255));
    title.setFontWeight(400);
    canvas.drawText("WhatsCanvas Android", 15.0f, 13.0f, title);
    auto subtitle = makeTextPaint(10.5f, Color(130, 157, 211));
    canvas.drawText("GLES 3  |  API 33  |  live feature matrix", 16.0f, 44.0f, subtitle);

    Paint livePill;
    livePill.setStyle(Paint::Style::FILL);
    livePill.setFillColor(Color(43, 204, 164, 42));
    const RectF liveRect(width - 58.0f, 17.0f, 43.0f, 21.0f);
    canvas.drawRoundRect(liveRect, 10.5f, livePill);
    auto liveText = makeTextPaint(9.0f, Color(83, 235, 196));
    liveText.setTextAlign(Paint::TextAlign::CENTER);
    canvas.drawText("LIVE", liveRect.getX() + liveRect.getWidth() * 0.5f,
                    liveRect.getY() + 5.0f, liveText);

    const float margin = 14.0f;
    const float gapX = 8.0f;
    const float gapY = 8.0f;
    const float gridTop = 69.0f;
    const float gridBottom = height - 35.0f;
    const bool landscape = width > height;
    const int columns = landscape ? 4 : 2;
    const int rows = landscape ? 2 : 4;
    const float cardWidth =
        (width - margin * 2.0f - gapX * static_cast<float>(columns - 1))
        / static_cast<float>(columns);
    const float cardHeight =
        (gridBottom - gridTop - gapY * static_cast<float>(rows - 1))
        / static_cast<float>(rows);
    const bool compactCards = cardHeight < 145.0f;
    auto cardRect = [&](int index) {
        const int column = index % columns;
        const int row = index / columns;
        return RectF(margin + static_cast<float>(column) * (cardWidth + gapX),
                     gridTop + static_cast<float>(row) * (cardHeight + gapY),
                     cardWidth, cardHeight);
    };

    // 1. Portable glyph atlas, font fallback, gradients and text layout.
    const RectF textCard = cardRect(0);
    drawCard(canvas, textCard, "TEXT + UTF-8");
    auto heroText = makeTextPaint(compactCards ? 23.0f : 27.0f, Color::WHITE);
    heroText.setLinearGradient(textCard.getX() + 11.0f, 0.0f,
                               textCard.getX() + cardWidth - 12.0f, 0.0f, {
        Paint::ColorStop(0.0f, Color(75, 225, 207)),
        Paint::ColorStop(1.0f, Color(108, 126, 248))
    });
    canvas.drawText("Aa 123", textCard.getX() + 11.0f,
                    textCard.getY() + 32.0f, heroText);
    auto cjkText = makeTextPaint(compactCards ? 13.5f : 16.0f,
                                 Color(239, 244, 255), kCjkFamily);
    canvas.drawText("\xE4\xB8\xAD\xE6\x96\x87\xE6\xB8\xB2\xE6\x9F\x93  Android",
                    textCard.getX() + 11.0f, textCard.getY() + 69.0f, cjkText);
    auto wrapText = makeTextPaint(compactCards ? 9.0f : 10.5f,
                                  Color(155, 178, 218));
    const float wrapTop = compactCards ? 91.0f : 98.0f;
    canvas.drawTextBox("Wrap, measure and ellipsis are rendered by the portable glyph atlas.",
                       RectF(textCard.getX() + 11.0f,
                             textCard.getY() + wrapTop,
                             cardWidth - 22.0f,
                             std::max(12.0f, cardHeight - wrapTop - 7.0f)),
                       compactCards ? 11.0f : 14.0f,
                       compactCards ? 1 : 3, true, wrapText);

    // 2. Concave path fill, cubic curve, stroke join and dash effect.
    const RectF pathCard = cardRect(1);
    drawCard(canvas, pathCard, "PATH + STROKE");
    const float pathCenterY = pathCard.getY()
        + (compactCards ? 77.0f : 91.0f);
    const float starRadius = compactCards ? 29.0f : 38.0f;
    Path star = makeStar(pathCard.getX() + 48.0f, pathCenterY,
                         starRadius, starRadius * 0.42f);
    Paint starFill;
    starFill.setStyle(Paint::Style::FILL);
    starFill.setLinearGradient(pathCard.getX() + 15.0f, pathCard.getY() + 55.0f,
                               pathCard.getX() + 82.0f, pathCard.getY() + 127.0f,
                               Color(255, 192, 80), Color(240, 82, 158));
    starFill.setAntiAlias(true);
    canvas.drawPath(star, starFill);
    Paint starStroke;
    starStroke.setStyle(Paint::Style::STROKE);
    starStroke.setStrokeColor(Color(255, 238, 192));
    starStroke.setStrokeWidth(2.0f);
    starStroke.setStrokeJoin(Paint::StrokeJoin::ROUND);
    canvas.drawPath(star, starStroke);
    Path curve;
    curve.moveTo(pathCard.getX() + 91.0f,
                 pathCard.getY() + (compactCards ? cardHeight - 16.0f : 116.0f));
    curve.cubicTo(pathCard.getX() + 111.0f,
                  pathCard.getY() + (compactCards ? 42.0f : 45.0f),
                  pathCard.getX() + 139.0f,
                  pathCard.getY() + (compactCards ? cardHeight - 9.0f : 145.0f),
                  pathCard.getX() + cardWidth - 13.0f,
                  pathCard.getY() + (compactCards ? 55.0f : 65.0f));
    Paint curveStroke;
    curveStroke.setStyle(Paint::Style::STROKE);
    curveStroke.setStrokeColor(Color(92, 213, 242));
    curveStroke.setStrokeWidth(4.0f);
    curveStroke.setStrokeCap(Paint::StrokeCap::ROUND);
    curveStroke.setDashPathEffect({9.0f, 6.0f}, elapsedSeconds * 12.0f);
    canvas.drawPath(curve, curveStroke);

    // 3. Non-rectangular anti-aliased clip over a multi-stop gradient.
    const RectF clipCard = cardRect(2);
    drawCard(canvas, clipCard, "CLIP PATH + GRADIENT");
    const PointF clipCenter(clipCard.getX() + cardWidth * 0.5f,
                            clipCard.getY() + cardHeight * 0.62f);
    const float clipRadius = compactCards ? 36.0f : 58.0f;
    Path clipStar = makeStar(clipCenter.getX(), clipCenter.getY(),
                             clipRadius, clipRadius * 0.5f, 7);
    canvas.save();
    canvas.clipPath(clipStar);
    Paint clippedGradient;
    clippedGradient.setStyle(Paint::Style::FILL);
    clippedGradient.setLinearGradient(clipCenter.getX() - 60.0f, clipCenter.getY() - 55.0f,
                                      clipCenter.getX() + 60.0f, clipCenter.getY() + 55.0f, {
        Paint::ColorStop(0.0f, Color(60, 225, 194)),
        Paint::ColorStop(0.48f, Color(63, 148, 237)),
        Paint::ColorStop(1.0f, Color(174, 87, 235))
    });
    canvas.drawRect(RectF(clipCenter.getX() - clipRadius - 7.0f,
                          clipCenter.getY() - clipRadius - 2.0f,
                          (clipRadius + 7.0f) * 2.0f,
                          (clipRadius + 2.0f) * 2.0f), clippedGradient);
    Paint stripe;
    stripe.setStyle(Paint::Style::STROKE);
    stripe.setStrokeColor(Color(255, 255, 255, 80));
    stripe.setStrokeWidth(5.0f);
    for (int i = -3; i <= 4; ++i) {
        const float offset = static_cast<float>(i) * 22.0f;
        canvas.drawLine(clipCenter.getX() - clipRadius - 22.0f + offset,
                        clipCenter.getY() + clipRadius + 7.0f,
                        clipCenter.getX() + offset,
                        clipCenter.getY() - clipRadius - 7.0f, stripe);
    }
    canvas.restore();

    // 4. Arc modes, rounded caps and independent stroke widths.
    const RectF arcCard = cardRect(3);
    drawCard(canvas, arcCard, "ARCS + ROUND CAPS");
    const float arcSize = std::min(104.0f, cardHeight - 52.0f);
    const RectF arcBounds(arcCard.getX() + (cardWidth - arcSize) * 0.5f,
                          arcCard.getY() + 41.0f, arcSize, arcSize);
    Paint arcTrack;
    arcTrack.setStyle(Paint::Style::STROKE);
    arcTrack.setStrokeColor(Color(49, 64, 96));
    arcTrack.setStrokeWidth(12.0f);
    canvas.drawArc(arcBounds, -kPi * 0.85f, kPi * 1.7f,
                   wsc::Canvas::ArcMode::OPEN, arcTrack);
    Paint arcValue;
    arcValue.setStyle(Paint::Style::STROKE);
    arcValue.setStrokeColor(Color(67, 220, 198));
    arcValue.setStrokeWidth(12.0f);
    arcValue.setStrokeCap(Paint::StrokeCap::ROUND);
    canvas.drawArc(arcBounds, -kPi * 0.85f,
                   kPi * (0.35f + pulse * 1.25f),
                   wsc::Canvas::ArcMode::OPEN, arcValue);
    Paint arcInner = arcValue;
    arcInner.setStrokeColor(Color(113, 130, 247));
    arcInner.setStrokeWidth(4.0f);
    canvas.drawArc(RectF(arcBounds.getX() + 17.0f, arcBounds.getY() + 17.0f,
                         arcBounds.getWidth() - 34.0f, arcBounds.getHeight() - 34.0f),
                   0.25f, kPi * 1.35f, wsc::Canvas::ArcMode::OPEN, arcInner);

    // 5. Save/restore with animated translate, rotate and scale transforms.
    const RectF transformCard = cardRect(4);
    drawCard(canvas, transformCard, "SAVE + TRANSFORM");
    const PointF transformCenter(transformCard.getX() + cardWidth * 0.5f,
                                 transformCard.getY() + cardHeight * 0.60f);
    canvas.save();
    canvas.translate(transformCenter.getX(), transformCenter.getY());
    canvas.rotate(elapsedSeconds * 0.45f);
    canvas.scale(0.92f + pulse * 0.14f, 0.92f + pulse * 0.14f);
    Paint rotated;
    rotated.setStyle(Paint::Style::FILL);
    rotated.setLinearGradient(-42.0f, -42.0f, 42.0f, 42.0f,
                              Color(97, 121, 246), Color(69, 221, 203));
    const float transformHalfSize = compactCards ? 29.0f : 40.0f;
    canvas.drawRoundRect(RectF(-transformHalfSize, -transformHalfSize,
                               transformHalfSize * 2.0f,
                               transformHalfSize * 2.0f),
                         compactCards ? 11.0f : 15.0f, rotated);
    Paint axis;
    axis.setStyle(Paint::Style::STROKE);
    axis.setStrokeWidth(2.0f);
    axis.setStrokeColor(Color(255, 255, 255, 170));
    const float axisRadius = compactCards ? 38.0f : 54.0f;
    canvas.drawLine(-axisRadius, 0.0f, axisRadius, 0.0f, axis);
    canvas.drawLine(0.0f, -axisRadius, 0.0f, axisRadius, axis);
    canvas.restore();

    // 6. Gaussian shadow plus SCREEN and MULTIPLY compositing.
    const RectF blendCard = cardRect(5);
    drawCard(canvas, blendCard, "SHADOW + BLEND");
    Paint shadowed;
    shadowed.setStyle(Paint::Style::FILL);
    shadowed.setFillColor(Color(255, 184, 76));
    shadowed.setShadowLayer(10.0f, 0.0f, 6.0f, Color(0, 0, 0, 180));
    const float blendSize = compactCards ? 44.0f : 64.0f;
    canvas.drawRoundRect(RectF(blendCard.getX() + 18.0f,
                              blendCard.getY() + (compactCards ? 47.0f : 57.0f),
                              blendSize, blendSize),
                         compactCards ? 13.0f : 18.0f, shadowed);
    Paint blendA;
    blendA.setStyle(Paint::Style::FILL);
    blendA.setFillColor(Color(71, 211, 229, 205));
    canvas.drawCircle(PointF(blendCard.getX() + 112.0f,
                             blendCard.getY() + (compactCards ? 61.0f : 78.0f)),
                      blendSize * 0.5f, blendA);
    Paint blendB;
    blendB.setStyle(Paint::Style::FILL);
    blendB.setFillColor(Color(178, 87, 238, 205));
    blendB.setBlendMode(Paint::BlendMode::SCREEN);
    canvas.drawCircle(PointF(blendCard.getX() + 136.0f,
                             blendCard.getY() + (compactCards ? 79.0f : 101.0f)),
                      blendSize * 0.5f, blendB);

    // 7. Raw RGBA upload, GPU texture sampling and rounded image clipping.
    const RectF imageCard = cardRect(6);
    drawCard(canvas, imageCard, "IMAGE + SAMPLING");
    if (checkerImage) {
        Paint imagePaint;
        imagePaint.setColor(Color::WHITE);
        imagePaint.setImageSampling(Paint::ImageSampling::NEAREST);
        canvas.drawImageTiled(*checkerImage,
                              RectF(imageCard.getX() + 13.0f, imageCard.getY() + 40.0f,
                                    cardWidth - 26.0f, cardHeight - 54.0f),
                              31.0f, 31.0f, imagePaint);
        canvas.drawImageRounded(*checkerImage,
                                RectF(imageCard.getX() + cardWidth - 71.0f,
                                      imageCard.getY() + cardHeight - 74.0f,
                                      56.0f, 56.0f),
                                18.0f, imagePaint);
    }

    // 8. Line AA, animated dash phase, point/line caps and progress geometry.
    const RectF motionCard = cardRect(7);
    drawCard(canvas, motionCard, "AA + DASH + MOTION");
    Path motionPath;
    motionPath.moveTo(motionCard.getX() + 14.0f, motionCard.getY() + 58.0f);
    motionPath.cubicTo(motionCard.getX() + 55.0f, motionCard.getY() + 28.0f,
                       motionCard.getX() + 96.0f, motionCard.getY() + 100.0f,
                       motionCard.getX() + cardWidth - 14.0f, motionCard.getY() + 61.0f);
    Paint dash;
    dash.setStyle(Paint::Style::STROKE);
    dash.setStrokeColor(Color(246, 204, 91));
    dash.setStrokeWidth(4.0f);
    dash.setStrokeCap(Paint::StrokeCap::ROUND);
    dash.setDashPathEffect({10.0f, 7.0f}, -elapsedSeconds * 18.0f);
    dash.setAntiAlias(true);
    canvas.drawPath(motionPath, dash);
    const float trackX = motionCard.getX() + 14.0f;
    const float trackY = motionCard.getY() + cardHeight - 39.0f;
    const float trackWidth = cardWidth - 28.0f;
    Paint track;
    track.setStyle(Paint::Style::FILL);
    track.setFillColor(Color(7, 12, 28, 230));
    canvas.drawRoundRect(RectF(trackX, trackY, trackWidth, 12.0f), 6.0f, track);
    Paint progress;
    progress.setStyle(Paint::Style::FILL);
    progress.setLinearGradient(trackX, trackY, trackX + trackWidth, trackY,
                               Color(71, 222, 204), Color(105, 123, 247));
    canvas.drawRoundRect(RectF(trackX, trackY,
                              trackWidth * (0.18f + pulse * 0.78f), 12.0f),
                         6.0f, progress);

    auto footer = makeTextPaint(9.5f, Color(108, 132, 183));
    footer.setTextAlign(Paint::TextAlign::CENTER);
    canvas.drawText("8 feature cards  |  real OpenGL ES output",
                    width * 0.5f, height - 23.0f, footer);
}

class NativeRenderer {
public:
    bool surfaceCreated()
    {
        release();
        const bool loaded = wsc::Canvas::loadOpenGL(&loadOpenGlesProcedure);
        if (!loaded) {
            logError("Canvas::loadOpenGL failed for the active OpenGL ES context");
        }
        return loaded;
    }

    bool resize(int width, int height, float density)
    {
        if (width <= 0 || height <= 0) {
            return false;
        }

        release();
        glViewport(0, 0, width, height);
        const float safeDensity = std::isfinite(density) && density > 0.0f
            ? density : 1.0f;
        logicalWidth_ = static_cast<float>(width) / safeDensity;
        logicalHeight_ = static_cast<float>(height) / safeDensity;

        canvas_ = wsc::Canvas::create(
            wsc::Canvas::Backend::OpenGLES, width, height);
        if (!canvas_) {
            logError("Canvas::create(OpenGLES) returned null");
            return false;
        }

        canvas_->setDevicePixelRatio(safeDensity);
        if (!canvas_->initializeContext()) {
            logError("Canvas::initializeContext failed");
            canvas_.reset();
            return false;
        }

        registerAndroidFonts(*canvas_);
        createCheckerImage(*canvas_, checkerImage_);

        auto probePaint = makeTextPaint(18.0f, wsc::Color::WHITE);
        const float probeWidth = canvas_->measureText(
            "WhatsCanvas Aa 123", probePaint);
        __android_log_print(
            ANDROID_LOG_INFO, kLogTag,
            "OpenGLES renderer ready: %dx%d, dpr=%.2f, GL=%s, textWidth=%.1f",
            width, height, density,
            wsc::Canvas::getOpenGLVersionString().c_str(), probeWidth);
        return true;
    }

    void render(float elapsedSeconds)
    {
        if (!canvas_) {
            return;
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.03f, 0.04f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        canvas_->beginFrame();
        drawScene(*canvas_, checkerImage_.get(), logicalWidth_, logicalHeight_,
                  elapsedSeconds);
        canvas_->endFrame();

        const int elapsedSecond = static_cast<int>(elapsedSeconds);
        if (elapsedSecond != lastStatsSecond_ && elapsedSecond % 5 == 0) {
            lastStatsSecond_ = elapsedSecond;
            const auto stats = canvas_->getRenderStats();
            __android_log_print(
                ANDROID_LOG_INFO, kLogTag,
                "Frame stats: commands=%zu draws=%zu paths=%zu glyphTextures=%zu images=%zu filters=%zu",
                stats.commandCount, stats.drawCallCount,
                stats.pathVertexCount, stats.glyphAtlasTextureCount,
                stats.imageTextureCount, stats.filterCount);
        }
    }

    void release()
    {
        checkerImage_.reset();
        if (canvas_) {
            canvas_->finalizeContext();
            canvas_.reset();
        }
        lastStatsSecond_ = -1;
    }

private:
    std::unique_ptr<wsc::Canvas> canvas_;
    std::unique_ptr<wsc::Image> checkerImage_;
    float logicalWidth_ = 1.0f;
    float logicalHeight_ = 1.0f;
    int lastStatsSecond_ = -1;
};

NativeRenderer* rendererFromHandle(jlong handle)
{
    return reinterpret_cast<NativeRenderer*>(
        static_cast<std::uintptr_t>(handle));
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_whatscanvas_demo_WhatsCanvasRenderer_nativeCreate(
    JNIEnv*, jobject)
{
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(
        new NativeRenderer()));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_whatscanvas_demo_WhatsCanvasRenderer_nativeSurfaceCreated(
    JNIEnv*, jobject, jlong handle)
{
    NativeRenderer* renderer = rendererFromHandle(handle);
    return renderer && renderer->surfaceCreated() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_whatscanvas_demo_WhatsCanvasRenderer_nativeResize(
    JNIEnv*, jobject, jlong handle, jint width, jint height, jfloat density)
{
    NativeRenderer* renderer = rendererFromHandle(handle);
    return renderer && renderer->resize(width, height, density)
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_whatscanvas_demo_WhatsCanvasRenderer_nativeRender(
    JNIEnv*, jobject, jlong handle, jfloat elapsedSeconds)
{
    NativeRenderer* renderer = rendererFromHandle(handle);
    if (renderer) {
        renderer->render(elapsedSeconds);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_whatscanvas_demo_WhatsCanvasRenderer_nativeDestroy(
    JNIEnv*, jobject, jlong handle)
{
    NativeRenderer* renderer = rendererFromHandle(handle);
    if (renderer) {
        renderer->release();
        delete renderer;
    }
}
