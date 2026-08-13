#include "wsc/wsc.h"

#include "../src/canvas/StrokeTessellator.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        return false;
    }
    return true;
}

bool near(float actual, float expected, float epsilon = 0.0001f)
{
    return std::fabs(actual - expected) <= epsilon;
}

bool testAlphaAndModes()
{
    wsc::Paint paint;
    paint.setAlpha(-12);
    bool ok = expect(paint.getAlpha() == 0, "integer alpha should clamp low");

    paint.setAlpha(1.5f);
    ok = expect(paint.getAlpha() == 255, "float alpha should clamp high") && ok;
    paint.setAlpha(0.5f);
    ok = expect(paint.getAlpha() == 128, "float alpha should round to byte alpha") && ok;
    ok = expect(near(paint.getAlphaF(), 128.0f / 255.0f), "float alpha getter should reflect byte alpha") && ok;

    paint.setBlendMode(wsc::Paint::BlendMode::SCREEN);
    paint.setImageSampling(wsc::Paint::ImageSampling::MIPMAP_LINEAR);
    paint.setImageTileMode(wsc::Paint::ImageTileMode::DECAL);
    paint.setShaderTileMode(wsc::Paint::ShaderTileMode::MIRROR);
    ok = expect(paint.getBlendMode() == wsc::Paint::BlendMode::SCREEN, "blend mode should round trip") && ok;
    ok = expect(paint.getImageSampling() == wsc::Paint::ImageSampling::MIPMAP_LINEAR,
                "image sampling should round trip") && ok;
    ok = expect(paint.getImageTileMode() == wsc::Paint::ImageTileMode::DECAL,
                "image tile mode should round trip") && ok;
    ok = expect(paint.getShaderTileMode() == wsc::Paint::ShaderTileMode::MIRROR,
                "shader tile mode should round trip") && ok;

    return ok;
}

bool testGradientStopsNormalize()
{
    wsc::Paint paint;
    paint.setLinearGradient(4.0f, 8.0f, 44.0f, 88.0f,
                            {
                                wsc::Paint::ColorStop(1.5f, wsc::Color::RED),
                                wsc::Paint::ColorStop(0.5f, wsc::Color::GREEN),
                                wsc::Paint::ColorStop(-0.5f, wsc::Color::BLUE),
                                wsc::Paint::ColorStop(std::numeric_limits<float>::infinity(), wsc::Color::WHITE),
                            });

    const auto &stops = paint.getGradientStops();
    bool ok = expect(paint.hasLinearGradient(), "linear gradient flag should be set");
    ok = expect(!paint.hasRadialGradient(), "radial gradient flag should be clear") && ok;
    ok = expect(stops.size() == 3, "gradient should drop non-finite stops") && ok;
    if (stops.size() == 3) {
        ok = expect(near(stops[0].position, 0.0f), "gradient should clamp low stop position") && ok;
        ok = expect(near(stops[1].position, 0.5f), "gradient should preserve middle stop position") && ok;
        ok = expect(near(stops[2].position, 1.0f), "gradient should clamp high stop position") && ok;
        ok = expect(paint.getGradientStartColor().getB() == 255, "start color should follow first sorted stop") && ok;
        ok = expect(paint.getGradientEndColor().getR() == 255, "end color should follow last sorted stop") && ok;
    }
    ok = expect(near(paint.getGradientStartX(), 4.0f), "gradient start x should round trip") && ok;
    ok = expect(near(paint.getGradientEndY(), 88.0f), "gradient end y should round trip") && ok;

    paint.clearShader();
    ok = expect(paint.getShaderType() == wsc::Paint::ShaderType::SOLID, "clearShader should restore solid shader") && ok;

    paint.setRadialGradient(12.0f, 16.0f, 32.0f,
                            {
                                wsc::Paint::ColorStop(0.0f, wsc::Color::WHITE),
                                wsc::Paint::ColorStop(1.0f, wsc::Color::BLACK),
                            });
    ok = expect(paint.hasRadialGradient(), "radial gradient flag should be set") && ok;
    ok = expect(near(paint.getRadialCenterX(), 12.0f), "radial center x should round trip") && ok;
    ok = expect(near(paint.getRadialRadius(), 32.0f), "radial radius should round trip") && ok;

    return ok;
}

bool testPathEffectsNormalize()
{
    wsc::Paint paint;
    paint.setCornerPathEffect(-2.0f);
    bool ok = expect(!paint.hasCornerPathEffect(), "negative corner effect radius should disable effect");

    paint.setCornerPathEffect(std::numeric_limits<float>::quiet_NaN());
    ok = expect(!paint.hasCornerPathEffect(), "non-finite corner effect radius should disable effect") && ok;

    paint.setCornerPathEffect(6.5f);
    ok = expect(paint.hasCornerPathEffect(), "positive corner effect radius should enable effect") && ok;
    ok = expect(near(paint.getCornerPathEffectRadius(), 6.5f), "corner effect radius should round trip") && ok;
    paint.clearCornerPathEffect();
    ok = expect(!paint.hasCornerPathEffect(), "clearCornerPathEffect should disable effect") && ok;

    paint.setDashPathEffect({8.0f, -1.0f, 4.0f, std::numeric_limits<float>::infinity(), 2.0f}, 3.0f);
    const auto &intervals = paint.getDashIntervals();
    ok = expect(paint.hasDashPathEffect(), "valid dash intervals should enable dash effect") && ok;
    ok = expect(intervals.size() == 6, "odd dash intervals should duplicate to an even count") && ok;
    if (intervals.size() == 6) {
        ok = expect(near(intervals[0], 8.0f) && near(intervals[1], 4.0f) && near(intervals[2], 2.0f),
                    "dash intervals should keep positive finite values") && ok;
        ok = expect(near(intervals[3], 8.0f) && near(intervals[4], 4.0f) && near(intervals[5], 2.0f),
                    "dash intervals should duplicate odd interval lists") && ok;
    }
    ok = expect(near(paint.getDashPhase(), 3.0f), "finite dash phase should round trip") && ok;

    paint.setDashPathEffect({0.0f, -3.0f, std::numeric_limits<float>::quiet_NaN()}, 5.0f);
    ok = expect(!paint.hasDashPathEffect(), "invalid dash intervals should disable dash effect") && ok;
    ok = expect(near(paint.getDashPhase(), 0.0f), "empty dash intervals should reset phase") && ok;

    paint.setDashPathEffect({5.0f, 2.0f}, std::numeric_limits<float>::infinity());
    ok = expect(near(paint.getDashPhase(), 0.0f), "non-finite dash phase should reset to zero") && ok;
    paint.clearDashPathEffect();
    ok = expect(!paint.hasDashPathEffect(), "clearDashPathEffect should disable effect") && ok;

    return ok;
}

bool testColorMatrixAndShadow()
{
    wsc::Paint paint;
    std::array<float, 20> matrix = {
        1.0f, 0.0f, 0.0f, 0.0f, 10.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 20.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 30.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f
    };
    paint.setColorMatrix(matrix);
    bool ok = expect(paint.hasColorMatrix(), "finite color matrix should enable effect");
    ok = expect(near(paint.getColorMatrix()[4], 10.0f), "color matrix values should round trip") && ok;

    matrix[7] = std::numeric_limits<float>::quiet_NaN();
    paint.setColorMatrix(matrix);
    ok = expect(!paint.hasColorMatrix(), "non-finite color matrix should clear effect") && ok;
    ok = expect(near(paint.getColorMatrix()[0], 1.0f) && near(paint.getColorMatrix()[18], 1.0f),
                "cleared color matrix should restore identity") && ok;

    paint.setShadowLayer(4.0f, 2.0f, 3.0f, wsc::Color(1, 2, 3, 0));
    ok = expect(!paint.hasShadowLayer(), "transparent shadow color should not report an active shadow") && ok;
    paint.setShadowLayer(4.0f, 2.0f, 3.0f, wsc::Color(1, 2, 3, 128));
    ok = expect(paint.hasShadowLayer(), "non-transparent shadow color should report an active shadow") && ok;
    ok = expect(near(paint.getShadowRadius(), 4.0f) && near(paint.getShadowDx(), 2.0f)
                && near(paint.getShadowDy(), 3.0f), "shadow geometry should round trip") && ok;
    paint.clearShadowLayer();
    ok = expect(!paint.hasShadowLayer(), "clearShadowLayer should disable shadow") && ok;

    return ok;
}

bool testTextAndStrokeState()
{
    wsc::Paint paint;
    paint.setFont("Inter");
    bool ok = expect(paint.hasFontFamily(), "setFont should set font family");
    ok = expect(paint.getFontFamily() == "Inter" && paint.getFont() == "Inter", "font aliases should round trip") && ok;
    paint.clearFont();
    ok = expect(!paint.hasFontFamily(), "clearFont should clear font family") && ok;

    paint.setFontWeight(-200);
    ok = expect(paint.getFontWeight() == 1, "font weight should clamp low") && ok;
    paint.setFontWeight(1400);
    ok = expect(paint.getFontWeight() == 1000, "font weight should clamp high") && ok;
    paint.setFontWeight(650);
    paint.setFontSlant(wsc::FontSlant::ITALIC);
    ok = expect(paint.getFontWeight() == 650, "font weight should round trip") && ok;
    ok = expect(paint.getFontSlant() == wsc::FontSlant::ITALIC, "font slant should round trip") && ok;

    paint.setFontFeature("liga", 0);
    paint.setFontFeature("kern", 1);
    paint.setFontFeature("bad", 1);
    ok = expect(paint.getFontFeatures().size() == 2,
                "only four-character OpenType feature tags should be retained") && ok;
    ok = expect(paint.getFontFeatures()[0].tag == "liga"
                    && paint.getFontFeatures()[0].value == 0,
                "font feature values should round trip") && ok;
    paint.setFontFeature("liga", 1);
    ok = expect(paint.getFontFeatures().size() == 2
                    && paint.getFontFeatures()[0].value == 1,
                "setting an existing font feature should update it in place") && ok;
    paint.clearFontFeatures();
    ok = expect(paint.getFontFeatures().empty(), "font features should clear") && ok;

    paint.setLetterSpacing(std::numeric_limits<float>::infinity());
    ok = expect(near(paint.getLetterSpacing(), 0.0f), "non-finite letter spacing should reset to zero") && ok;
    paint.setLetterSpacing(1.25f);
    ok = expect(near(paint.getLetterSpacing(), 1.25f), "finite letter spacing should round trip") && ok;

    paint.setTextAlign(wsc::Paint::TextAlign::RIGHT);
    paint.setTextBaseline(wsc::Paint::TextBaseline::BOTTOM);
    paint.setStyle(wsc::Paint::Style::FILL_AND_STROKE);
    paint.setStrokeCap(wsc::Paint::StrokeCap::ROUND);
    paint.setStrokeJoin(wsc::Paint::StrokeJoin::BEVEL);
    paint.setStrokeMiterLimit(std::numeric_limits<float>::quiet_NaN());
    ok = expect(near(paint.getStrokeMiterLimit(), 1.0f), "non-finite miter limit should clamp to one") && ok;
    paint.setStrokeMiterLimit(0.25f);
    ok = expect(near(paint.getStrokeMiterLimit(), 1.0f), "small miter limit should clamp to one") && ok;
    paint.setStrokeMiterLimit(6.0f);
    ok = expect(paint.getTextAlign() == wsc::Paint::TextAlign::RIGHT, "text align should round trip") && ok;
    ok = expect(paint.getTextBaseline() == wsc::Paint::TextBaseline::BOTTOM, "text baseline should round trip") && ok;
    ok = expect(paint.getStyle() == wsc::Paint::Style::FILL_AND_STROKE, "paint style should round trip") && ok;
    ok = expect(paint.getStrokeCap() == wsc::Paint::StrokeCap::ROUND, "stroke cap should round trip") && ok;
    ok = expect(paint.getStrokeJoin() == wsc::Paint::StrokeJoin::BEVEL, "stroke join should round trip") && ok;
    ok = expect(near(paint.getStrokeMiterLimit(), 6.0f), "stroke miter limit should round trip") && ok;

    paint.setColor(wsc::Color::CYAN);
    ok = expect(paint.getColor().getG() == 255 && paint.getStrokeColor().getG() == 255,
                "setColor should update fill and stroke color") && ok;
    paint.setLinearGradient(0.0f, 0.0f, 1.0f, 0.0f, wsc::Color::BLACK, wsc::Color::WHITE);
    paint.setFillColor(wsc::Color::MAGENTA);
    ok = expect(paint.getShaderType() == wsc::Paint::ShaderType::SOLID, "setFillColor should clear shader") && ok;

    return ok;
}

bool testMiterLimitAffectsStrokeMesh()
{
    const std::vector<wsc::detail::Vec2> points = {
        {0.0f, 0.0f},
        {50.0f, 0.0f},
        {52.0f, 100.0f},
    };

    const auto mitered = wsc::detail::tessellateStroke(
        points,
        {10.0f, wsc::detail::StrokeJoin::Miter, wsc::detail::StrokeCap::Butt, false, 64.0f});

    const auto beveled = wsc::detail::tessellateStroke(
        points,
        {10.0f, wsc::detail::StrokeJoin::Miter, wsc::detail::StrokeCap::Butt, false, 1.0f});

    return expect(mitered.size() == 12, "high miter limit should keep the miter joint")
        && expect(beveled.size() > mitered.size(), "low miter limit should bevel sharp joints");
}

} // namespace

int main()
{
    bool ok = true;
    ok = testAlphaAndModes() && ok;
    ok = testGradientStopsNormalize() && ok;
    ok = testPathEffectsNormalize() && ok;
    ok = testColorMatrixAndShadow() && ok;
    ok = testTextAndStrokeState() && ok;
    ok = testMiterLimitAffectsStrokeMesh() && ok;
    return ok ? 0 : 1;
}
