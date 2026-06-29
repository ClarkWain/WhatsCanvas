#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "text/TextShaper.h"
#include "text/TextUtils.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }

    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

bool testDecodeValidUtf8()
{
    const std::string text = "A\xe4\xb8\xad\xf0\x9f\x98\x80";
    const auto codepoints = wsc::text::decodeUtf8(text);

    return expect(codepoints.size() == 3, "valid UTF-8 should decode to three codepoints")
        && expect(codepoints[0].value == 'A', "ASCII codepoint should be preserved")
        && expect(codepoints[1].value == 0x4E2D, "CJK codepoint should be decoded")
        && expect(codepoints[2].value == 0x1F600, "four-byte codepoint should be decoded")
        && expect(wsc::text::isValidUtf8(text), "valid UTF-8 should validate");
}

bool testInvalidUtf8Replacement()
{
    const std::string text = std::string("A") + static_cast<char>(0xC0) + "B";
    const auto codepoints = wsc::text::decodeUtf8(text);

    return expect(codepoints.size() == 3, "invalid byte should consume one byte")
        && expect(!codepoints[1].valid, "invalid byte should be marked invalid")
        && expect(!wsc::text::isValidUtf8(text), "invalid UTF-8 should not validate")
        && expect(wsc::text::normalizeUtf8ForText(text) == (std::string("A") + "\xEF\xBF\xBD" + "B"),
                  "invalid byte should normalize to replacement character");
}

bool testAsciiFallbackKeepsShape()
{
    const std::string text = "Hi\t\xe4\xb8\xad\n!";
    const std::string fallback = wsc::text::makeAsciiFallbackText(text);

    return expect(fallback == "Hi    ?\n!", "fallback should preserve ASCII controls and replace non-ASCII")
        && expect(wsc::text::countUtf8Codepoints("A\xe4\xb8\xad") == 2,
                  "codepoint count should count Unicode scalar positions, not bytes");
}

bool testSimpleShaperBuildsGlyphRun()
{
    const std::string text = "A\xe4\xb8\xad";
    const auto run = wsc::text::shapeTextSimple(text,
                                                1.5f,
                                                [](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
        if (codepoint == 'A') {
            return wsc::text::ResolvedGlyph{3, 7.0f};
        }
        if (codepoint == 0x4E2D) {
            return wsc::text::ResolvedGlyph{9, 11.0f};
        }
        return std::nullopt;
    });

    return expect(run.has_value(), "simple shaper should resolve known codepoints")
        && expect(run->glyphs.size() == 2, "simple shaper should emit one glyph per decoded scalar")
        && expect(run->glyphs[0].sourceStart == 0 && run->glyphs[0].sourceLength == 1,
                  "ASCII glyph should retain source byte mapping")
        && expect(run->glyphs[0].glyphIndex == 3, "simple shaper should retain resolved glyph index")
        && expect(run->glyphs[1].sourceStart == 1 && run->glyphs[1].sourceLength == 3,
                  "multi-byte glyph should retain source byte mapping")
        && expect(run->width == 19.5f, "simple shaper should include letter spacing between glyphs");
}

bool testSimpleShaperStopsAtFirstLineAndFailsMissingGlyphs()
{
    const auto singleLine = wsc::text::shapeTextSimple("AB\nC",
                                                       2.0f,
                                                       [](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
        return wsc::text::ResolvedGlyph{static_cast<int>(codepoint), 5.0f};
    });
    const auto missing = wsc::text::shapeTextSimple("A?",
                                                    0.0f,
                                                    [](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
        return codepoint == 'A'
            ? std::optional<wsc::text::ResolvedGlyph>(wsc::text::ResolvedGlyph{1, 5.0f})
            : std::nullopt;
    });

    return expect(singleLine.has_value(), "simple shaper should shape the first line")
        && expect(singleLine->glyphs.size() == 2, "simple shaper should stop at newline")
        && expect(singleLine->width == 12.0f, "simple shaper should space first-line glyphs")
        && expect(!missing.has_value(), "simple shaper should fail when a glyph cannot be resolved");
}

bool testSimpleShaperOrdersRightToLeftRuns()
{
    const std::string hebrew = "\xd7\x90\xd7\x91";
    const auto run = wsc::text::shapeTextSimple(hebrew,
                                                0.0f,
                                                [](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
        return wsc::text::ResolvedGlyph{static_cast<int>(codepoint), 6.0f};
    });

    return expect(run.has_value(), "simple shaper should shape RTL codepoints")
        && expect(run->rightToLeft, "simple shaper should detect RTL runs")
        && expect(run->glyphs.size() == 2, "RTL run should keep glyph count")
        && expect(run->glyphs[0].sourceStart == 2, "RTL visual order should place second source glyph first")
        && expect(run->glyphs[1].sourceStart == 0, "RTL visual order should place first source glyph second");
}

} // namespace

int main()
{
    const bool ok = testDecodeValidUtf8()
        && testInvalidUtf8Replacement()
        && testAsciiFallbackKeepsShape()
        && testSimpleShaperBuildsGlyphRun()
        && testSimpleShaperStopsAtFirstLineAndFailsMissingGlyphs()
        && testSimpleShaperOrdersRightToLeftRuns();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
