#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "text/FontRasterizer.h"
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

void appendU16BE(std::vector<std::uint8_t> &bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

void appendU32BE(std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

std::vector<std::uint8_t> makeSfntWithTables(const std::vector<std::string> &tags)
{
    std::vector<std::uint8_t> bytes;
    appendU32BE(bytes, 0x00010000u);
    appendU16BE(bytes, static_cast<std::uint16_t>(tags.size()));
    appendU16BE(bytes, 0);
    appendU16BE(bytes, 0);
    appendU16BE(bytes, 0);
    for (const std::string &tag : tags) {
        for (std::size_t i = 0; i < 4u; ++i) {
            bytes.push_back(i < tag.size() ? static_cast<std::uint8_t>(tag[i]) : static_cast<std::uint8_t>(' '));
        }
        appendU32BE(bytes, 0);
        appendU32BE(bytes, 0);
        appendU32BE(bytes, 0);
    }
    return bytes;
}

std::vector<std::uint8_t> makeTtcWithFirstFontTables(const std::vector<std::string> &tags)
{
    std::vector<std::uint8_t> bytes;
    appendU32BE(bytes, 0x74746366u);
    appendU32BE(bytes, 0x00010000u);
    appendU32BE(bytes, 1u);
    appendU32BE(bytes, 16u);
    const std::vector<std::uint8_t> sfnt = makeSfntWithTables(tags);
    bytes.insert(bytes.end(), sfnt.begin(), sfnt.end());
    return bytes;
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

bool testUnicodeBreakTokensSplitCjkText()
{
    const std::string text = "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c";
    const std::vector<wsc::text::TextBreakToken> tokens =
        wsc::text::buildTextBreakTokens(text, 0, text.size());

    return expect(tokens.size() == 4, "CJK text should expose per-codepoint break tokens")
        && expect(tokens[0].sourceStart == 0 && tokens[0].sourceEnd == 3,
                  "first CJK token should map the first UTF-8 scalar")
        && expect(tokens[3].sourceStart == 9 && tokens[3].sourceEnd == text.size(),
                  "last CJK token should map the final UTF-8 scalar");
}

bool testUnicodeBreakTokensAttachClosingPunctuation()
{
    const std::string text = "\xe4\xbd\xa0\xef\xbc\x8c\xe5\xa5\xbd";
    const std::vector<wsc::text::TextBreakToken> tokens =
        wsc::text::buildTextBreakTokens(text, 0, text.size());

    return expect(tokens.size() == 2, "closing CJK punctuation should attach to the previous token")
        && expect(tokens[0].sourceStart == 0 && tokens[0].sourceEnd == 6,
                  "attached punctuation should stay in the previous source span")
        && expect(tokens[1].sourceStart == 6 && tokens[1].sourceEnd == text.size(),
                  "following CJK character should remain independently breakable");
}

bool testUnicodeBreakTokensTreatWhitespaceAsBreaks()
{
    const std::string text = "alpha\tbeta\xe3\x80\x80gamma";
    const std::vector<wsc::text::TextBreakToken> tokens =
        wsc::text::buildTextBreakTokens(text, 0, text.size());

    return expect(tokens.size() == 3, "tab and Unicode spaces should split break tokens")
        && expect(tokens[0].sourceStart == 0 && tokens[0].sourceEnd == 5 && !tokens[0].prefixSpace,
                  "first whitespace token should retain alpha source range")
        && expect(tokens[1].sourceStart == 6 && tokens[1].sourceEnd == 10 && tokens[1].prefixSpace,
                  "tab-separated token should request a collapsed prefix space")
        && expect(tokens[2].sourceStart == 13 && tokens[2].sourceEnd == text.size() && tokens[2].prefixSpace,
                  "ideographic-space-separated token should request a collapsed prefix space");
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

bool testSimpleShaperMirrorsRightToLeftPunctuation()
{
    const std::string text = "\xd7\x90(";
    const auto run = wsc::text::shapeTextSimple(text,
                                                0.0f,
                                                [](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
        return wsc::text::ResolvedGlyph{static_cast<int>(codepoint), 6.0f};
    });

    return expect(run.has_value(), "simple shaper should shape RTL punctuation")
        && expect(run->rightToLeft, "RTL punctuation run should use RTL visual ordering")
        && expect(run->glyphs.size() == 2, "RTL punctuation run should keep glyph count")
        && expect(run->glyphs[0].codepoint == ')', "RTL opening parenthesis should be mirrored for rendering")
        && expect(run->glyphs[0].sourceStart == 2 && run->glyphs[0].sourceLength == 1,
                  "mirrored punctuation should retain the original source range")
        && expect(run->glyphs[1].codepoint == 0x05D0, "strong RTL glyph should remain unchanged");
}

bool testSimpleShaperSkipsBidiControls()
{
    const std::string text = std::string("A") + "\xE2\x80\x8F" + "B";
    const auto run = wsc::text::shapeTextSimple(text,
                                                1.0f,
                                                [](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
        return wsc::text::ResolvedGlyph{static_cast<int>(codepoint), 5.0f};
    });

    return expect(run.has_value(), "simple shaper should ignore bidi controls while shaping")
        && expect(run->glyphs.size() == 2, "bidi controls should not emit visible glyphs")
        && expect(run->glyphs[0].codepoint == 'A' && run->glyphs[1].codepoint == 'B',
                  "visible glyph order should ignore RLM")
        && expect(run->width == 11.0f, "letter spacing should only apply between visible glyphs");
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

bool testBidiRunSegmentationSkipsControlOnlyText()
{
    const std::string text = "\xE2\x80\x8E\xE2\x80\x8F";
    const std::vector<wsc::text::BidiRun> runs = wsc::text::segmentBidiRuns(text);

    return expect(runs.empty(), "bidi control-only text should not produce visible runs");
}

bool testBidiRunSegmentationUsesDirectionalMarks()
{
    const std::string rlmText = "\xE2\x80\x8F" "123";
    const std::string lrmText = "\xE2\x80\x8E" "123";
    const std::string almText = "\xD8\x9C" "123";
    const std::vector<wsc::text::BidiRun> rlmRuns = wsc::text::segmentBidiRuns(rlmText);
    const std::vector<wsc::text::BidiRun> lrmRuns = wsc::text::segmentBidiRuns(lrmText);
    const std::vector<wsc::text::BidiRun> almRuns = wsc::text::segmentBidiRuns(almText);

    return expect(rlmRuns.size() == 1 && rlmRuns[0].rightToLeft,
                  "RLM should set weak-only text to RTL")
        && expect(rlmRuns[0].sourceStart == 3 && rlmRuns[0].sourceEnd == rlmText.size(),
                  "RLM should not be included in the visible source range")
        && expect(lrmRuns.size() == 1 && !lrmRuns[0].rightToLeft,
                  "LRM should set weak-only text to LTR")
        && expect(lrmRuns[0].sourceStart == 3 && lrmRuns[0].sourceEnd == lrmText.size(),
                  "LRM should not be included in the visible source range")
        && expect(almRuns.size() == 1 && almRuns[0].rightToLeft,
                  "ALM should set weak-only text to RTL")
        && expect(almRuns[0].sourceStart == 2 && almRuns[0].sourceEnd == almText.size(),
                  "ALM should not be included in the visible source range");
}

bool testColorFontTableDetection()
{
    const std::vector<std::uint8_t> sfnt =
        makeSfntWithTables({"COLR", "CPAL", "CBDT", "CBLC", "sbix", "SVG "});
    const wsc::text::ColorFontTables tables =
        wsc::text::detectColorFontTables({sfnt.data(), sfnt.size()});

    return expect(tables.hasAny(), "color table detection should report color font data")
        && expect(tables.colr && tables.cpal, "COLR/CPAL tables should be detected")
        && expect(tables.cbdt && tables.cblc, "CBDT/CBLC bitmap color tables should be detected")
        && expect(tables.sbix, "sbix table should be detected")
        && expect(tables.svg, "SVG table should be detected");
}

bool testColorFontTableDetectionHandlesTtcAndMalformedData()
{
    const std::vector<std::uint8_t> ttc = makeTtcWithFirstFontTables({"COLR", "CPAL"});
    const wsc::text::ColorFontTables ttcTables =
        wsc::text::detectColorFontTables({ttc.data(), ttc.size()});
    const std::vector<std::uint8_t> malformed = {0, 1, 2, 3, 4};
    const wsc::text::ColorFontTables malformedTables =
        wsc::text::detectColorFontTables({malformed.data(), malformed.size()});

    return expect(ttcTables.colr && ttcTables.cpal, "TTC first-font color tables should be detected")
        && expect(!malformedTables.hasAny(), "malformed font data should not report color tables");
}

} // namespace

int main()
{
    const bool ok = testDecodeValidUtf8()
        && testInvalidUtf8Replacement()
        && testAsciiFallbackKeepsShape()
        && testUnicodeBreakTokensSplitCjkText()
        && testUnicodeBreakTokensAttachClosingPunctuation()
        && testUnicodeBreakTokensTreatWhitespaceAsBreaks()
        && testSimpleShaperBuildsGlyphRun()
        && testSimpleShaperStopsAtFirstLineAndFailsMissingGlyphs()
        && testSimpleShaperOrdersRightToLeftRuns()
        && testSimpleShaperMirrorsRightToLeftPunctuation()
        && testSimpleShaperSkipsBidiControls()
        && testTextShapingEngineFactoryFallsBackToSimple()
        && testBidiRunSegmentation()
        && testBidiRunSegmentationKeepsLeadingNeutrals()
        && testBidiRunSegmentationKeepsWeakOnlyText()
        && testBidiRunSegmentationSkipsControlOnlyText()
        && testBidiRunSegmentationUsesDirectionalMarks()
        && testColorFontTableDetection()
        && testColorFontTableDetectionHandlesTtcAndMalformedData();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
