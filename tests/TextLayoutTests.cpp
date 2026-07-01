#include <iostream>
#include <string>
#include <vector>

#include "wsc/wsc.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }

    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

wsc::Paint makeTextPaint()
{
    wsc::Paint paint;
    paint.setTextSize(12.0f);
    paint.setFontFamily("Consolas");
    return paint;
}

bool testParagraphRanges()
{
    wsc::Canvas canvas;
    wsc::Paint paint = makeTextPaint();
    const std::vector<wsc::Canvas::TextLine> lines =
        canvas.layoutTextBox("alpha beta\ngamma", wsc::RectF(10.0f, 20.0f, 400.0f, 120.0f), 18.0f, paint);

    return expect(lines.size() == 2, "layout should preserve two paragraph rows")
        && expect(lines[0].text == "alpha beta", "first line should contain first paragraph")
        && expect(lines[0].sourceStart == 0, "first line source start should be zero")
        && expect(lines[0].sourceLength == 10, "first line source length should include inner space")
        && expect(lines[1].text == "gamma", "second line should contain second paragraph")
        && expect(lines[1].sourceStart == 11, "second line source start should skip newline")
        && expect(lines[1].sourceLength == 5, "second line source length should match gamma")
        && expect(lines[0].x == 10.0f, "left aligned x should use bounds x")
        && expect(lines[0].y == 20.0f, "first line y should use bounds y")
        && expect(lines[1].y == 38.0f, "line height should advance y");
}

bool testAlignAndEllipsis()
{
    wsc::Canvas canvas;
    wsc::Paint paint = makeTextPaint();
    paint.setTextAlign(wsc::Paint::TextAlign::CENTER);
    const std::vector<wsc::Canvas::TextLine> centered =
        canvas.layoutTextBox("alpha beta gamma delta", wsc::RectF(20.0f, 30.0f, 88.0f, 80.0f), 16.0f, 1, true, paint);

    return expect(centered.size() == 1, "maxLines should cap layout rows")
        && expect(centered[0].x == 64.0f, "center aligned x should use bounds center")
        && expect(centered[0].ellipsized, "capped overflowing line should be ellipsized")
        && expect(centered[0].text.size() >= 3, "ellipsized text should keep marker text")
        && expect(centered[0].text.find("...") != std::string::npos, "ellipsized text should include marker");
}

bool testCjkWrappingWithoutSpaces()
{
    wsc::Canvas canvas;
    wsc::Paint paint;
    paint.setTextSize(12.0f);
    const std::vector<wsc::Canvas::TextLine> lines =
        canvas.layoutTextBox("\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c",
                             wsc::RectF(0.0f, 0.0f, 12.0f, 100.0f),
                             14.0f,
                             paint);

    return expect(lines.size() >= 2, "CJK text without spaces should wrap across lines")
        && expect(lines[0].sourceStart == 0, "first CJK line should map to source start")
        && expect(lines[0].sourceLength > 0 && lines[0].sourceLength < 12,
                  "first CJK line should contain a partial UTF-8 source span");
}

bool testInvalidInputs()
{
    wsc::Canvas canvas;
    wsc::Paint paint = makeTextPaint();
    return expect(canvas.layoutTextBox("", wsc::RectF(0.0f, 0.0f, 100.0f, 40.0f), paint).empty(),
                  "empty text should not layout")
        && expect(canvas.layoutTextBox("text", wsc::RectF(0.0f, 0.0f, 0.0f, 40.0f), paint).empty(),
                  "empty bounds should not layout");
}

} // namespace

int main()
{
    const bool ok = testParagraphRanges()
        && testAlignAndEllipsis()
        && testCjkWrappingWithoutSpaces()
        && testInvalidInputs();
    return ok ? 0 : 1;
}
