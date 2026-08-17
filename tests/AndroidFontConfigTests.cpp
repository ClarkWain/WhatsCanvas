#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "text/TextUtils.h"
#include "text/platform/AndroidFontConfig.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) return true;
    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

void writeU16BE(std::vector<unsigned char> &bytes, std::size_t offset,
                std::uint16_t value)
{
    bytes[offset] = static_cast<unsigned char>(value >> 8u);
    bytes[offset + 1u] = static_cast<unsigned char>(value & 0xFFu);
}

void writeU32BE(std::vector<unsigned char> &bytes, std::size_t offset,
                std::uint32_t value)
{
    bytes[offset] = static_cast<unsigned char>(value >> 24u);
    bytes[offset + 1u] = static_cast<unsigned char>((value >> 16u) & 0xFFu);
    bytes[offset + 2u] = static_cast<unsigned char>((value >> 8u) & 0xFFu);
    bytes[offset + 3u] = static_cast<unsigned char>(value & 0xFFu);
}

void writeTag(std::vector<unsigned char> &bytes, std::size_t offset,
              const char tag[4])
{
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<unsigned char>(tag[index]);
    }
}

std::vector<unsigned char> makeStyleSfnt(std::size_t sfntOffset,
                                         int weight,
                                         std::uint16_t selection)
{
    const std::size_t os2Offset = sfntOffset + 44u;
    const std::size_t headOffset = os2Offset + 64u;
    std::vector<unsigned char> bytes(headOffset + 54u, 0u);
    writeU32BE(bytes, sfntOffset, 0x00010000u);
    writeU16BE(bytes, sfntOffset + 4u, 2u);

    writeTag(bytes, sfntOffset + 12u, "OS/2");
    writeU32BE(bytes, sfntOffset + 20u,
               static_cast<std::uint32_t>(os2Offset));
    writeU32BE(bytes, sfntOffset + 24u, 64u);
    writeTag(bytes, sfntOffset + 28u, "head");
    writeU32BE(bytes, sfntOffset + 36u,
               static_cast<std::uint32_t>(headOffset));
    writeU32BE(bytes, sfntOffset + 40u, 54u);

    writeU16BE(bytes, os2Offset + 4u, static_cast<std::uint16_t>(weight));
    writeU16BE(bytes, os2Offset + 62u, selection);
    return bytes;
}

std::filesystem::path writeTemporaryFont(
    const std::string &name, const std::vector<unsigned char> &bytes)
{
    static std::atomic<unsigned long long> sequence{0};
    const auto tick = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path()
        / (name + "." + std::to_string(tick) + "."
           + std::to_string(sequence.fetch_add(1)));
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return output ? path : std::filesystem::path();
}

bool testModernConfigAndAliases()
{
    const std::string xml = R"xml(
        <?xml version="1.0" encoding="utf-8"?>
        <familyset version="23">
          <family name="sans-serif">
            <font weight="100" style="normal">Roboto-Thin.ttf</font>
            <font weight="400" style="normal">Roboto-Regular.ttf
              <axis tag="wdth" stylevalue="90.5"/>
              <axis tag="wght" stylevalue="425"/>
              <axis tag="wght" stylevalue="700"/>
            </font>
            <font weight="700" style="italic" index="2">Roboto-BoldItalic.ttc</font>
          </family>
          <family lang="zh-Hans">
            <font weight="400">NotoSansCJKsc-Regular.otf</font>
          </family>
          <family lang="zh-Hant">
            <font weight="400">NotoSansCJKtc-Regular.otf</font>
          </family>
          <family lang="und-Zsye">
            <font weight="400">NotoColorEmoji.ttf</font>
          </family>
          <alias name="sans-serif-thin" to="sans-serif" weight="100"/>
        </familyset>)xml";
    wsc::text::detail::AndroidFontConfig config;
    const bool parsed = wsc::text::detail::parseAndroidFontConfig(
        xml, "/system/fonts", config);
    const auto italic = wsc::text::detail::matchAndroidFontConfig(
        config, "sans-serif", 700, wsc::FontSlant::ITALIC, "en-US");
    const auto thin = wsc::text::detail::matchAndroidFontConfig(
        config, "sans-serif-thin", 400, wsc::FontSlant::NORMAL, "en-US");
    const auto traditional = wsc::text::detail::matchAndroidFontConfig(
        config, "sans-serif", 400, wsc::FontSlant::NORMAL, "zh-TW");
    const auto simplified = wsc::text::detail::matchAndroidFontConfig(
        config, "sans-serif", 400, wsc::FontSlant::NORMAL, "zh-CN");
    const auto emoji = wsc::text::detail::matchAndroidFontConfig(
        config, "sans-serif", 400, wsc::FontSlant::NORMAL, "", true, false);
    const auto firstFallbackPath = [](const auto &matches) {
        for (const auto *face : matches) {
            if (face != nullptr && face->families.empty()) return face->path;
        }
        return std::string();
    };

    return expect(parsed, "modern Android familyset should parse")
        && expect(config.faces.size() == 6, "all modern font records should be retained")
        && expect(config.aliases.size() == 1, "font aliases should be retained")
        && expect(config.faces[0].weightSpecified
                      && config.faces[0].slantSpecified
                      && config.faces[4].weightSpecified
                      && !config.faces[4].slantSpecified,
                  "parser should distinguish configured style from scan defaults")
        && expect(wsc::text::detail::hasAndroidFontConfigFamily(
                      config, "SANS-SERIF-THIN"),
                  "declared aliases should be discoverable as families")
        && expect(!italic.empty()
                      && italic.front()->path == "/system/fonts/Roboto-BoldItalic.ttc"
                      && italic.front()->faceIndex == 2
                      && italic.front()->slant == wsc::FontSlant::ITALIC,
                  "style matching should retain TTC index and italic metadata")
        && expect(!thin.empty()
                      && thin.front()->path == "/system/fonts/Roboto-Thin.ttf",
                  "weighted aliases should select their configured target weight")
        && expect(config.faces[1].variationAxes.size() == 2
                      && config.faces[1].variationAxes[0].tag == "wdth"
                      && config.faces[1].variationAxes[0].value == 90.5f
                      && config.faces[1].variationAxes[1].tag == "wght"
                      && config.faces[1].variationAxes[1].value == 425.0f,
                  "variable-font axes should be retained and duplicates ignored")
        && expect(firstFallbackPath(traditional)
                      == "/system/fonts/NotoSansCJKtc-Regular.otf",
                  "zh-TW should prefer the Hant fallback")
        && expect(firstFallbackPath(simplified)
                      == "/system/fonts/NotoSansCJKsc-Regular.otf",
                  "zh-CN should prefer the Hans fallback")
        && expect(firstFallbackPath(emoji)
                      == "/system/fonts/NotoColorEmoji.ttf",
                  "VS16 matching should prefer the emoji-presentation family");
}

bool testIntrinsicStyleScanningAndConfigPrecedence()
{
    const auto sfnt = makeStyleSfnt(0u, 650, 1u << 9u);
    const std::filesystem::path sfntPath = writeTemporaryFont(
        "whatscanvas-style-oracle.ttf", sfnt);

    auto ttc = makeStyleSfnt(16u, 725, 1u);
    writeTag(ttc, 0u, "ttcf");
    writeU32BE(ttc, 4u, 0x00010000u);
    writeU32BE(ttc, 8u, 1u);
    writeU32BE(ttc, 12u, 16u);
    const std::filesystem::path ttcPath = writeTemporaryFont(
        "whatscanvas-style-oracle.ttc", ttc);

    wsc::text::detail::AndroidFontConfigFace scanned;
    scanned.path = sfntPath.string();
    const bool scannedOk =
        wsc::text::detail::resolveAndroidFontConfigFaceStyle(scanned);

    wsc::text::detail::AndroidFontConfigFace explicitStyle;
    explicitStyle.path = sfntPath.string();
    explicitStyle.weight = 700;
    explicitStyle.slant = wsc::FontSlant::ITALIC;
    explicitStyle.weightSpecified = true;
    explicitStyle.slantSpecified = true;
    const bool explicitOk =
        wsc::text::detail::resolveAndroidFontConfigFaceStyle(explicitStyle);

    wsc::text::detail::AndroidFontConfigFace collection;
    collection.path = ttcPath.string();
    collection.faceIndex = 0;
    const bool collectionOk =
        wsc::text::detail::resolveAndroidFontConfigFaceStyle(collection);
    wsc::text::detail::AndroidFontConfigFace badCollection = collection;
    badCollection.faceIndex = 1;
    badCollection.weight = 400;
    badCollection.slant = wsc::FontSlant::NORMAL;
    const bool badCollectionOk =
        wsc::text::detail::resolveAndroidFontConfigFaceStyle(badCollection);

    std::error_code ignored;
    std::filesystem::remove(sfntPath, ignored);
    std::filesystem::remove(ttcPath, ignored);

    return expect(!sfntPath.empty() && !ttcPath.empty(),
                  "temporary style fixtures should be writable")
        && expect(scannedOk && scanned.weight == 650
                      && scanned.slant == wsc::FontSlant::OBLIQUE,
                  "OS/2 weight and oblique metadata should fill omitted XML style")
        && expect(explicitOk && explicitStyle.weight == 700
                      && explicitStyle.slant == wsc::FontSlant::ITALIC,
                  "explicit Android XML style must override intrinsic metadata")
        && expect(collectionOk && collection.weight == 725
                      && collection.slant == wsc::FontSlant::ITALIC,
                  "TTC face offsets should resolve intrinsic style")
        && expect(!badCollectionOk,
                  "out-of-range TTC face indices should fail without mutation");
}

bool testWeightedAliasDoesNotApproximateMissingWeight()
{
    const std::string xml = R"xml(
        <familyset>
          <family name="sans-serif">
            <font weight="400">Regular.ttf</font>
            <font weight="700">Bold.ttf</font>
          </family>
          <alias name="sans-serif-medium" to="sans-serif" weight="500"/>
        </familyset>)xml";
    wsc::text::detail::AndroidFontConfig config;
    const bool parsed = wsc::text::detail::parseAndroidFontConfig(
        xml, "/system/fonts", config);
    const auto matches = wsc::text::detail::matchAndroidFontConfig(
        config, "sans-serif-medium", 400, wsc::FontSlant::NORMAL, "");
    return expect(parsed, "weighted-alias fixture should parse")
        && expect(matches.empty(),
                  "Skia weighted aliases contain exact target weights only");
}

bool testCssStylePreferenceOrder()
{
    const std::string xml = R"xml(
        <familyset>
          <family name="css-order">
            <font weight="400">Regular.ttf</font>
            <font weight="500">Medium.ttf</font>
            <font weight="600">Semibold.ttf</font>
          </family>
        </familyset>)xml";
    wsc::text::detail::AndroidFontConfig config;
    const bool parsed = wsc::text::detail::parseAndroidFontConfig(
        xml, "/system/fonts", config);
    const auto weight450 = wsc::text::detail::matchAndroidFontConfig(
        config, "css-order", 450, wsc::FontSlant::NORMAL, "");

    const std::string missing500Xml = R"xml(
        <familyset>
          <family name="css-missing-medium">
            <font weight="400">Regular.ttf</font>
            <font weight="600">Semibold.ttf</font>
          </family>
        </familyset>)xml";
    wsc::text::detail::AndroidFontConfig missing500;
    const bool missingParsed = wsc::text::detail::parseAndroidFontConfig(
        missing500Xml, "/system/fonts", missing500);
    const auto weight500 = wsc::text::detail::matchAndroidFontConfig(
        missing500, "css-missing-medium", 500,
        wsc::FontSlant::NORMAL, "");

    return expect(parsed && missingParsed,
                  "CSS style-order fixtures should parse")
        && expect(!weight450.empty()
                      && weight450.front()->path
                          == "/system/fonts/Medium.ttf",
                  "450 should search upward through 500 before 400")
        && expect(!weight500.empty()
                      && weight500.front()->path
                          == "/system/fonts/Regular.ttf",
                  "missing 500 should prefer 400 before 600");
}

bool testDefaultVariantPreferenceMatchesSkia()
{
    const std::string xml = R"xml(
        <familyset>
          <family lang="ja" variant="compact">
            <font weight="400">CompactJapanese.ttf</font>
          </family>
          <family variant="elegant">
            <font weight="400">Elegant.ttf</font>
          </family>
          <family>
            <font weight="400">DefaultVariant.ttf</font>
          </family>
        </familyset>)xml";
    wsc::text::detail::AndroidFontConfig config;
    const bool parsed = wsc::text::detail::parseAndroidFontConfig(
        xml, "/system/fonts", config);
    const auto matches = wsc::text::detail::matchAndroidFontConfig(
        config, "sans-serif", 400, wsc::FontSlant::NORMAL, "ja");
    return expect(parsed, "variant fixture should parse")
        && expect(matches.size() == 3,
                  "all global variant families should remain candidates")
        && expect(matches.front()->path == "/system/fonts/Elegant.ttf",
                  "Skia default fallback should run elegant before compact")
        && expect(matches[1]->path == "/system/fonts/DefaultVariant.ttf",
                  "default variant should participate in the elegant pass")
        && expect(matches[2]->path == "/system/fonts/CompactJapanese.ttf",
                  "compact fallback should run after elegant/default even with exact locale");
}

bool testFallbackForIsolationMatchesSkia()
{
    const std::string xml = R"xml(
        <familyset>
          <family name="sans-serif">
            <font weight="400">Sans.ttf</font>
            <font weight="400" fallbackFor="serif">SerifOnlyFallback.ttf</font>
          </family>
          <family lang="ja">
            <font weight="400">GeneralFallback.ttf</font>
          </family>
        </familyset>)xml";
    wsc::text::detail::AndroidFontConfig config;
    const bool parsed = wsc::text::detail::parseAndroidFontConfig(
        xml, "/system/fonts", config);
    const auto sans = wsc::text::detail::matchAndroidFontConfig(
        config, "sans-serif", 400, wsc::FontSlant::NORMAL, "");
    const auto serif = wsc::text::detail::matchAndroidFontConfig(
        config, "serif", 400, wsc::FontSlant::NORMAL, "ja");
    const auto containsPath = [](const auto &matches, const std::string &path) {
        for (const auto *face : matches) {
            if (face != nullptr && face->path == path) return true;
        }
        return false;
    };

    return expect(parsed, "fallbackFor fixture should parse")
        && expect(config.faces[1].families.empty(),
                  "fallbackFor fonts should be split from their parent family")
        && expect(!containsPath(sans, "/system/fonts/SerifOnlyFallback.ttf"),
                  "target-specific fallback must not leak into another family")
        && expect(containsPath(serif, "/system/fonts/SerifOnlyFallback.ttf"),
                  "target-specific fallback should be available to its target")
        && expect(!serif.empty()
                      && serif.front()->path
                          == "/system/fonts/SerifOnlyFallback.ttf",
                  "target-specific fallback should precede a locale-matched global fallback")
        && expect(containsPath(sans, "/system/fonts/GeneralFallback.ttf")
                      && containsPath(serif, "/system/fonts/GeneralFallback.ttf"),
                  "unnamed general fallbacks should remain globally available");
}

bool testLegacyNamesetFilesetAndMerge()
{
    const std::string legacy = R"xml(
        <familyset>
          <family>
            <nameset><name>Droid Sans</name><name>sans-serif</name></nameset>
            <fileset>
              <file lang="ja" variant="elegant">DroidSans.ttf</file>
              <file style="italic">DroidSans-Italic.ttf</file>
            </fileset>
          </family>
        </familyset>)xml";
    const std::string product = R"xml(
        <fonts-modification version="1">
          <family customizationType="new-named-family" name="oem-sans">
            <font weight="500">OemSans-Medium.ttf</font>
          </family>
        </fonts-modification>)xml";
    wsc::text::detail::AndroidFontConfig config;
    const bool legacyParsed = wsc::text::detail::parseAndroidFontConfig(
        legacy, "/system/fonts/", config);
    const bool productParsed = wsc::text::detail::parseAndroidFontConfig(
        product, "/product/fonts", config);
    const auto legacyMatches = wsc::text::detail::matchAndroidFontConfig(
        config, "DROID   SANS", 400, wsc::FontSlant::ITALIC, "");
    const auto productMatches = wsc::text::detail::matchAndroidFontConfig(
        config, "oem-sans", 500, wsc::FontSlant::NORMAL, "");

    return expect(legacyParsed && productParsed,
                  "legacy and product configs should merge")
        && expect(config.faces.size() == 3,
                  "merged config should preserve all faces")
        && expect(!legacyMatches.empty()
                      && legacyMatches.front()->path
                          == "/system/fonts/DroidSans-Italic.ttf",
                  "legacy nameset/fileset should preserve family and slant")
        && expect(config.faces[0].locales.size() == 1
                      && config.faces[0].locales[0] == "ja"
                      && config.faces[0].variant
                          == wsc::text::detail::AndroidFontVariant::Elegant,
                  "legacy file language and variant metadata should be retained")
        && expect(!productMatches.empty()
                      && productMatches.front()->path
                          == "/product/fonts/OemSans-Medium.ttf",
                  "product customization should resolve against its font directory");
}

bool testMalformedConfigIsRejected()
{
    wsc::text::detail::AndroidFontConfig config;
    return expect(!wsc::text::detail::parseAndroidFontConfig(
                      "<familyset><family name='sans-serif'", "/system/fonts", config),
                  "unterminated XML tags should be rejected");
}

std::string readCorpusFixture(const std::string &name)
{
    const std::filesystem::path path =
        std::filesystem::u8path(WHATSCANVAS_ANDROID_FONT_CONFIG_FIXTURES) / name;
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

bool testCrossVersionAndVendorCorpus()
{
    struct CorpusCase
    {
        const char *name;
        const char *directory;
        std::size_t faceCount;
        std::size_t aliasCount;
    };
    const CorpusCase cases[] = {
        {"api21_legacy.xml", "/system/fonts", 3, 0},
        {"api21_family_list.xml", "/system/fonts", 2, 0},
        {"api21_alias_case.xml", "/system/fonts", 1, 1},
        {"api23_fonts.xml", "/system/fonts", 3, 1},
        {"api28_fallback.xml", "/system/fonts", 2, 0},
        {"api29_locales.xml", "/system/fonts", 2, 0},
        {"api33_presentation.xml", "/system/fonts", 2, 0},
        {"api35_variable.xml", "/system/fonts", 1, 0},
        {"vendor_product.xml", "/product/fonts", 2, 2},
        {"aosp_api33_complete.xml", "/system/fonts", 367, 24},
        {"xiaomi_miui12_api30_complete.xml", "/system/fonts", 360, 24},
    };
    bool ok = true;
    for (const CorpusCase &entry : cases) {
        const std::string xml = readCorpusFixture(entry.name);
        wsc::text::detail::AndroidFontConfig parsed;
        ok = expect(!xml.empty(), std::string(entry.name) + " should be readable") && ok;
        ok = expect(wsc::text::detail::parseAndroidFontConfig(
                        xml, entry.directory, parsed),
                    std::string(entry.name) + " should parse") && ok;
        ok = expect(parsed.faces.size() == entry.faceCount,
                    std::string(entry.name) + " should retain the expected faces") && ok;
        ok = expect(parsed.aliases.size() == entry.aliasCount,
                    std::string(entry.name) + " should retain only valid aliases") && ok;
    }

    wsc::text::detail::AndroidFontConfig legacy;
    (void)wsc::text::detail::parseAndroidFontConfig(
        readCorpusFixture("api21_legacy.xml"), "/system/fonts", legacy);
    ok = expect(legacy.faces.size() == 3
                    && legacy.faces[1].faceIndex == 1
                    && legacy.faces[1].locales == std::vector<std::string>{"ja"}
                    && legacy.faces[1].variant
                        == wsc::text::detail::AndroidFontVariant::Compact,
                "API 21 legacy file metadata should survive nameset/fileset parsing") && ok;

    wsc::text::detail::AndroidFontConfig familyList;
    (void)wsc::text::detail::parseAndroidFontConfig(
        readCorpusFixture("api21_family_list.xml"), "/system/fonts", familyList);
    ok = expect(familyList.faces.size() == 2
                    && familyList.faces[0].families
                        == std::vector<std::string>{"ui-grouped"}
                    && familyList.faces[1].families
                        == std::vector<std::string>{"ui-grouped"},
                "API 21 family-list should name every nested family") && ok;

    wsc::text::detail::AndroidFontConfig aliasCase;
    (void)wsc::text::detail::parseAndroidFontConfig(
        readCorpusFixture("api21_alias_case.xml"), "/system/fonts", aliasCase);
    const auto aliasMatches = wsc::text::detail::matchAndroidFontConfig(
        aliasCase, "DISPLAY-BLACK", 400, wsc::FontSlant::NORMAL, "");
    ok = expect(aliasMatches.size() == 1
                    && aliasMatches.front()->weight == 900,
                "alias name and target matching should be case insensitive") && ok;

    wsc::text::detail::AndroidFontConfig api28;
    (void)wsc::text::detail::parseAndroidFontConfig(
        readCorpusFixture("api28_fallback.xml"), "/system/fonts", api28);
    ok = expect(api28.faces.size() == 2
                    && api28.faces[0].variationAxes.size() == 1
                    && api28.faces[0].variationAxes[0].tag == "wdth"
                    && api28.faces[1].families.empty()
                    && api28.faces[1].fallbackFor == "sans-serif",
                "API 28 duplicate/invalid axes and fallbackFor isolation should be stable") && ok;

    wsc::text::detail::AndroidFontConfig api29;
    (void)wsc::text::detail::parseAndroidFontConfig(
        readCorpusFixture("api29_locales.xml"), "/system/fonts", api29);
    const auto traditional = wsc::text::detail::matchAndroidFontConfig(
        api29, "missing-family", 400, wsc::FontSlant::NORMAL, "zh-TW");
    const auto simplified = wsc::text::detail::matchAndroidFontConfig(
        api29, "missing-family", 400, wsc::FontSlant::NORMAL, "zh-CN");
    ok = expect(!traditional.empty()
                    && traditional.front()->path.find("CJKtc") != std::string::npos
                    && !simplified.empty()
                    && simplified.front()->path.find("CJKsc") != std::string::npos,
                "locale parent/script ranking should distinguish zh-TW and zh-CN") && ok;

    wsc::text::detail::AndroidFontConfig api33;
    (void)wsc::text::detail::parseAndroidFontConfig(
        readCorpusFixture("api33_presentation.xml"), "/system/fonts", api33);
    const auto emoji = wsc::text::detail::matchAndroidFontConfig(
        api33, "missing-family", 400, wsc::FontSlant::NORMAL, "", true, false);
    const auto text = wsc::text::detail::matchAndroidFontConfig(
        api33, "missing-family", 400, wsc::FontSlant::NORMAL, "", false, true);
    const auto matchCluster = [&](std::vector<std::uint32_t> cluster) {
        const auto presentation =
            wsc::text::classifyEmojiPresentation(cluster);
        return wsc::text::detail::matchAndroidFontConfig(
            api33, "missing-family", 400, wsc::FontSlant::NORMAL, "",
            presentation == wsc::text::EmojiPresentation::Emoji,
            presentation == wsc::text::EmojiPresentation::Text);
    };
    const auto defaultEmoji = matchCluster({0x1F600});
    const auto zwjEmoji = matchCluster({0x1F469, 0x200D, 0x1F4BB});
    const auto keycapEmoji = matchCluster({'8', 0x20E3});
    ok = expect(!emoji.empty() && emoji.front()->path.find("ColorEmoji") != std::string::npos
                    && !text.empty() && text.front()->path.find("Symbols") != std::string::npos,
                "API 33 emoji/text presentation should reorder global fallbacks") && ok;
    ok = expect(!defaultEmoji.empty()
                    && defaultEmoji.front()->path.find("ColorEmoji") != std::string::npos
                    && !zwjEmoji.empty()
                    && zwjEmoji.front()->path.find("ColorEmoji") != std::string::npos
                    && !keycapEmoji.empty()
                    && keycapEmoji.front()->path.find("ColorEmoji") != std::string::npos,
                "default emoji and structural sequences should prefer the emoji family") && ok;

    wsc::text::detail::AndroidFontConfig api35;
    (void)wsc::text::detail::parseAndroidFontConfig(
        readCorpusFixture("api35_variable.xml"), "/system/fonts", api35);
    ok = expect(api35.faces.size() == 1
                    && api35.faces[0].faceIndex == 3
                    && api35.faces[0].slant == wsc::FontSlant::OBLIQUE
                    && api35.faces[0].variationAxes.size() == 2,
                "API 35 TTC and variable axes should remain part of face identity") && ok;

    wsc::text::detail::AndroidFontConfig vendor;
    (void)wsc::text::detail::parseAndroidFontConfig(
        readCorpusFixture("vendor_product.xml"), "/product/fonts", vendor);
    const auto missingTarget = wsc::text::detail::matchAndroidFontConfig(
        vendor, "missing-target", 500, wsc::FontSlant::NORMAL, "");
    ok = expect(vendor.faces.size() == 2 && vendor.aliases.size() == 2
                    && missingTarget.size() == 1
                    && missingTarget.front()->families.empty(),
                "vendor empty entries should be skipped and missing aliases should only see global fallback") && ok;

    wsc::text::detail::AndroidFontConfig aosp;
    (void)wsc::text::detail::parseAndroidFontConfig(
        readCorpusFixture("aosp_api33_complete.xml"), "/system/fonts", aosp);
    const auto aospEmoji = wsc::text::detail::matchAndroidFontConfig(
        aosp, "missing-family", 400, wsc::FontSlant::NORMAL, "", true, false);
    ok = expect(!aospEmoji.empty()
                    && aospEmoji.front()->path.find("NotoColorEmojiLegacy.ttf")
                        != std::string::npos,
                "complete AOSP API 33 config should preserve emoji presentation order") && ok;

    wsc::text::detail::AndroidFontConfig miui;
    (void)wsc::text::detail::parseAndroidFontConfig(
        readCorpusFixture("xiaomi_miui12_api30_complete.xml"),
        "/system/fonts", miui);
    const auto miuiPro = wsc::text::detail::matchAndroidFontConfig(
        miui, "mipro", 400, wsc::FontSlant::NORMAL, "");
    ok = expect(!miuiPro.empty()
                    && miuiPro.front()->path.find("MiLanProVF.ttf")
                        != std::string::npos
                    && miuiPro.front()->variationAxes.size() == 1
                    && miuiPro.front()->variationAxes.front().tag == "wght"
                    && miuiPro.front()->variationAxes.front().value == 340.0f,
                "complete MIUI config should preserve OEM family and variable instance") && ok;
    return ok;
}

} // namespace

int main()
{
    const bool ok = testModernConfigAndAliases()
        && testIntrinsicStyleScanningAndConfigPrecedence()
        && testWeightedAliasDoesNotApproximateMissingWeight()
        && testCssStylePreferenceOrder()
        && testDefaultVariantPreferenceMatchesSkia()
        && testFallbackForIsolationMatchesSkia()
        && testLegacyNamesetFilesetAndMerge()
        && testMalformedConfigIsRejected()
        && testCrossVersionAndVendorCorpus();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
