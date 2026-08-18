#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_android.h"
#include "include/ports/SkFontScanner_FreeType.h"

namespace {

std::string jsonString(const std::string &value)
{
    std::ostringstream output;
    output << '"';
    for (unsigned char ch : value) {
        if (ch == '"') output << "\\\"";
        else if (ch == '\\') output << "\\\\";
        else if (ch == '\n') output << "\\n";
        else if (ch == '\r') output << "\\r";
        else if (ch == '\t') output << "\\t";
        else output << static_cast<char>(ch);
    }
    output << '"';
    return output.str();
}

const char *slantName(SkFontStyle::Slant slant)
{
    if (slant == SkFontStyle::kItalic_Slant) return "italic";
    if (slant == SkFontStyle::kOblique_Slant) return "oblique";
    return "normal";
}

void appendTypeface(std::ostringstream &output, const sk_sp<SkTypeface> &typeface,
                    SkUnichar character)
{
    output << "{\"found\":" << (typeface ? "true" : "false");
    if (typeface) {
        SkString family;
        typeface->getFamilyName(&family);
        const SkFontStyle style = typeface->fontStyle();
        output << ",\"family\":" << jsonString(family.c_str())
               << ",\"weight\":" << style.weight()
               << ",\"width\":" << style.width()
               << ",\"slant\":" << jsonString(slantName(style.slant()))
               << ",\"glyph\":" << typeface->unicharToGlyph(character);
    }
    output << '}';
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::cerr << "Usage: SkiaAndroidFontManagerProbe <fonts.xml> <font-dir>\n";
        return EXIT_FAILURE;
    }
    std::string basePath = argv[2];
    if (!basePath.empty() && basePath.back() != '/' && basePath.back() != '\\') {
        basePath.push_back('/');
    }
    const SkFontMgr_Android_CustomFonts custom{
        SkFontMgr_Android_CustomFonts::kOnlyCustom,
        basePath.c_str(),
        argv[1],
        nullptr,
        true,
    };
    sk_sp<SkFontMgr> manager = SkFontMgr_New_Android(
        &custom, SkFontScanner_Make_FreeType());
    if (!manager) return EXIT_FAILURE;

    const SkFontStyle requested(700, SkFontStyle::kNormal_Width,
                                SkFontStyle::kItalic_Slant);
    const sk_sp<SkTypeface> styleMatch =
        manager->matchFamilyStyle("oracle-variable", requested);
    const char *english[] = {"en"};
    const char *simplifiedChinese[] = {"zh-Hans"};
    const char *japanese[] = {"ja"};
    const char *emojiPresentation[] = {"und-Zsye"};
    const sk_sp<SkTypeface> latinMatch = manager->matchFamilyStyleCharacter(
        "oracle-variable", requested, english, 1, 0x0041);
    const sk_sp<SkTypeface> simplifiedCjkMatch = manager->matchFamilyStyleCharacter(
        "oracle-variable", requested, simplifiedChinese, 1, 0x4c2e);
    const sk_sp<SkTypeface> japaneseCjkMatch = manager->matchFamilyStyleCharacter(
        "oracle-variable", requested, japanese, 1, 0x4c2e);
    const sk_sp<SkTypeface> colrEmojiMatch = manager->matchFamilyStyleCharacter(
        "oracle-variable", requested, emojiPresentation, 1, 0x3297);
    const sk_sp<SkTypeface> bitmapEmojiMatch = manager->matchFamilyStyleCharacter(
        "oracle-variable", requested, emojiPresentation, 1, 0x2049);
    const sk_sp<SkTypeface> missingMatch = manager->matchFamilyStyleCharacter(
        "oracle-variable", requested, simplifiedChinese, 1, 0x4e2d);

    SkString configuredFamily;
    if (manager->countFamilies() > 0) manager->getFamilyName(0, &configuredFamily);
    std::ostringstream output;
    output << "{\"schema\":\"whatscanvas.skia-android-font-manager.v1\","
              "\"engine\":\"skia-android-freetype\",\"familyCount\":"
           << manager->countFamilies()
           << ",\"configuredFamily\":" << jsonString(configuredFamily.c_str())
           << ",\"styleMatch\":";
    appendTypeface(output, styleMatch, 0x0041);
    output << ",\"latinCharacterMatch\":";
    appendTypeface(output, latinMatch, 0x0041);
    output << ",\"simplifiedCjkMatch\":";
    appendTypeface(output, simplifiedCjkMatch, 0x4c2e);
    output << ",\"japaneseCjkMatch\":";
    appendTypeface(output, japaneseCjkMatch, 0x4c2e);
    output << ",\"colrEmojiPresentationMatch\":";
    appendTypeface(output, colrEmojiMatch, 0x3297);
    output << ",\"bitmapEmojiPresentationMatch\":";
    appendTypeface(output, bitmapEmojiMatch, 0x2049);
    output << ",\"missingCharacterMatch\":";
    appendTypeface(output, missingMatch, 0x4e2d);
    output << "}\n";
    std::cout << output.str();
    return EXIT_SUCCESS;
}
