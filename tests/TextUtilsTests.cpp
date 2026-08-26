#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <fstream>
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

std::vector<std::uint8_t> readFileBytes(const std::string &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
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

std::vector<std::uint8_t> makeTtcWithFontTables(const std::vector<std::vector<std::string>> &fontTags)
{
    std::vector<std::uint8_t> bytes;
    appendU32BE(bytes, 0x74746366u);
    appendU32BE(bytes, 0x00010000u);
    appendU32BE(bytes, static_cast<std::uint32_t>(fontTags.size()));

    std::vector<std::vector<std::uint8_t>> fonts;
    fonts.reserve(fontTags.size());
    std::uint32_t offset = static_cast<std::uint32_t>(12u + fontTags.size() * 4u);
    for (const auto &tags : fontTags) {
        std::vector<std::uint8_t> sfnt = makeSfntWithTables(tags);
        appendU32BE(bytes, offset);
        offset += static_cast<std::uint32_t>(sfnt.size());
        fonts.push_back(std::move(sfnt));
    }
    for (const auto &font : fonts) {
        bytes.insert(bytes.end(), font.begin(), font.end());
    }
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

bool testAsciiFallbackSkipsZeroWidthBreak()
{
    const std::string text = "A\xE2\x80\x8B" "B";
    return expect(wsc::text::makeAsciiFallbackText(text) == "AB",
                  "ASCII fallback should skip zero-width break without replacement");
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

bool testUnicodeBreakTokensAttachOpeningPunctuation()
{
    const std::string text = "\xe4\xbd\xa0\xef\xbc\x88\xe5\xa5\xbd";
    const std::vector<wsc::text::TextBreakToken> tokens =
        wsc::text::buildTextBreakTokens(text, 0, text.size());

    return expect(tokens.size() == 2, "opening CJK punctuation should attach to the next token")
        && expect(tokens[0].sourceStart == 0 && tokens[0].sourceEnd == 3,
                  "previous CJK character should remain independently breakable")
        && expect(tokens[1].sourceStart == 3 && tokens[1].sourceEnd == text.size(),
                  "opening punctuation and following CJK character should share a source span");
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

bool testUnicodeBreakTokensPreserveNoBreakSpaces()
{
    const std::string figureSpace = "12\xE2\x80\x87" "34";
    const std::string narrowNoBreakSpace = "12\xE2\x80\xAF" "34";
    const std::vector<wsc::text::TextBreakToken> figureTokens =
        wsc::text::buildTextBreakTokens(figureSpace, 0, figureSpace.size());
    const std::vector<wsc::text::TextBreakToken> narrowTokens =
        wsc::text::buildTextBreakTokens(narrowNoBreakSpace, 0, narrowNoBreakSpace.size());

    return expect(figureTokens.size() == 1, "figure space should not split break tokens")
        && expect(figureTokens[0].sourceStart == 0 && figureTokens[0].sourceEnd == figureSpace.size(),
                  "figure space should remain inside the token source span")
        && expect(narrowTokens.size() == 1, "narrow no-break space should not split break tokens")
        && expect(narrowTokens[0].sourceStart == 0 && narrowTokens[0].sourceEnd == narrowNoBreakSpace.size(),
                  "narrow no-break space should remain inside the token source span");
}

bool testUnicodeBreakTokensStopAtCarriageReturn()
{
    const std::string text = "alpha\rbeta";
    const std::vector<wsc::text::TextBreakToken> tokens =
        wsc::text::buildTextBreakTokens(text, 0, text.size());

    return expect(tokens.size() == 1, "carriage return should stop the current break-token row")
        && expect(tokens[0].sourceStart == 0 && tokens[0].sourceEnd == 5,
                  "carriage return should not be included in break-token source spans");
}

bool testUnicodeBreakTokensSplitZeroWidthSpace()
{
    const std::string text = "alpha\xE2\x80\x8B" "beta";
    const std::vector<wsc::text::TextBreakToken> tokens =
        wsc::text::buildTextBreakTokens(text, 0, text.size());

    return expect(tokens.size() == 2, "zero-width space should split break tokens")
        && expect(tokens[0].sourceStart == 0 && tokens[0].sourceEnd == 5,
                  "zero-width break should keep first source span visible")
        && expect(tokens[1].sourceStart == 8 && tokens[1].sourceEnd == text.size(),
                  "zero-width break should skip its UTF-8 bytes in the next source span")
        && expect(!tokens[1].prefixSpace, "zero-width break should not request a visible prefix space");
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

bool testSimpleShaperStopsAtCarriageReturn()
{
    const auto run = wsc::text::shapeTextSimple("AB\rC",
                                                1.0f,
                                                [](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
        return wsc::text::ResolvedGlyph{static_cast<int>(codepoint), 5.0f};
    });

    return expect(run.has_value(), "simple shaper should shape text before carriage return")
        && expect(run->glyphs.size() == 2, "simple shaper should stop at carriage return")
        && expect(run->glyphs[0].codepoint == 'A' && run->glyphs[1].codepoint == 'B',
                  "simple shaper should not render text after carriage return");
}

bool testSimpleShaperSkipsVariationSelectors()
{
    int resolverCalls = 0;
    const auto run = wsc::text::shapeTextSimple(
        "\xF0\x9F\x98\x80\xEF\xB8\x8F", 0.0f,
        [&](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
            ++resolverCalls;
            return codepoint == 0x1F600u
                ? std::optional<wsc::text::ResolvedGlyph>(
                    wsc::text::ResolvedGlyph{77, 18.0f})
                : std::nullopt;
        });
    return expect(run.has_value() && run->glyphs.size() == 1,
                  "simple shaping should keep one visible emoji glyph")
        && expect(resolverCalls == 1,
                  "variation selectors should not require standalone glyphs")
        && expect(run->glyphs.front().codepoint == 0x1F600u,
                  "the base emoji should retain the shaped glyph identity");
}

bool testSimpleShaperDirectionStopsAtLineBreak()
{
    const auto lfRun = wsc::text::shapeTextSimple("123\n\xd7\x90",
                                                  0.0f,
                                                  [](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
        return wsc::text::ResolvedGlyph{static_cast<int>(codepoint), 5.0f};
    });
    const auto crRun = wsc::text::shapeTextSimple("123\r\xd7\x90",
                                                  0.0f,
                                                  [](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
        return wsc::text::ResolvedGlyph{static_cast<int>(codepoint), 5.0f};
    });

    return expect(lfRun.has_value() && !lfRun->rightToLeft,
                  "simple shaper direction should ignore strong text after LF")
        && expect(crRun.has_value() && !crRun->rightToLeft,
                  "simple shaper direction should ignore strong text after CR")
        && expect(lfRun->glyphs.size() == 3 && crRun->glyphs.size() == 3,
                  "line-break direction test should only shape first-line glyphs");
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

bool testSimpleShaperSkipsZeroWidthBreak()
{
    const std::string text = "A\xE2\x80\x8B" "B";
    const auto run = wsc::text::shapeTextSimple(text,
                                                1.0f,
                                                [](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
        return wsc::text::ResolvedGlyph{static_cast<int>(codepoint), 5.0f};
    });

    return expect(run.has_value(), "simple shaper should shape around zero-width break")
        && expect(run->glyphs.size() == 2, "zero-width break should not emit a glyph")
        && expect(run->glyphs[0].codepoint == 'A' && run->glyphs[1].codepoint == 'B',
                  "visible glyph order should skip zero-width break")
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

bool testOpenTypeShaperHonorsCollectionFaceIndex()
{
    if (!wsc::text::isOpenTypeShapingAvailable()) {
        return true;
    }

    const std::vector<std::uint8_t> bytes = readFileBytes(WHATSCANVAS_TEST_TTC_FONT);
    if (!expect(!bytes.empty(), "TTC shaping fixture should load")) {
        return false;
    }

    const auto resolver = [](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
        return wsc::text::ResolvedGlyph{static_cast<int>(codepoint), 10.0f};
    };
    auto shaper = wsc::text::createOpenTypeTextShapingEngine();
    wsc::text::TextShapeInput input;
    input.normalizedText = ".";
    input.pixelSize = 16.0f;
    input.fontData = wsc::text::FontDataView{bytes.data(), bytes.size(), 1};
    const auto secondFace = shaper->shape(input, resolver);

    input.fontData->faceIndex = 2;
    const auto outOfRangeFace = shaper->shape(input, resolver);
    return expect(secondFace.has_value(), "TTC face index 1 should shape successfully")
        && expect(!outOfRangeFace.has_value(),
                  "out-of-range TTC face index must not silently shape face 0");
}

bool testOpenTypeShaperProducesRealLigatures()
{
    if (!wsc::text::isOpenTypeShapingAvailable()) {
        return true;
    }

    const std::vector<std::uint8_t> bytes = readFileBytes(WHATSCANVAS_TEST_OPENTYPE_FONT);
    if (!expect(!bytes.empty(), "OpenType shaping fixture should load")) {
        return false;
    }

    const auto resolver = [](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
        return wsc::text::ResolvedGlyph{static_cast<int>(codepoint), 10.0f};
    };
    auto shaper = wsc::text::createOpenTypeTextShapingEngine();
    wsc::text::TextShapeInput input;
    input.normalizedText = "ffi";
    input.pixelSize = 20.0f;
    input.language = "en";
    input.fontData = wsc::text::FontDataView{bytes.data(), bytes.size(), 0};
    const auto shaped = shaper->shape(input, resolver);

    return expect(shaped.has_value(), "HarfBuzz should shape the ligature fixture")
        && expect(shaped->glyphs.size() < 3,
                  "default OpenType features should combine the ffi ligature");
}

bool testOpenTypeFeaturesCanDisableLigatures()
{
    if (!wsc::text::isOpenTypeShapingAvailable()) {
        return true;
    }

    const std::vector<std::uint8_t> bytes = readFileBytes(WHATSCANVAS_TEST_OPENTYPE_FONT);
    const auto resolver = [](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
        return wsc::text::ResolvedGlyph{static_cast<int>(codepoint), 10.0f};
    };
    auto shaper = wsc::text::createOpenTypeTextShapingEngine();
    wsc::text::TextShapeInput input;
    input.normalizedText = "ffi";
    input.pixelSize = 20.0f;
    input.fontData = wsc::text::FontDataView{bytes.data(), bytes.size(), 0};
    input.openTypeFeatures.push_back({"liga", 0});
    const auto shaped = shaper->shape(input, resolver);

    return expect(shaped.has_value(), "HarfBuzz should shape with explicit features")
        && expect(shaped->glyphs.size() == 3,
                  "disabling liga should keep the three source glyphs separate");
}

bool testAndroidClusterEncodingPreservesCompleteUtf16Runs()
{
    const std::vector<std::uint16_t> zwj =
        wsc::text::encodeCodepointsToUtf16({0x1F469, 0x200D, 0x1F4BB});
    const std::vector<std::uint16_t> flag =
        wsc::text::encodeCodepointsToUtf16({0x1F1E8, 0x1F1F3});
    const std::vector<std::uint16_t> invalid =
        wsc::text::encodeCodepointsToUtf16({0xD800, 0x110000});

    return expect(zwj == std::vector<std::uint16_t>{
                      0xD83D, 0xDC69, 0x200D, 0xD83D, 0xDCBB},
                  "Android matching should receive the complete UTF-16 ZWJ cluster")
        && expect(flag == std::vector<std::uint16_t>{
                      0xD83C, 0xDDE8, 0xD83C, 0xDDF3},
                  "regional indicators should retain both surrogate pairs")
        && expect(invalid == std::vector<std::uint16_t>{0xFFFD, 0xFFFD},
                  "invalid scalar values should become UTF-16 replacement characters");
}

bool testEmojiPresentationClassification()
{
    using Presentation = wsc::text::EmojiPresentation;
    const auto classify = [](std::initializer_list<std::uint32_t> codepoints) {
        return wsc::text::classifyEmojiPresentation(
            std::vector<std::uint32_t>(codepoints));
    };

    return expect(classify({'A'}) == Presentation::Default,
                  "ordinary text should not request an emoji family")
        && expect(classify({0x00A9}) == Presentation::Default,
                  "text-default emoji candidates should stay neutral without VS16")
        && expect(classify({0x00A9, 0xFE0F}) == Presentation::Emoji,
                  "VS16 should explicitly request emoji presentation")
        && expect(classify({0x1F600, 0xFE0E}) == Presentation::Text,
                  "VS15 should override a default emoji presentation")
        && expect(classify({0x1F600}) == Presentation::Emoji,
                  "Emoji_Presentation characters should prefer emoji fonts")
        && expect(classify({0x1F469, 0x1F3FD}) == Presentation::Emoji,
                  "skin-tone sequences should prefer emoji fonts")
        && expect(classify({0x1F469, 0x200D, 0x1F4BB}) == Presentation::Emoji,
                  "ZWJ pictographic sequences should prefer emoji fonts")
        && expect(classify({0x1F1E8, 0x1F1F3}) == Presentation::Emoji,
                  "regional-indicator flags should prefer emoji fonts")
        && expect(classify({'8', 0x20E3}) == Presentation::Emoji,
                  "keycap sequences should prefer emoji fonts")
        && expect(classify({0x1F3F4, 0xE0067, 0xE0062, 0xE007F})
                      == Presentation::Emoji,
                  "emoji tag sequences should prefer emoji fonts");
}

bool testFontFallbackClustersPreserveGraphemeSequences()
{
    const std::string text = "A\xCC\x81" "B "
        "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB "
        "\xF0\x9F\x87\xA8\xF0\x9F\x87\xB3";
    const auto clusters = wsc::text::buildFontFallbackClusters(text, 0, text.size());

    bool sawCombiningCluster = false;
    bool sawZwjCluster = false;
    bool sawFlagCluster = false;
    for (const auto &cluster : clusters) {
        const std::string value = text.substr(cluster.sourceStart,
                                              cluster.sourceEnd - cluster.sourceStart);
        sawCombiningCluster = sawCombiningCluster || value == "A\xCC\x81";
        sawZwjCluster = sawZwjCluster
            || value == "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB";
        sawFlagCluster = sawFlagCluster
            || value == "\xF0\x9F\x87\xA8\xF0\x9F\x87\xB3";
    }

    return expect(sawCombiningCluster, "base character and combining mark should share one fallback cluster")
        && expect(sawZwjCluster, "emoji ZWJ sequence should share one fallback cluster")
        && expect(sawFlagCluster, "regional-indicator pair should share one fallback cluster");
}

bool testFontFallbackClustersFollowExtendedGraphemeRules()
{
    const std::vector<std::string> indivisible = {
        "\xE0\xA4\x95\xE0\xA4\xBE",             // Devanagari base + SpacingMark (GB9a)
        "\xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8", // Hangul L + V + T (GB6-GB8)
        "\xD8\x80" "A",                           // Prepend + base (GB9b)
        "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\x95", // Indic consonant + linker + consonant (GB9c)
        "\xF0\x9F\x91\xA9\xEF\xB8\x8F\xE2\x80\x8D\xF0\x9F\x92\xBB" // GB11
    };

    bool ok = true;
    for (const std::string &text : indivisible) {
        const auto clusters = wsc::text::buildFontFallbackClusters(text, 0, text.size());
        ok = expect(clusters.size() == 1
                        && clusters.front().sourceStart == 0
                        && clusters.front().sourceEnd == text.size(),
                    "each extended grapheme sequence should remain one cluster") && ok;
    }

    const std::string regionalTriplet =
        "\xF0\x9F\x87\xA6\xF0\x9F\x87\xA7\xF0\x9F\x87\xA8";
    const auto regionalClusters =
        wsc::text::buildFontFallbackClusters(regionalTriplet, 0, regionalTriplet.size());
    ok = expect(regionalClusters.size() == 2
                    && regionalClusters[0].sourceEnd == 8
                    && regionalClusters[1].sourceStart == 8,
                "regional indicators should pair from the start of the sequence") && ok;

    const std::string controls("A\0B", 3);
    const auto controlClusters =
        wsc::text::buildFontFallbackClusters(controls, 0, controls.size());
    ok = expect(controlClusters.size() == 3,
                "controls should force grapheme boundaries on both sides") && ok;
    return ok;
}

bool testBreakTokensPreserveCjkVariationSequence()
{
    const std::string text = "\xE4\xB8\x80\xEF\xB8\x8F\xE4\xBA\x8C";
    const auto tokens = wsc::text::buildTextBreakTokens(text, 0, text.size());
    return expect(tokens.size() == 2, "two CJK grapheme clusters should produce two tokens")
        && expect(tokens[0].sourceStart == 0 && tokens[0].sourceEnd == 6,
                  "CJK variation selector must remain attached to its base token")
        && expect(tokens[1].sourceStart == 6 && tokens[1].sourceEnd == text.size(),
                  "the following CJK cluster should start after the variation sequence");
}

bool testBidiRunSegmentation()
{
    const std::string mixed = "abc \xd7\x90\xd7\x91 def";
    const std::vector<wsc::text::BidiRun> runs = wsc::text::segmentBidiRuns(mixed);

    bool ok = expect(runs.size() == 3, "mixed LTR/RTL text should split into three bidi runs");
    ok = expect(!runs[0].rightToLeft && runs[0].sourceStart == 0 && runs[0].sourceEnd == 4,
                "first bidi run should be LTR and include trailing neutral space") && ok;
    ok = expect(runs[1].rightToLeft && runs[1].sourceStart == 4 && runs[1].sourceEnd == 8,
                "second bidi run should be RTL and contain the Hebrew span") && ok;
    ok = expect(!runs[2].rightToLeft && runs[2].sourceStart == 8 && runs[2].sourceEnd == mixed.size(),
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

bool testBidiRunSegmentationStopsAtCarriageReturn()
{
    const std::string text = "abc\r\xd7\x90\xd7\x91";
    const std::vector<wsc::text::BidiRun> runs = wsc::text::segmentBidiRuns(text);

    return expect(runs.size() == 1, "bidi segmentation should stop at carriage return")
        && expect(!runs[0].rightToLeft, "pre-carriage-return run should stay LTR")
        && expect(runs[0].sourceStart == 0 && runs[0].sourceEnd == 3,
                  "bidi segmentation should exclude carriage return and following text");
}

bool testBidiRunSegmentationUsesDirectionalMarks()
{
    const std::string rlmText = "\xE2\x80\x8F" "123";
    const std::string lrmText = "\xE2\x80\x8E" "123";
    const std::string almText = "\xD8\x9C" "123";
    const std::vector<wsc::text::BidiRun> rlmRuns = wsc::text::segmentBidiRuns(rlmText);
    const std::vector<wsc::text::BidiRun> lrmRuns = wsc::text::segmentBidiRuns(lrmText);
    const std::vector<wsc::text::BidiRun> almRuns = wsc::text::segmentBidiRuns(almText);

    return expect(rlmRuns.size() == 1 && !rlmRuns[0].rightToLeft,
                  "RLM should set RTL paragraph context while European digits remain an LTR number run")
        && expect(rlmRuns[0].sourceStart == 3 && rlmRuns[0].sourceEnd == rlmText.size(),
                  "RLM should not be included in the visible source range")
        && expect(lrmRuns.size() == 1 && !lrmRuns[0].rightToLeft,
                  "LRM should set weak-only text to LTR")
        && expect(lrmRuns[0].sourceStart == 3 && lrmRuns[0].sourceEnd == lrmText.size(),
                  "LRM should not be included in the visible source range")
        && expect(almRuns.size() == 1 && !almRuns[0].rightToLeft,
                  "ALM should set RTL paragraph context while European digits remain an LTR number run")
        && expect(almRuns[0].sourceStart == 2 && almRuns[0].sourceEnd == almText.size(),
                  "ALM should not be included in the visible source range");
}

bool testBidiRunSegmentationUsesExplicitControls()
{
    const std::string rleText = "\xE2\x80\xAB" "123" "\xE2\x80\xAC";
    const std::string lreText = "\xE2\x80\xAA" "123" "\xE2\x80\xAC";
    const std::string rliText = "\xE2\x81\xA7" "123" "\xE2\x81\xA9";
    const std::string fsiText = "\xE2\x81\xA8" "\xD7\x90" "12" "\xE2\x81\xA9";
    const std::string restoredText = "a " "\xE2\x80\xAB" "123" "\xE2\x80\xAC" " b";

    const std::vector<wsc::text::BidiRun> rleRuns = wsc::text::segmentBidiRuns(rleText);
    const std::vector<wsc::text::BidiRun> lreRuns = wsc::text::segmentBidiRuns(lreText);
    const std::vector<wsc::text::BidiRun> rliRuns = wsc::text::segmentBidiRuns(rliText);
    const std::vector<wsc::text::BidiRun> fsiRuns = wsc::text::segmentBidiRuns(fsiText);
    const std::vector<wsc::text::BidiRun> restoredRuns = wsc::text::segmentBidiRuns(restoredText);

    return expect(rleRuns.size() == 1 && !rleRuns[0].rightToLeft,
                  "RLE should embed European digits as an LTR number run inside RTL context")
        && expect(rleRuns[0].sourceStart == 3 && rleRuns[0].sourceEnd == 6,
                  "RLE/PDF controls should stay outside visible source ranges")
        && expect(lreRuns.size() == 1 && !lreRuns[0].rightToLeft,
                  "LRE should keep weak-only text LTR")
        && expect(lreRuns[0].sourceStart == 3 && lreRuns[0].sourceEnd == 6,
                  "LRE/PDF controls should stay outside visible source ranges")
        && expect(rliRuns.size() == 1 && !rliRuns[0].rightToLeft,
                  "RLI should isolate European digits as an LTR number run inside RTL context")
        && expect(rliRuns[0].sourceStart == 3 && rliRuns[0].sourceEnd == 6,
                  "RLI/PDI controls should stay outside visible source ranges")
        && expect(fsiRuns.size() == 2 && !fsiRuns[0].rightToLeft && fsiRuns[1].rightToLeft,
                  "FSI should infer RTL isolate direction and return visual level-run order")
        && expect(fsiRuns[0].sourceStart == 5 && fsiRuns[0].sourceEnd == 7
                      && fsiRuns[1].sourceStart == 3 && fsiRuns[1].sourceEnd == 5,
                  "FSI/PDI controls should stay outside visible source ranges")
        && expect(restoredRuns.size() == 3, "PDF should restore the previous direction")
        && expect(!restoredRuns[0].rightToLeft && restoredRuns[0].sourceStart == 0 && restoredRuns[0].sourceEnd == 2,
                  "text before RLE should remain LTR")
        && expect(!restoredRuns[1].rightToLeft && restoredRuns[1].sourceStart == 5 && restoredRuns[1].sourceEnd == 8,
                  "European digits inside RLE should remain an LTR number run")
        && expect(!restoredRuns[2].rightToLeft && restoredRuns[2].sourceStart == 11
                      && restoredRuns[2].sourceEnd == restoredText.size(),
                  "text after PDF should restore LTR");
}

bool testUnicodeBidiResolvesWeakAndNeutralTypes()
{
    const std::string arabicNumber = "\xD8\xA7" " 12,34";
    const std::string mixedNeutral = "abc \xD7\x90\xD7\x91 def";
    const std::vector<wsc::text::BidiRun> arabicRuns = wsc::text::segmentBidiRuns(arabicNumber);
    const std::vector<wsc::text::BidiRun> neutralRuns = wsc::text::segmentBidiRuns(mixedNeutral);

    bool ok = expect(!arabicRuns.empty(), "Arabic-number bidi text should produce runs");
    ok = expect(arabicRuns.size() >= 2,
                "Arabic paragraph with European digits should split strong text and number runs") && ok;
    ok = expect(arabicRuns.front().sourceStart >= 3 || !arabicRuns.front().rightToLeft,
                "Arabic paragraph should preserve a renderable number run") && ok;
    ok = expect(neutralRuns.size() == 3,
                "mixed strong text with neutral separator should split into three resolved runs") && ok;
    ok = expect(!neutralRuns[2].rightToLeft && neutralRuns[2].sourceStart == 8,
                "neutral space between RTL and LTR should resolve with the following LTR run") && ok;
    return ok;
}

bool testUnicodeBidiOverrideControls()
{
    const std::string rloText = "\xE2\x80\xAE" "abc" "\xE2\x80\xAC";
    const std::string lroText = "\xE2\x80\xAD" "\xD7\x90\xD7\x91" "\xE2\x80\xAC";
    const std::vector<wsc::text::BidiRun> rloRuns = wsc::text::segmentBidiRuns(rloText);
    const std::vector<wsc::text::BidiRun> lroRuns = wsc::text::segmentBidiRuns(lroText);

    return expect(rloRuns.size() == 1 && rloRuns[0].rightToLeft,
                  "RLO should force enclosed LTR letters into an RTL run")
        && expect(rloRuns[0].sourceStart == 3 && rloRuns[0].sourceEnd == 6,
                  "RLO/PDF controls should stay outside the visible source range")
        && expect(lroRuns.size() == 1 && !lroRuns[0].rightToLeft,
                  "LRO should force enclosed RTL letters into an LTR run")
        && expect(lroRuns[0].sourceStart == 3 && lroRuns[0].sourceEnd == 7,
                  "LRO/PDF controls should stay outside the visible source range");
}

bool testColorFontTableDetection()
{
    const std::vector<std::uint8_t> sfnt =
        makeSfntWithTables({"COLR", "CPAL", "CBDT", "CBLC", "sbix", "SVG "});
    const wsc::text::ColorFontTables tables =
        wsc::text::detectColorFontTables({sfnt.data(), sfnt.size()});
    const wsc::text::ColorFontTables outOfRangeTables =
        wsc::text::detectColorFontTables({sfnt.data(), sfnt.size()}, 1);

    return expect(tables.hasAny(), "color table detection should report color font data")
        && expect(tables.colr && tables.cpal, "COLR/CPAL tables should be detected")
        && expect(tables.cbdt && tables.cblc, "CBDT/CBLC bitmap color tables should be detected")
        && expect(tables.sbix, "sbix table should be detected")
        && expect(tables.svg, "SVG table should be detected")
        && expect(!outOfRangeTables.hasAny(), "non-collection face index should not alias face 0");
}

bool testColorFontTableDetectionHandlesTtcAndMalformedData()
{
    const std::vector<std::uint8_t> ttc = makeTtcWithFirstFontTables({"COLR", "CPAL"});
    const wsc::text::ColorFontTables ttcTables =
        wsc::text::detectColorFontTables({ttc.data(), ttc.size()});
    const std::vector<std::uint8_t> indexedTtc = makeTtcWithFontTables({{"CBDT"}, {"COLR", "CPAL"}});
    const wsc::text::ColorFontTables firstTables =
        wsc::text::detectColorFontTables({indexedTtc.data(), indexedTtc.size()}, 0);
    const wsc::text::ColorFontTables secondTables =
        wsc::text::detectColorFontTables({indexedTtc.data(), indexedTtc.size()}, 1);
    const wsc::text::ColorFontTables outOfRangeTables =
        wsc::text::detectColorFontTables({indexedTtc.data(), indexedTtc.size()}, 2);
    const std::vector<std::uint8_t> malformed = {0, 1, 2, 3, 4};
    const wsc::text::ColorFontTables malformedTables =
        wsc::text::detectColorFontTables({malformed.data(), malformed.size()});

    return expect(ttcTables.colr && ttcTables.cpal, "TTC first-font color tables should be detected")
        && expect(firstTables.cbdt && !firstTables.colr, "TTC face index 0 should read the first font tables")
        && expect(secondTables.colr && secondTables.cpal && !secondTables.cbdt,
                  "TTC face index 1 should read the second font tables")
        && expect(!outOfRangeTables.hasAny(), "out-of-range TTC face index should not report color tables")
        && expect(!malformedTables.hasAny(), "malformed font data should not report color tables");
}

bool testGeometryTextVerticesRespectOrigin()
{
    // The pen origin must be a device-space translation, not scaled with the
    // glyph coordinates. Regression guard for the ASCII fallback that used to
    // bake (x, y) into stb_easy_font output and then multiply it by the scale.
    const float originX = 100.0f;
    const float originY = 200.0f;
    const float scale = 5.0f;
    const std::vector<float> vertices =
        wsc::text::buildTextVertices("A", originX, originY, scale, 0.0f);
    if (!expect(!vertices.empty(), "geometry text should emit vertices")) {
        return false;
    }

    float minX = vertices[0];
    float minY = vertices[1];
    for (std::size_t i = 0; i + 1 < vertices.size(); i += 2) {
        minX = std::min(minX, vertices[i]);
        minY = std::min(minY, vertices[i + 1]);
    }

    // The glyph's top-left starts at the origin (plus a small scaled inset),
    // never at origin * scale (which would land near 500, 1000 here).
    return expect(minX >= originX - 1.0f && minX <= originX + 8.0f * scale,
                  "glyph x should sit at the pen origin, not the scaled origin")
        && expect(minY >= originY - 1.0f && minY <= originY + 12.0f * scale,
                  "glyph y should sit at the pen origin, not the scaled origin");
}

bool testTextBaselineOffsetsKeepDistinctSemantics()
{
    constexpr float height = 20.0f;
    constexpr float alphabetic = 15.0f;
    return expect(
               wsc::text::textBaselineOffset(
                   wsc::Paint::TextBaseline::TOP, height, alphabetic) == 0.0f,
               "top baseline should anchor the text bounds top")
        && expect(
               wsc::text::textBaselineOffset(
                   wsc::Paint::TextBaseline::MIDDLE, height, alphabetic) == -10.0f,
               "middle baseline should anchor the text bounds centre")
        && expect(
               wsc::text::textBaselineOffset(
                   wsc::Paint::TextBaseline::BOTTOM, height, alphabetic) == -20.0f,
               "bottom baseline should anchor the text bounds bottom")
        && expect(
               wsc::text::textBaselineOffset(
                   wsc::Paint::TextBaseline::ALPHABETIC, height, alphabetic) == -15.0f,
               "alphabetic baseline should use the backend typographic baseline");
}

} // namespace

int main()
{
    const bool ok = testDecodeValidUtf8()
        && testInvalidUtf8Replacement()
        && testAsciiFallbackKeepsShape()
        && testAsciiFallbackSkipsZeroWidthBreak()
        && testUnicodeBreakTokensSplitCjkText()
        && testUnicodeBreakTokensAttachClosingPunctuation()
        && testUnicodeBreakTokensAttachOpeningPunctuation()
        && testUnicodeBreakTokensTreatWhitespaceAsBreaks()
        && testUnicodeBreakTokensPreserveNoBreakSpaces()
        && testUnicodeBreakTokensStopAtCarriageReturn()
        && testUnicodeBreakTokensSplitZeroWidthSpace()
        && testSimpleShaperBuildsGlyphRun()
        && testSimpleShaperStopsAtFirstLineAndFailsMissingGlyphs()
        && testSimpleShaperStopsAtCarriageReturn()
        && testSimpleShaperSkipsVariationSelectors()
        && testSimpleShaperDirectionStopsAtLineBreak()
        && testSimpleShaperOrdersRightToLeftRuns()
        && testSimpleShaperMirrorsRightToLeftPunctuation()
        && testSimpleShaperSkipsBidiControls()
        && testSimpleShaperSkipsZeroWidthBreak()
        && testTextShapingEngineFactoryFallsBackToSimple()
        && testOpenTypeShaperHonorsCollectionFaceIndex()
        && testOpenTypeShaperProducesRealLigatures()
        && testOpenTypeFeaturesCanDisableLigatures()
        && testAndroidClusterEncodingPreservesCompleteUtf16Runs()
        && testEmojiPresentationClassification()
        && testFontFallbackClustersPreserveGraphemeSequences()
        && testFontFallbackClustersFollowExtendedGraphemeRules()
        && testBreakTokensPreserveCjkVariationSequence()
        && testBidiRunSegmentation()
        && testBidiRunSegmentationKeepsLeadingNeutrals()
        && testBidiRunSegmentationKeepsWeakOnlyText()
        && testBidiRunSegmentationSkipsControlOnlyText()
        && testBidiRunSegmentationStopsAtCarriageReturn()
        && testBidiRunSegmentationUsesDirectionalMarks()
        && testBidiRunSegmentationUsesExplicitControls()
        && testUnicodeBidiResolvesWeakAndNeutralTypes()
        && testUnicodeBidiOverrideControls()
        && testColorFontTableDetection()
        && testColorFontTableDetectionHandlesTtcAndMalformedData()
        && testGeometryTextVerticesRespectOrigin()
        && testTextBaselineOffsetsKeepDistinctSemantics();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
