#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "wsc/wsc.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace wsc;

namespace {

constexpr int kWidth = 1920;
constexpr int kHeight = 1080;

constexpr float kSidebarWidth = 224.0f;
constexpr float kContentLeft = 278.0f;
constexpr float kHeroTop = 126.0f;
constexpr float kHeroWidth = 1048.0f;
constexpr float kHeroHeight = 764.0f;
constexpr float kRailLeft = 1370.0f;
constexpr float kRailWidth = 482.0f;

Paint solid(const Color &color)
{
    Paint paint;
    paint.setStyle(Paint::Style::FILL);
    paint.setColor(color);
    paint.setAntiAlias(true);
    return paint;
}

Paint textStyle(float size, const Color &color, int weight = 400)
{
    Paint paint = solid(color);
    paint.setTextSize(size);
    paint.setFontFamily(FontSystem::kDefaultPrimaryFamily);
    paint.setFontWeight(weight);
    paint.setTextBaseline(Paint::TextBaseline::TOP);
    return paint;
}

bool registerShowcaseFonts(Canvas &canvas)
{
#ifdef WSC_SHOWCASE_CJK_FONT_PATH
    constexpr const char *kPortableCjkFamily = "Showcase Portable CJK";
    if (!canvas.registerFontFace(FontFace::fromFile(
            FontDescriptor(kPortableCjkFamily),
            WSC_SHOWCASE_CJK_FONT_PATH))) {
        return false;
    }
    FontFallbackChain chain(FontSystem::kDefaultPrimaryFamily);
    chain.addFallbackFamily(kPortableCjkFamily);
    return canvas.setFontFallbackChain(chain);
#else
    return true;
#endif
}

void strokeRoundRect(Canvas &canvas, const RectF &bounds, float radius,
                     const Color &color, float width)
{
    Path path;
    path.addRoundRect(bounds, radius);
    Paint border;
    border.setStyle(Paint::Style::STROKE);
    border.setStrokeColor(color);
    border.setStrokeWidth(width);
    border.setAntiAlias(true);
    canvas.drawPath(path, border);
}

void drawPill(Canvas &canvas, const RectF &bounds, const Color &fill,
              const std::string &label, const Color &textColor)
{
    canvas.drawRoundRect(bounds, bounds.getHeight() * 0.5f, solid(fill));
    Paint labelPaint = textStyle(15.0f, textColor, 600);
    labelPaint.setTextBaseline(Paint::TextBaseline::MIDDLE);
    canvas.drawText(label, bounds.getX() + 18.0f,
                    bounds.getY() + bounds.getHeight() * 0.5f, labelPaint);
}

void drawBackground(Canvas &canvas)
{
    Paint base;
    base.setLinearGradient(
        0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight),
        {
            Paint::ColorStop(0.0f, Color(5, 13, 29, 255)),
            Paint::ColorStop(0.48f, Color(12, 31, 66, 255)),
            Paint::ColorStop(1.0f, Color(6, 20, 45, 255)),
        });
    canvas.drawRect(RectF(0.0f, 0.0f, static_cast<float>(kWidth),
                          static_cast<float>(kHeight)), base);

    Paint cyanGlow;
    cyanGlow.setRadialGradient(
        1110.0f, 610.0f, 520.0f,
        {
            Paint::ColorStop(0.0f, Color(34, 177, 222, 115)),
            Paint::ColorStop(0.45f, Color(20, 97, 181, 66)),
            Paint::ColorStop(1.0f, Color(7, 20, 45, 0)),
        });
    canvas.drawCircle(1110.0f, 610.0f, 520.0f, cyanGlow);

    Paint violetGlow;
    violetGlow.setRadialGradient(
        1280.0f, 70.0f, 430.0f,
        {
            Paint::ColorStop(0.0f, Color(130, 102, 255, 125)),
            Paint::ColorStop(0.55f, Color(74, 61, 171, 64)),
            Paint::ColorStop(1.0f, Color(8, 20, 47, 0)),
        });
    canvas.drawCircle(1280.0f, 70.0f, 430.0f, violetGlow);

    Paint coralOrb;
    coralOrb.setRadialGradient(
        1280.0f, 74.0f, 190.0f,
        {
            Paint::ColorStop(0.0f, Color(255, 189, 158, 235)),
            Paint::ColorStop(0.48f, Color(255, 123, 145, 220)),
            Paint::ColorStop(1.0f, Color(199, 83, 157, 195)),
        });
    canvas.drawCircle(1320.0f, 118.0f, 130.0f, coralOrb);
    canvas.drawCircle(165.0f, 935.0f, 185.0f,
                      solid(Color(83, 103, 224, 76)));

    Paint ribbon;
    ribbon.setStyle(Paint::Style::STROKE);
    ribbon.setStrokeWidth(78.0f);
    ribbon.setStrokeCap(Paint::StrokeCap::ROUND);
    ribbon.setStrokeColor(Color(38, 103, 170, 86));
    Path ribbonPath;
    ribbonPath.moveTo(540.0f, 920.0f);
    ribbonPath.cubicTo(850.0f, 640.0f, 1100.0f, 930.0f, 1450.0f, 610.0f);
    ribbonPath.cubicTo(1580.0f, 490.0f, 1710.0f, 510.0f, 1870.0f, 380.0f);
    canvas.drawPath(ribbonPath, ribbon);

    Paint fineRibbon = ribbon;
    fineRibbon.setStrokeWidth(2.0f);
    fineRibbon.setStrokeColor(Color(170, 235, 255, 110));
    fineRibbon.clearShader();
    for (int i = 0; i < 7; ++i) {
        canvas.save();
        canvas.translate(0.0f, static_cast<float>(i) * 13.0f);
        canvas.drawPath(ribbonPath, fineRibbon);
        canvas.restore();
    }
}

void drawLogo(Canvas &canvas)
{
    const RectF mark(32.0f, 30.0f, 42.0f, 42.0f);
    Paint markFill;
    markFill.setLinearGradient(
        mark.getX(), mark.getY(), mark.getX() + mark.getWidth(),
        mark.getY() + mark.getHeight(),
        Color(69, 216, 232, 255), Color(75, 132, 247, 255));
    canvas.drawRoundRect(mark, 11.0f, markFill);
    canvas.drawRoundRect(RectF(41.0f, 39.0f, 24.0f, 24.0f), 6.0f,
                         solid(Color(7, 20, 43, 255)));
    canvas.drawRoundRect(RectF(55.0f, 30.0f, 9.0f, 18.0f), 4.5f,
                         solid(Color(7, 20, 43, 255)));

    canvas.drawText("WhatsCanvas", 88.0f, 36.0f,
                    textStyle(25.0f, Color(244, 249, 255, 255), 700));
}

void drawNavIcon(Canvas &canvas, float x, float y, int kind,
                 const Color &color)
{
    Paint icon;
    icon.setStyle(Paint::Style::STROKE);
    icon.setStrokeColor(color);
    icon.setStrokeWidth(2.4f);
    icon.setStrokeCap(Paint::StrokeCap::ROUND);
    icon.setStrokeJoin(Paint::StrokeJoin::ROUND);

    if (kind == 0) {
        Path home;
        home.moveTo(x - 11.0f, y + 1.0f);
        home.lineTo(x, y - 10.0f);
        home.lineTo(x + 11.0f, y + 1.0f);
        home.lineTo(x + 8.0f, y + 1.0f);
        home.lineTo(x + 8.0f, y + 11.0f);
        home.lineTo(x - 8.0f, y + 11.0f);
        home.lineTo(x - 8.0f, y + 1.0f);
        canvas.drawPath(home, icon);
    } else if (kind == 1) {
        canvas.drawCircle(x, y, 11.0f, icon);
        for (int i = -2; i <= 2; ++i) {
            canvas.drawLine(x - 8.0f + i * 3.0f, y + 8.0f,
                            x + 8.0f + i * 3.0f, y - 8.0f, icon);
        }
    } else if (kind == 2) {
        Paint type = textStyle(27.0f, color, 700);
        type.setTextBaseline(Paint::TextBaseline::MIDDLE);
        canvas.drawText("T", x - 10.0f, y, type);
    } else if (kind == 3) {
        canvas.drawRoundRect(RectF(x - 12.0f, y - 10.0f, 24.0f, 20.0f),
                             3.0f, icon);
        canvas.drawCircle(x + 5.0f, y - 4.0f, 2.0f, solid(color));
        Path mountain;
        mountain.moveTo(x - 9.0f, y + 7.0f);
        mountain.lineTo(x - 2.0f, y);
        mountain.lineTo(x + 3.0f, y + 5.0f);
        mountain.lineTo(x + 7.0f, y + 1.0f);
        mountain.lineTo(x + 11.0f, y + 7.0f);
        canvas.drawPath(mountain, icon);
    } else {
        Path flask;
        flask.moveTo(x - 5.0f, y - 11.0f);
        flask.lineTo(x + 5.0f, y - 11.0f);
        flask.moveTo(x - 2.0f, y - 11.0f);
        flask.lineTo(x - 2.0f, y - 2.0f);
        flask.lineTo(x - 10.0f, y + 10.0f);
        flask.lineTo(x + 10.0f, y + 10.0f);
        flask.lineTo(x + 2.0f, y - 2.0f);
        flask.lineTo(x + 2.0f, y - 11.0f);
        canvas.drawPath(flask, icon);
    }
}

void drawSidebar(Canvas &canvas)
{
    canvas.drawRect(RectF(0.0f, 0.0f, kSidebarWidth, kHeight),
                    solid(Color(3, 12, 28, 222)));
    canvas.drawRect(RectF(kSidebarWidth - 1.0f, 0.0f, 1.0f, kHeight),
                    solid(Color(136, 193, 255, 35)));
    drawLogo(canvas);

    struct NavItem {
        const char *label;
        int icon;
    };
    const NavItem items[] = {
        {"Overview", 0},
        {"Glass", 1},
        {"Typography", 2},
        {"Images", 3},
        {"Rendering Lab", 4},
    };

    for (int i = 0; i < 5; ++i) {
        const float y = 132.0f + static_cast<float>(i) * 88.0f;
        const bool active = i == 0;
        if (active) {
            Paint selected;
            selected.setLinearGradient(
                16.0f, y, 208.0f, y + 60.0f,
                Color(22, 101, 155, 108), Color(25, 56, 105, 60));
            canvas.drawRoundRect(RectF(16.0f, y, 192.0f, 60.0f), 15.0f,
                                 selected);
            canvas.drawRoundRect(RectF(16.0f, y + 15.0f, 3.0f, 30.0f),
                                 1.5f, solid(Color(84, 224, 241, 235)));
        }
        const Color foreground =
            active ? Color(104, 226, 242, 255) : Color(187, 204, 229, 214);
        drawNavIcon(canvas, 48.0f, y + 30.0f, items[i].icon, foreground);
        Paint label = textStyle(18.0f, foreground, active ? 600 : 400);
        label.setTextBaseline(Paint::TextBaseline::MIDDLE);
        canvas.drawText(items[i].label, 83.0f, y + 30.0f, label);
    }

    canvas.drawText("STATIC 2D ENGINE", 35.0f, 938.0f,
                    textStyle(11.0f, Color(126, 157, 197, 180), 600));
    canvas.drawText("Canvas / Paint / Path", 35.0f, 962.0f,
                    textStyle(13.0f, Color(176, 197, 224, 185)));
}

void drawGlassSurface(Canvas &canvas, const RectF &bounds, float radius,
                      float blurSigma, const Color &topTint,
                      const Color &bottomTint)
{
    canvas.save();
    Path clip;
    clip.addRoundRect(bounds, radius);
    canvas.clipPath(clip);

    LayerOptions glass;
    glass.setBackdropFilter(
        ImageFilter::frostedGlass(blurSigma, 1.08f, 1.03f, 1.0f, 0.004f));
    canvas.saveLayer(bounds, solid(Color(255, 255, 255, 255)), glass);
    canvas.restore();

    Paint tint;
    tint.setLinearGradient(
        bounds.getX(), bounds.getY(),
        bounds.getX(), bounds.getY() + bounds.getHeight(),
        {
            Paint::ColorStop(0.0f, topTint),
            Paint::ColorStop(0.55f, Color(
                static_cast<int>((topTint.r() + bottomTint.r()) * 127.5f),
                static_cast<int>((topTint.g() + bottomTint.g()) * 127.5f),
                static_cast<int>((topTint.b() + bottomTint.b()) * 127.5f),
                30)),
            Paint::ColorStop(1.0f, bottomTint),
        });
    canvas.drawRect(bounds, tint);

    Paint shine;
    shine.setLinearGradient(
        bounds.getX(), bounds.getY(), bounds.getX(),
        bounds.getY() + std::min(180.0f, bounds.getHeight()),
        Color(255, 255, 255, 38), Color(255, 255, 255, 0));
    canvas.drawRect(RectF(bounds.getX(), bounds.getY(), bounds.getWidth(),
                          std::min(180.0f, bounds.getHeight())), shine);
    canvas.restore();

    strokeRoundRect(canvas, bounds, radius, Color(202, 226, 255, 105), 1.3f);
}

void drawTintedSurface(Canvas &canvas, const RectF &bounds, float radius,
                       const Color &topColor, const Color &bottomColor)
{
    canvas.drawRoundRect(RectF(bounds.getX() + 5.0f, bounds.getY() + 9.0f,
                               bounds.getWidth(), bounds.getHeight()),
                         radius, solid(Color(1, 8, 23, 62)));

    Paint surface;
    surface.setLinearGradient(bounds.getX(), bounds.getY(),
                              bounds.getX(),
                              bounds.getY() + bounds.getHeight(),
                              topColor, bottomColor);
    canvas.drawRoundRect(bounds, radius, surface);

    Paint topEdge;
    topEdge.setStyle(Paint::Style::STROKE);
    topEdge.setStrokeColor(Color(220, 239, 255, 52));
    topEdge.setStrokeWidth(1.0f);
    canvas.drawLine(bounds.getX() + radius, bounds.getY() + 1.0f,
                    bounds.getX() + bounds.getWidth() - radius,
                    bounds.getY() + 1.0f, topEdge);
    strokeRoundRect(canvas, bounds, radius, Color(151, 206, 255, 92), 1.2f);
}

void drawWeather(Canvas &canvas)
{
    const float x = kContentLeft + 46.0f;
    const float y = kHeroTop + 50.0f;

    Paint atmosphere;
    atmosphere.setRadialGradient(
        x + 220.0f, y + 300.0f, 230.0f,
        {
            Paint::ColorStop(0.0f, Color(79, 203, 231, 42)),
            Paint::ColorStop(0.55f, Color(62, 115, 220, 18)),
            Paint::ColorStop(1.0f, Color(41, 74, 145, 0)),
        });
    canvas.drawCircle(x + 220.0f, y + 300.0f, 230.0f, atmosphere);
    for (int i = 0; i < 3; ++i) {
        Paint orbit;
        orbit.setStyle(Paint::Style::STROKE);
        orbit.setStrokeColor(Color(151, 218, 245, 28 - i * 6));
        orbit.setStrokeWidth(1.0f);
        canvas.drawCircle(x + 220.0f, y + 300.0f,
                          150.0f + static_cast<float>(i) * 52.0f, orbit);
    }

    canvas.drawCircle(x + 10.0f, y + 11.0f, 6.0f,
                      solid(Color(93, 220, 239, 255)));
    canvas.drawText("SHANGHAI", x + 30.0f, y,
                    textStyle(18.0f, Color(226, 240, 255, 245), 600));
    canvas.drawText("CLEAR EVENING", x + 30.0f, y + 31.0f,
                    textStyle(12.0f, Color(167, 191, 222, 190), 600));

    canvas.drawText("24", x - 6.0f, y + 86.0f,
                    textStyle(174.0f, Color(245, 250, 255, 255), 300));
    canvas.drawText("\xC2\xB0", x + 165.0f, y + 128.0f,
                    textStyle(70.0f, Color(200, 229, 255, 255), 300));

    Paint description = textStyle(22.0f, Color(229, 239, 251, 235));
    description.setLetterSpacing(0.3f);
    canvas.drawText("Calm skies over the city", x, y + 294.0f, description);

    canvas.drawRoundRect(RectF(x, y + 345.0f, 420.0f, 1.0f), 0.5f,
                         solid(Color(208, 230, 255, 45)));

    const char *values[] = {"12 km/h", "56%", "18:47"};
    const char *labels[] = {"NW WIND", "HUMIDITY", "SUNSET"};
    const Color accents[] = {
        Color(92, 221, 239, 255),
        Color(139, 150, 255, 255),
        Color(255, 147, 126, 255),
    };
    for (int i = 0; i < 3; ++i) {
        const float itemX = x + static_cast<float>(i) * 142.0f;
        canvas.drawCircle(itemX + 7.0f, y + 404.0f, 6.0f, solid(accents[i]));
        canvas.drawText(values[i], itemX + 22.0f, y + 389.0f,
                        textStyle(17.0f, Color(238, 246, 255, 245), 600));
        canvas.drawText(labels[i], itemX, y + 421.0f,
                        textStyle(11.0f, Color(149, 178, 211, 190), 600));
    }

    drawPill(canvas, RectF(x, y + 505.0f, 166.0f, 40.0f),
             Color(57, 208, 228, 28), "GLASS ACTIVE",
             Color(100, 227, 241, 255));
}

void drawAlbumArtwork(Canvas &canvas, const RectF &bounds)
{
    canvas.save();
    Path clip;
    clip.addRoundRect(bounds, 24.0f);
    canvas.clipPath(clip);

    Paint sky;
    sky.setLinearGradient(
        bounds.getX(), bounds.getY(),
        bounds.getX() + bounds.getWidth(), bounds.getY() + bounds.getHeight(),
        {
            Paint::ColorStop(0.0f, Color(255, 139, 126, 255)),
            Paint::ColorStop(0.42f, Color(142, 111, 255, 255)),
            Paint::ColorStop(1.0f, Color(44, 177, 223, 255)),
        });
    canvas.drawRect(bounds, sky);

    canvas.drawCircle(bounds.getX() + bounds.getWidth() * 0.64f,
                      bounds.getY() + bounds.getHeight() * 0.34f,
                      bounds.getWidth() * 0.27f,
                      solid(Color(255, 183, 144, 210)));

    Paint waveA;
    waveA.setStyle(Paint::Style::FILL);
    waveA.setFillColor(Color(40, 48, 119, 245));
    Path wavePathA;
    wavePathA.moveTo(bounds.getX() - 20.0f,
                     bounds.getY() + bounds.getHeight() * 0.55f);
    wavePathA.cubicTo(
        bounds.getX() + bounds.getWidth() * 0.28f,
        bounds.getY() + bounds.getHeight() * 0.30f,
        bounds.getX() + bounds.getWidth() * 0.48f,
        bounds.getY() + bounds.getHeight() * 0.85f,
        bounds.getX() + bounds.getWidth() + 20.0f,
        bounds.getY() + bounds.getHeight() * 0.52f);
    wavePathA.lineTo(bounds.getX() + bounds.getWidth() + 20.0f,
                     bounds.getY() + bounds.getHeight() + 20.0f);
    wavePathA.lineTo(bounds.getX() - 20.0f,
                     bounds.getY() + bounds.getHeight() + 20.0f);
    wavePathA.close();
    canvas.drawPath(wavePathA, waveA);

    Paint waveB = solid(Color(13, 38, 90, 250));
    Path wavePathB;
    wavePathB.moveTo(bounds.getX() - 20.0f,
                     bounds.getY() + bounds.getHeight() * 0.70f);
    wavePathB.cubicTo(
        bounds.getX() + bounds.getWidth() * 0.34f,
        bounds.getY() + bounds.getHeight() * 0.48f,
        bounds.getX() + bounds.getWidth() * 0.66f,
        bounds.getY() + bounds.getHeight() * 0.96f,
        bounds.getX() + bounds.getWidth() + 20.0f,
        bounds.getY() + bounds.getHeight() * 0.64f);
    wavePathB.lineTo(bounds.getX() + bounds.getWidth() + 20.0f,
                     bounds.getY() + bounds.getHeight() + 20.0f);
    wavePathB.lineTo(bounds.getX() - 20.0f,
                     bounds.getY() + bounds.getHeight() + 20.0f);
    wavePathB.close();
    canvas.drawPath(wavePathB, waveB);

    Paint contour;
    contour.setStyle(Paint::Style::STROKE);
    contour.setStrokeColor(Color(180, 222, 255, 34));
    contour.setStrokeWidth(1.0f);
    for (int i = 0; i < 5; ++i) {
        const float offset = static_cast<float>(i) * 16.0f;
        Path contourPath;
        contourPath.moveTo(bounds.getX() - 12.0f,
                           bounds.getY() + bounds.getHeight() * 0.58f
                               + offset);
        contourPath.cubicTo(
            bounds.getX() + bounds.getWidth() * 0.28f,
            bounds.getY() + bounds.getHeight() * 0.40f + offset,
            bounds.getX() + bounds.getWidth() * 0.65f,
            bounds.getY() + bounds.getHeight() * 0.83f + offset,
            bounds.getX() + bounds.getWidth() + 12.0f,
            bounds.getY() + bounds.getHeight() * 0.57f + offset);
        canvas.drawPath(contourPath, contour);
    }

    canvas.restore();
    strokeRoundRect(canvas, bounds, 24.0f, Color(208, 231, 255, 100), 1.0f);
}

void drawInsetCircle(Canvas &canvas, float cx, float cy, float radius)
{
    const RectF bounds(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
    LayerOptions inset;
    inset.setImageFilter(ImageFilter::innerShadow(
        7.0f, 1.5f, 2.0f, Color(31, 72, 118, 105)));
    canvas.saveLayer(bounds, solid(Color(255, 255, 255, 255)), inset);
    Paint concave;
    concave.setRadialGradient(
        cx - radius * 0.24f, cy - radius * 0.28f, radius * 1.35f,
        {
            Paint::ColorStop(0.0f, Color(157, 221, 246, 70)),
            Paint::ColorStop(0.52f, Color(104, 164, 219, 60)),
            Paint::ColorStop(1.0f, Color(50, 92, 151, 92)),
        });
    canvas.drawCircle(cx, cy, radius, concave);
    canvas.restore();
    strokeRoundRect(canvas, bounds, radius, Color(203, 229, 255, 100), 1.0f);
    canvas.drawCircle(cx - radius * 0.25f, cy - radius * 0.27f,
                      std::max(2.0f, radius * 0.055f),
                      solid(Color(222, 247, 255, 155)));
}

void drawMusic(Canvas &canvas)
{
    const RectF artwork(kContentLeft + 600.0f, kHeroTop + 58.0f,
                        360.0f, 360.0f);
    drawAlbumArtwork(canvas, artwork);

    const float x = artwork.getX();
    const float y = artwork.getY() + artwork.getHeight() + 32.0f;
    canvas.drawText("Glowline", x, y,
                    textStyle(25.0f, Color(246, 250, 255, 255), 700));
    canvas.drawText("Midnight Echoes", x, y + 38.0f,
                    textStyle(17.0f, Color(174, 198, 229, 220)));

    const float trackY = y + 108.0f;
    const float trackWidth = artwork.getWidth();
    LayerOptions inset;
    inset.setImageFilter(ImageFilter::innerShadow(
        4.0f, 1.0f, 1.5f, Color(18, 55, 93, 95)));
    canvas.saveLayer(RectF(x - 5.0f, trackY - 5.0f, trackWidth + 10.0f, 18.0f),
                     solid(Color(255, 255, 255, 255)), inset);
    canvas.drawRoundRect(RectF(x, trackY, trackWidth, 8.0f), 4.0f,
                         solid(Color(183, 210, 238, 95)));
    canvas.restore();
    const float progressWidth = trackWidth * 0.44f;
    canvas.drawRoundRect(RectF(x + 1.0f, trackY + 1.0f,
                               progressWidth, 6.0f), 3.0f,
                         solid(Color(80, 219, 235, 245)));
    canvas.drawCircle(x + progressWidth, trackY + 4.0f, 7.0f,
                      solid(Color(227, 248, 255, 255)));

    canvas.drawText("01:24", x, trackY + 25.0f,
                    textStyle(13.0f, Color(190, 213, 238, 215)));
    Paint duration = textStyle(13.0f, Color(190, 213, 238, 215));
    duration.setTextAlign(Paint::TextAlign::RIGHT);
    canvas.drawText("04:38", x + trackWidth, trackY + 25.0f, duration);

    const float controlsY = trackY + 118.0f;
    const float previousX = x + 62.0f;
    const float playX = x + trackWidth * 0.5f;
    const float nextX = x + trackWidth - 62.0f;
    drawInsetCircle(canvas, previousX, controlsY, 39.0f);
    drawInsetCircle(canvas, playX, controlsY, 52.0f);
    drawInsetCircle(canvas, nextX, controlsY, 39.0f);

    Paint control;
    control.setStyle(Paint::Style::STROKE);
    control.setStrokeColor(Color(238, 248, 255, 245));
    control.setStrokeWidth(4.0f);
    control.setStrokeCap(Paint::StrokeCap::ROUND);
    canvas.drawLine(previousX - 13.0f, controlsY - 11.0f,
                    previousX - 13.0f, controlsY + 11.0f, control);
    Path previous;
    previous.moveTo(previousX - 10.0f, controlsY);
    previous.lineTo(previousX + 8.0f, controlsY - 12.0f);
    previous.lineTo(previousX + 8.0f, controlsY + 12.0f);
    previous.close();
    canvas.drawPath(previous, solid(Color(238, 248, 255, 245)));

    canvas.drawRoundRect(RectF(playX - 12.0f, controlsY - 16.0f,
                               8.0f, 32.0f),
                         4.0f, solid(Color(244, 250, 255, 250)));
    canvas.drawRoundRect(RectF(playX + 4.0f, controlsY - 16.0f,
                               8.0f, 32.0f),
                         4.0f, solid(Color(244, 250, 255, 250)));

    canvas.drawLine(nextX + 13.0f, controlsY - 11.0f,
                    nextX + 13.0f, controlsY + 11.0f, control);
    Path next;
    next.moveTo(nextX + 10.0f, controlsY);
    next.lineTo(nextX - 8.0f, controlsY - 12.0f);
    next.lineTo(nextX - 8.0f, controlsY + 12.0f);
    next.close();
    canvas.drawPath(next, solid(Color(238, 248, 255, 245)));
}

void drawHero(Canvas &canvas)
{
    const RectF hero(kContentLeft, kHeroTop, kHeroWidth, kHeroHeight);
    canvas.drawRoundRect(RectF(hero.getX() + 12.0f, hero.getY() + 22.0f,
                               hero.getWidth(), hero.getHeight()),
                         34.0f, solid(Color(1, 7, 20, 74)));
    drawGlassSurface(canvas, hero, 34.0f, 18.0f,
                     Color(202, 228, 255, 57),
                     Color(55, 124, 198, 47));

    canvas.drawRoundRect(RectF(hero.getX() + 496.0f, hero.getY() + 44.0f,
                               1.0f, hero.getHeight() - 88.0f),
                         0.5f, solid(Color(209, 231, 255, 40)));
    drawWeather(canvas);
    drawMusic(canvas);
}

void drawCapabilityRail(Canvas &canvas)
{
    const RectF glassCard(kRailLeft, kHeroTop, kRailWidth, 250.0f);
    drawTintedSurface(canvas, glassCard, 28.0f,
                      Color(34, 75, 126, 232),
                      Color(12, 36, 76, 242));
    canvas.drawText("01 / OPTICAL MATERIAL", glassCard.getX() + 28.0f,
                    glassCard.getY() + 27.0f,
                    textStyle(11.0f, Color(108, 216, 239, 230), 700));
    canvas.drawText("Frosted", glassCard.getX() + 28.0f,
                    glassCard.getY() + 63.0f,
                    textStyle(36.0f, Color(247, 251, 255, 255), 600));
    canvas.drawText("glass", glassCard.getX() + 28.0f,
                    glassCard.getY() + 104.0f,
                    textStyle(36.0f, Color(247, 251, 255, 255), 300));
    canvas.drawTextBox(
        "Backdrop blur / tint / saturation",
        RectF(glassCard.getX() + 28.0f, glassCard.getY() + 170.0f,
              205.0f, 42.0f),
        20.0f, 2, true,
        textStyle(14.0f, Color(174, 199, 229, 225)));

    const RectF glassSample(glassCard.getX() + 284.0f,
                            glassCard.getY() + 34.0f, 158.0f, 180.0f);
    Paint chroma;
    chroma.setLinearGradient(
        glassSample.getX(), glassSample.getY(),
        glassSample.getX() + glassSample.getWidth(),
        glassSample.getY() + glassSample.getHeight(),
        {
            Paint::ColorStop(0.0f, Color(255, 132, 146, 245)),
            Paint::ColorStop(0.48f, Color(134, 100, 255, 245)),
            Paint::ColorStop(1.0f, Color(48, 201, 231, 245)),
        });
    canvas.drawRoundRect(glassSample, 32.0f, chroma);
    canvas.drawCircle(glassSample.getX() + 108.0f,
                      glassSample.getY() + 56.0f, 43.0f,
                      solid(Color(255, 207, 173, 220)));
    drawGlassSurface(canvas,
                     RectF(glassSample.getX() + 18.0f,
                           glassSample.getY() + 18.0f, 122.0f, 144.0f),
                     24.0f, 12.0f, Color(255, 255, 255, 56),
                     Color(86, 154, 225, 40));

    const float squareWidth = 231.0f;
    const RectF insetCard(kRailLeft, 398.0f, squareWidth, 226.0f);
    const RectF textCard(kRailLeft + squareWidth + 20.0f, 398.0f,
                         squareWidth, 226.0f);
    drawTintedSurface(canvas, insetCard, 26.0f,
                      Color(30, 66, 111, 232),
                      Color(12, 35, 73, 242));
    drawTintedSurface(canvas, textCard, 26.0f,
                      Color(30, 66, 111, 232),
                      Color(12, 35, 73, 242));

    canvas.drawText("02", insetCard.getX() + 22.0f,
                    insetCard.getY() + 20.0f,
                    textStyle(11.0f, Color(109, 210, 235, 225), 700));
    drawInsetCircle(canvas, insetCard.getX() + insetCard.getWidth() * 0.5f,
                    insetCard.getY() + 91.0f, 47.0f);
    canvas.drawText("Inner depth", insetCard.getX() + 22.0f,
                    insetCard.getY() + 157.0f,
                    textStyle(20.0f, Color(244, 249, 255, 250), 600));
    canvas.drawText("cool / restrained", insetCard.getX() + 22.0f,
                    insetCard.getY() + 190.0f,
                    textStyle(12.0f, Color(162, 188, 219, 215)));

    canvas.drawText("03", textCard.getX() + 22.0f,
                    textCard.getY() + 20.0f,
                    textStyle(11.0f, Color(109, 210, 235, 225), 700));
    canvas.drawText("Aa", textCard.getX() + 22.0f,
                    textCard.getY() + 53.0f,
                    textStyle(62.0f, Color(244, 250, 255, 255), 300));
    canvas.drawText("\xE4\xB8\xAD\xE6\x96\x87", textCard.getX() + 128.0f,
                    textCard.getY() + 80.0f,
                    textStyle(20.0f, Color(98, 221, 239, 245), 600));
    canvas.drawText("Glyph system", textCard.getX() + 22.0f,
                    textCard.getY() + 157.0f,
                    textStyle(20.0f, Color(244, 249, 255, 250), 600));
    canvas.drawText("shape / fallback", textCard.getX() + 22.0f,
                    textCard.getY() + 190.0f,
                    textStyle(12.0f, Color(162, 188, 219, 215)));

    const RectF backendCard(kRailLeft, 646.0f, kRailWidth, 244.0f);
    drawTintedSurface(canvas, backendCard, 28.0f,
                      Color(30, 67, 115, 232),
                      Color(10, 32, 69, 242));
    canvas.drawText("04 / RENDER GRAPH", backendCard.getX() + 28.0f,
                    backendCard.getY() + 26.0f,
                    textStyle(11.0f, Color(109, 210, 235, 225), 700));
    canvas.drawText("One API.", backendCard.getX() + 28.0f,
                    backendCard.getY() + 62.0f,
                    textStyle(31.0f, Color(246, 251, 255, 255), 600));
    canvas.drawText("Four targets.", backendCard.getX() + 28.0f,
                    backendCard.getY() + 101.0f,
                    textStyle(31.0f, Color(177, 202, 235, 245), 300));

    const float hubX = backendCard.getX() + backendCard.getWidth() * 0.5f;
    const float hubY = backendCard.getY() + 160.0f;
    const float nodeY = backendCard.getY() + 205.0f;
    Paint connector;
    connector.setStyle(Paint::Style::STROKE);
    connector.setStrokeColor(Color(104, 174, 221, 85));
    connector.setStrokeWidth(1.0f);
    canvas.drawCircle(hubX, hubY, 18.0f,
                      solid(Color(54, 150, 205, 55)));
    canvas.drawCircle(hubX, hubY, 7.0f,
                      solid(Color(91, 222, 239, 245)));
    const Color nodes[] = {
        Color(114, 143, 180, 255), Color(66, 207, 235, 255),
        Color(74, 214, 145, 255), Color(143, 111, 250, 255),
    };
    const char *nodeLabels[] = {"SW", "GL", "ES", "VK"};
    for (int i = 0; i < 4; ++i) {
        const float nodeX = backendCard.getX() + 48.0f
            + static_cast<float>(i) * 126.0f;
        canvas.drawLine(hubX, hubY + 6.0f, nodeX, nodeY - 8.0f,
                        connector);
        canvas.drawCircle(nodeX, nodeY, 10.0f, solid(nodes[i]));
        Paint nodeLabel = textStyle(10.0f, Color(183, 204, 230, 220), 600);
        nodeLabel.setTextAlign(Paint::TextAlign::CENTER);
        canvas.drawText(nodeLabels[i], nodeX, nodeY + 18.0f, nodeLabel);
    }
}

std::string statsText(const Canvas::RenderStats *stats)
{
    if (stats == nullptr) {
        return "OpenGL  |  LIVE FRAME";
    }
    std::ostringstream stream;
    stream << "OpenGL  |  LIVE  |  "
           << stats->filterCount << " filters  |  "
           << stats->filterPassCount << " passes";
    return stream.str();
}

void drawStatusBar(Canvas &canvas, const Canvas::RenderStats *stats)
{
    const RectF bar(kContentLeft, 948.0f,
                    kRailLeft + kRailWidth - kContentLeft, 82.0f);
    drawTintedSurface(canvas, bar, 22.0f,
                      Color(25, 59, 104, 226),
                      Color(11, 34, 72, 238));

    canvas.drawCircle(bar.getX() + 35.0f, bar.getY() + 41.0f, 11.0f,
                      solid(Color(82, 213, 236, 255)));
    Paint statsPaint = textStyle(17.0f, Color(226, 239, 253, 246), 600);
    statsPaint.setTextBaseline(Paint::TextBaseline::MIDDLE);
    canvas.drawText(statsText(stats), bar.getX() + 58.0f,
                    bar.getY() + 41.0f, statsPaint);

    Paint targetLabel = textStyle(11.0f, Color(132, 158, 191, 210), 600);
    targetLabel.setTextBaseline(Paint::TextBaseline::MIDDLE);
    canvas.drawText("API TARGETS", bar.getX() + 675.0f,
                    bar.getY() + 41.0f, targetLabel);

    const char *backends[] = {"Software", "OpenGL", "OpenGLES", "Vulkan"};
    for (int i = 0; i < 4; ++i) {
        const float x = bar.getX() + 790.0f + static_cast<float>(i) * 184.0f;
        const bool active = i == 1;
        canvas.drawCircle(
            x, bar.getY() + 41.0f, active ? 8.0f : 6.0f,
            solid(active ? Color(69, 204, 238, 255)
                         : Color(121, 148, 182, 165)));
        Paint backend = textStyle(
            16.0f, active ? Color(231, 244, 255, 250)
                          : Color(181, 201, 226, 205),
            active ? 600 : 400);
        backend.setTextBaseline(Paint::TextBaseline::MIDDLE);
        canvas.drawText(backends[i], x + 18.0f, bar.getY() + 41.0f, backend);
    }
}

void drawMasthead(Canvas &canvas)
{
    canvas.drawCircle(kContentLeft + 6.0f, 54.0f, 5.0f,
                      solid(Color(73, 214, 237, 255)));
    canvas.drawText("WHATS / CANVAS", kContentLeft + 23.0f, 42.0f,
                    textStyle(13.0f, Color(228, 240, 253, 245), 700));
    canvas.drawText("MATERIAL OBSERVATORY  ·  STUDY 01",
                    kContentLeft + 176.0f, 43.0f,
                    textStyle(12.0f, Color(123, 153, 192, 210), 600));

    Paint right = textStyle(11.0f, Color(123, 153, 192, 195), 600);
    right.setTextAlign(Paint::TextAlign::RIGHT);
    canvas.drawText("STATIC 2D  /  NATIVE C++  /  1920 × 1080",
                    kRailLeft + kRailWidth, 43.0f, right);

    Paint rule;
    rule.setStyle(Paint::Style::STROKE);
    rule.setStrokeColor(Color(126, 179, 224, 34));
    rule.setStrokeWidth(1.0f);
    canvas.drawLine(kContentLeft, 88.0f, kRailLeft + kRailWidth,
                    88.0f, rule);
}

void drawShowcase(Canvas &canvas, const Canvas::RenderStats *stats)
{
    drawBackground(canvas);
    drawSidebar(canvas);
    drawMasthead(canvas);
    drawHero(canvas);
    drawCapabilityRail(canvas);
    drawStatusBar(canvas, stats);
}

} // namespace

int main(int argc, char **argv)
{
    const std::string outputPath =
        argc > 1 ? argv[1] : "images/image-filter-showcase.png";

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 0);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif

    GLFWwindow *window = glfwCreateWindow(
        kWidth, kHeight, "WhatsCanvas Showcase", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create OpenGL window\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth != kWidth || framebufferHeight != kHeight) {
        std::cerr << "Unexpected framebuffer size " << framebufferWidth
                  << 'x' << framebufferHeight << "; expected "
                  << kWidth << 'x' << kHeight << '\n';
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (!Canvas::loadOpenGL(
            reinterpret_cast<Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
        std::cerr << "Failed to load OpenGL\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    auto canvas = Canvas::create(Canvas::Backend::OpenGL, kWidth, kHeight);
    if (!canvas || !canvas->initializeContext()) {
        std::cerr << "Failed to initialize WhatsCanvas\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (!registerShowcaseFonts(*canvas)) {
        std::cerr << "Failed to register the portable showcase fonts\n";
        canvas.reset();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    canvas->setSize(kWidth, kHeight);
    if (!canvas->setOutputTarget(
            OutputTarget::GLFramebuffer(0, kWidth, kHeight, true))) {
        std::cerr << "Failed to set the OpenGL output target\n";
        canvas.reset();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    canvas->beginFrame();
    drawShowcase(*canvas, nullptr);
    canvas->endFrame();
    const Canvas::RenderStats firstFrameStats = canvas->getRenderStats();

    canvas->beginFrame();
    drawShowcase(*canvas, &firstFrameStats);
    canvas->endFrame();

    const Canvas::RenderStats stats = canvas->getRenderStats();
    const GLenum renderError = glGetError();
    std::cout << "Showcase commands " << stats.commandCount
              << ", draws " << stats.drawCallCount
              << ", filters " << stats.filterCount
              << ", filter passes " << stats.filterPassCount
              << ", downsampled " << stats.downsampledFilterCount
              << ", filter pixel-passes " << stats.filterPixelPassCount
              << ", GL error " << renderError << '\n';

    std::vector<unsigned char> pixels;
    const bool read = canvas->readPixelsRGBA(pixels);
    const GLenum readbackError = glGetError();
    const bool wrote = renderError == GL_NO_ERROR
        && readbackError == GL_NO_ERROR
        && read
        && pixels.size() == static_cast<std::size_t>(kWidth) * kHeight * 4u
        && stbi_write_png(outputPath.c_str(), kWidth, kHeight, 4,
                          pixels.data(), kWidth * 4) != 0;

    canvas.reset();
    glfwDestroyWindow(window);
    glfwTerminate();

    if (!wrote) {
        std::cerr << "Failed to render or write " << outputPath
                  << " (render GL error " << renderError
                  << ", readback GL error " << readbackError << ")\n";
        return 1;
    }
    std::cout << "Wrote " << outputPath << " (" << kWidth << 'x' << kHeight
              << ")\n";
    return 0;
}
