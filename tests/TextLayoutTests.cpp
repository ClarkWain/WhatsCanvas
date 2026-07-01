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

bool isValidUtf8(const std::string &text)
{
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch < 0x80) {
            ++i;
            continue;
        }

        std::size_t length = 0;
        std::uint32_t value = 0;
        std::uint32_t minimum = 0;
        if ((ch & 0xE0) == 0xC0) {
            length = 2;
            value = ch & 0x1F;
            minimum = 0x80;
        } else if ((ch & 0xF0) == 0xE0) {
            length = 3;
            value = ch & 0x0F;
            minimum = 0x800;
        } else if ((ch & 0xF8) == 0xF0) {
            length = 4;
            value = ch & 0x07;
            minimum = 0x10000;
        } else {
            return false;
        }

        if (i + length > text.size()) {
            return false;
        }
        for (std::size_t j = 1; j < length; ++j) {
            const unsigned char continuation = static_cast<unsigned char>(text[i + j]);
            if ((continuation & 0xC0) != 0x80) {
                return false;
            }
            value = (value << 6) | (continuation & 0x3F);
        }
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
            return false;
        }
        i += length;
    }
    return true;
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

bool testLongWordWrappingWithoutSpaces()
{
    wsc::Canvas canvas;
    wsc::Paint paint;
    paint.setTextSize(12.0f);
    const std::string text = "supercalifragilistic";
    const std::vector<wsc::Canvas::TextLine> lines =
        canvas.layoutTextBox(text,
                             wsc::RectF(0.0f, 0.0f, 18.0f, 100.0f),
                             14.0f,
                             paint);

    return expect(lines.size() >= 2, "long unspaced words should wrap across lines")
        && expect(lines[0].sourceStart == 0, "first long-word line should map to source start")
        && expect(lines[0].sourceLength > 0 && lines[0].sourceLength < text.size(),
                  "first long-word line should contain a partial source span");
}

bool testCjkEllipsisKeepsValidUtf8()
{
    wsc::Canvas canvas;
    wsc::Paint paint;
    paint.setTextSize(12.0f);
    const std::vector<wsc::Canvas::TextLine> lines =
        canvas.layoutTextBox("\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c",
                             wsc::RectF(0.0f, 0.0f, 24.0f, 14.0f),
                             14.0f,
                             1,
                             true,
                             paint);

    return expect(lines.size() == 1, "CJK ellipsis layout should keep one visible line")
        && expect(lines[0].ellipsized, "CJK constrained layout should ellipsize")
        && expect(isValidUtf8(lines[0].text), "CJK ellipsis should not split UTF-8 scalars")
        && expect(lines[0].text.find("...") != std::string::npos, "CJK ellipsis should include marker");
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
        && testLongWordWrappingWithoutSpaces()
        && testCjkEllipsisKeepsValidUtf8()
        && testInvalidInputs();
    return ok ? 0 : 1;
}
