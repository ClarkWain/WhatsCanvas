#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "wsc/Font.h"

namespace wsc::text::detail {

enum class AndroidFontVariant
{
    Default,
    Compact,
    Elegant,
};

struct AndroidFontVariationAxis
{
    std::string tag;
    float value = 0.0f;
};

struct AndroidFontConfigFace
{
    std::vector<std::string> families;
    std::vector<std::string> locales;
    std::vector<AndroidFontVariationAxis> variationAxes;
    std::string fallbackFor;
    std::string path;
    int faceIndex = 0;
    int weight = 400;
    FontSlant slant = FontSlant::NORMAL;
    bool weightSpecified = false;
    bool slantSpecified = false;
    AndroidFontVariant variant = AndroidFontVariant::Default;
    std::size_t order = 0;
};

struct AndroidFontConfigAlias
{
    std::string name;
    std::string target;
    int weight = 0;
};

struct AndroidFontConfig
{
    std::vector<AndroidFontConfigFace> faces;
    std::vector<AndroidFontConfigAlias> aliases;
};

/// Parse modern Android fonts.xml (including family-list),
/// fonts-modification, and legacy system_fonts.xml/fallback_fonts.xml
/// nameset/fileset schemas. Parsed records are appended so system, vendor, and
/// product configurations can be merged.
bool parseAndroidFontConfig(const std::string &xml,
                            const std::string &fontDirectory,
                            AndroidFontConfig &config);

/// True when a named family or alias is declared by the merged config.
bool hasAndroidFontConfigFamily(const AndroidFontConfig &config,
                                const std::string &family);

/// Fill style fields omitted by Android XML from the font's OS/2/head/post
/// tables. Explicit configuration always wins. Supports TTF/OTF and TTC/OTC.
bool resolveAndroidFontConfigFaceStyle(AndroidFontConfigFace &face);

/// Return candidates in named-family, locale, style, and system-config order.
/// Definitive cluster coverage remains the rasterizer's responsibility.
std::vector<const AndroidFontConfigFace *> matchAndroidFontConfig(
    const AndroidFontConfig &config, const std::string &family, int weight,
    FontSlant slant, const std::string &locale,
    bool preferEmojiPresentation = false,
    bool preferTextPresentation = false);

} // namespace wsc::text::detail
