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

bool testTextShapingEngineFactoryFallsBackToSimple()
{
    const auto simple = wsc::text::createTextShapingEngine(wsc::text::TextShapingBackend::Simple);
    const auto requestedOpenType = wsc::text::createTextShapingEngine(wsc::text::TextShapingBackend::OpenType);
    const bool openTypeAvailable = wsc::text::isOpenTypeShapingAvailable();

    bool ok = expect(simple != nullptr, "simple shaping engine should be constructible");
    ok = expect(simple->backend() == wsc::text::TextShapingBackend::Simple,
                "simple shaping engine should report simple backend") && ok;
    ok = expect(!simple->supportsOpenTypeFeatures(),
                "simple shaping engine should not claim OpenType support") && ok;
    ok = expect(requestedOpenType != nullptr,
                "OpenType shaping request should return a usable engine") && ok;
    if (openTypeAvailable) {
        ok = expect(requestedOpenType->backend() == wsc::text::TextShapingBackend::OpenType,
                    "available OpenType shaping request should return OpenType backend") && ok;
        ok = expect(requestedOpenType->supportsOpenTypeFeatures(),
                    "available OpenType shaping engine should report feature support") && ok;
    } else {
        ok = expect(requestedOpenType->backend() == wsc::text::TextShapingBackend::Simple,
                    "unavailable OpenType shaping request should fall back to simple backend") && ok;
    }
    return ok;
}

bool testBidiRunSegmentation()
{
    const std::string mixed = "abc \xd7\x90\xd7\x91 def";
    const std::vector<wsc::text::BidiRun> runs = wsc::text::segmentBidiRuns(mixed);

    bool ok = expect(runs.size() == 3, "mixed LTR/RTL text should split into three bidi runs");
    ok = expect(!runs[0].rightToLeft && runs[0].sourceStart == 0 && runs[0].sourceEnd == 4,
                "first bidi run should be LTR and include trailing neutral space") && ok;
    ok = expect(runs[1].rightToLeft && runs[1].sourceStart == 4 && runs[1].sourceEnd == 9,
                "second bidi run should be RTL and include trailing neutral space") && ok;
    ok = expect(!runs[2].rightToLeft && runs[2].sourceStart == 9 && runs[2].sourceEnd == mixed.size(),
                "third bidi run should return to LTR") && ok;
    return ok;
}

bool testBidiRunSegmentationKeepsLeadingNeutrals()
{
    const std::string text = "  \xd7\x90\xd7\x91";
    const std::vector<wsc::text::BidiRun> runs = wsc::text::segmentBidiRuns(text);

    return expect(runs.size() == 1, "leading neutral RTL text should stay in one run")
        && expect(runs[0].rightToLeft, "first strong RTL direction should set the run direction")
        && expect(runs[0].sourceStart == 0, "leading neutral bytes should be kept in the run")
        && expect(runs[0].sourceEnd == text.size(), "RTL run should include the full visible text");
}

bool testBidiRunSegmentationKeepsWeakOnlyText()
{
    const std::string text = "123 !?";
    const std::vector<wsc::text::BidiRun> runs = wsc::text::segmentBidiRuns(text);

    return expect(runs.size() == 1, "weak-only text should still produce a renderable run")
        && expect(!runs[0].rightToLeft, "weak-only text should default to LTR direction")
        && expect(runs[0].sourceStart == 0 && runs[0].sourceEnd == text.size(),
                  "weak-only run should cover the full visible text");
}

} // namespace

int main()
{
    const bool ok = testDecodeValidUtf8()
        && testInvalidUtf8Replacement()
        && testAsciiFallbackKeepsShape()
        && testSimpleShaperBuildsGlyphRun()
        && testSimpleShaperStopsAtFirstLineAndFailsMissingGlyphs()
        && testSimpleShaperOrdersRightToLeftRuns()
        && testTextShapingEngineFactoryFallsBackToSimple()
        && testBidiRunSegmentation()
        && testBidiRunSegmentationKeepsLeadingNeutrals()
        && testBidiRunSegmentationKeepsWeakOnlyText();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
