#include "text/platform/AndroidFontConfig.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace wsc::text::detail {
namespace {

using Attributes = std::unordered_map<std::string, std::string>;

struct FamilyContext
{
    std::vector<std::string> families;
    std::vector<std::string> locales;
    std::vector<AndroidFontConfigFace> faces;
    AndroidFontVariant variant = AndroidFontVariant::Default;
};

struct TextCapture
{
    std::string tag;
    Attributes attributes;
    std::string text;
    std::vector<AndroidFontVariationAxis> variationAxes;
};

std::string trim(std::string value)
{
    const auto whitespace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), whitespace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(),
                value.end());
    return value;
}

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a')
                                      : static_cast<char>(ch);
    });
    return value;
}

std::string decodeXml(std::string value)
{
    const std::pair<const char *, const char *> entities[] = {
        {"&amp;", "&"}, {"&quot;", "\""}, {"&apos;", "'"},
        {"&lt;", "<"}, {"&gt;", ">"}
    };
    for (const auto &entity : entities) {
        std::size_t offset = 0;
        while ((offset = value.find(entity.first, offset)) != std::string::npos) {
            value.replace(offset, std::char_traits<char>::length(entity.first),
                          entity.second);
            offset += std::char_traits<char>::length(entity.second);
        }
    }
    return value;
}

std::vector<std::string> splitTags(const std::string &value)
{
    std::vector<std::string> result;
    std::string current;
    for (unsigned char ch : value) {
        if (ch == ',' || std::isspace(ch) != 0) {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(static_cast<char>(ch));
        }
    }
    if (!current.empty()) result.push_back(std::move(current));
    return result;
}

int parseInt(const std::string &value, int fallback, int minimum, int maximum)
{
    if (value.empty()) return fallback;
    char *end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') return fallback;
    return static_cast<int>(std::clamp<long>(parsed, minimum, maximum));
}

std::optional<int> parseOptionalInt(const std::string &value,
                                    int minimum, int maximum)
{
    if (value.empty()) return std::nullopt;
    char *end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0'
        || parsed < minimum || parsed > maximum) {
        return std::nullopt;
    }
    return static_cast<int>(parsed);
}

std::optional<float> parseFloat(const std::string &value)
{
    if (value.empty()) return std::nullopt;
    char *end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return std::nullopt;
    }
    return parsed;
}

AndroidFontVariant parseVariant(const std::string &value)
{
    const std::string normalized = lowerAscii(trim(value));
    if (normalized == "compact") return AndroidFontVariant::Compact;
    if (normalized == "elegant") return AndroidFontVariant::Elegant;
    return AndroidFontVariant::Default;
}

std::size_t findTagEnd(const std::string &xml, std::size_t start)
{
    char quote = '\0';
    for (std::size_t index = start; index < xml.size(); ++index) {
        const char ch = xml[index];
        if (quote != '\0') {
            if (ch == quote) quote = '\0';
        } else if (ch == '\'' || ch == '\"') {
            quote = ch;
        } else if (ch == '>') {
            return index;
        }
    }
    return std::string::npos;
}

void parseTag(const std::string &raw, std::string &name, Attributes &attributes,
              bool &closing, bool &selfClosing)
{
    std::string content = trim(raw);
    closing = !content.empty() && content.front() == '/';
    if (closing) content = trim(content.substr(1));
    selfClosing = !content.empty() && content.back() == '/';
    if (selfClosing) content = trim(content.substr(0, content.size() - 1));

    std::size_t offset = 0;
    while (offset < content.size()
           && std::isspace(static_cast<unsigned char>(content[offset])) == 0) {
        ++offset;
    }
    name = lowerAscii(content.substr(0, offset));
    attributes.clear();
    while (offset < content.size()) {
        while (offset < content.size()
               && std::isspace(static_cast<unsigned char>(content[offset])) != 0) {
            ++offset;
        }
        const std::size_t keyStart = offset;
        while (offset < content.size() && content[offset] != '='
               && std::isspace(static_cast<unsigned char>(content[offset])) == 0) {
            ++offset;
        }
        std::string key = lowerAscii(content.substr(keyStart, offset - keyStart));
        while (offset < content.size()
               && std::isspace(static_cast<unsigned char>(content[offset])) != 0) {
            ++offset;
        }
        if (key.empty() || offset >= content.size() || content[offset] != '=') {
            while (offset < content.size()
                   && std::isspace(static_cast<unsigned char>(content[offset])) == 0) {
                ++offset;
            }
            continue;
        }
        ++offset;
        while (offset < content.size()
               && std::isspace(static_cast<unsigned char>(content[offset])) != 0) {
            ++offset;
        }
        if (offset >= content.size()) break;
        const char quote = content[offset];
        std::string value;
        if (quote == '\'' || quote == '\"') {
            const std::size_t valueStart = ++offset;
            while (offset < content.size() && content[offset] != quote) ++offset;
            value = content.substr(valueStart, offset - valueStart);
            if (offset < content.size()) ++offset;
        } else {
            const std::size_t valueStart = offset;
            while (offset < content.size()
                   && std::isspace(static_cast<unsigned char>(content[offset])) == 0) {
                ++offset;
            }
            value = content.substr(valueStart, offset - valueStart);
        }
        attributes.insert_or_assign(std::move(key), decodeXml(std::move(value)));
    }
}

std::string attribute(const Attributes &attributes, const std::string &name)
{
    const auto found = attributes.find(name);
    return found == attributes.end() ? std::string() : found->second;
}

std::string resolveFontPath(const std::string &fontDirectory,
                            const std::string &filename)
{
    const std::string clean = trim(decodeXml(filename));
    if (clean.empty()) return {};
    if (clean.front() == '/') return clean;
    if (fontDirectory.empty()) return clean;
    return fontDirectory.back() == '/'
        ? fontDirectory + clean : fontDirectory + '/' + clean;
}

void finishFace(FamilyContext &family, TextCapture &capture,
                const std::string &fontDirectory)
{
    AndroidFontConfigFace face;
    face.fallbackFor = attribute(capture.attributes, "fallbackfor");
    // A fallbackFor record is target-specific and must remain isolated from
    // its named parent. Otherwise it could win normal family matching before
    // the fallback constraint is considered.
    if (face.fallbackFor.empty()) face.families = family.families;
    const std::vector<std::string> faceLocales =
        splitTags(attribute(capture.attributes, "lang"));
    face.locales = faceLocales.empty() ? family.locales : faceLocales;
    const std::string variant = attribute(capture.attributes, "variant");
    face.variant = variant.empty() ? family.variant : parseVariant(variant);
    face.variationAxes = std::move(capture.variationAxes);
    face.path = resolveFontPath(fontDirectory, capture.text);
    face.faceIndex = parseInt(attribute(capture.attributes, "index"), 0, 0,
                             std::numeric_limits<int>::max());
    const std::optional<int> configuredWeight = parseOptionalInt(
        attribute(capture.attributes, "weight"), 1, 1000);
    face.weightSpecified = configuredWeight.has_value();
    face.weight = configuredWeight.value_or(400);
    const std::string style = lowerAscii(attribute(capture.attributes, "style"));
    face.slantSpecified = style == "normal" || style == "italic"
        || style == "oblique";
    face.slant = style == "italic" || style == "oblique"
        ? (style == "oblique" ? FontSlant::OBLIQUE : FontSlant::ITALIC)
        : FontSlant::NORMAL;
    if (!face.path.empty()) family.faces.push_back(std::move(face));
}

void finishFamily(FamilyContext &family, AndroidFontConfig &config)
{
    for (AndroidFontConfigFace &face : family.faces) {
        // Legacy namesets may be parsed after a file record. Preserve
        // file-level language/variant metadata and fallbackFor isolation.
        if (face.fallbackFor.empty()) face.families = family.families;
        if (face.locales.empty()) face.locales = family.locales;
        if (face.variant == AndroidFontVariant::Default) {
            face.variant = family.variant;
        }
        face.order = config.faces.size();
        config.faces.push_back(std::move(face));
    }
    family = {};
}

struct AliasResolution
{
    std::string family;
    int weight = 0;
};

AliasResolution resolveAlias(const AndroidFontConfig &config,
                             const std::string &family)
{
    AliasResolution result{family, 0};
    for (int depth = 0; depth < 16; ++depth) {
        const std::string key = canonicalFontFamilyName(result.family);
        const auto alias = std::find_if(config.aliases.begin(), config.aliases.end(),
                                        [&](const AndroidFontConfigAlias &candidate) {
            return canonicalFontFamilyName(candidate.name) == key;
        });
        if (alias == config.aliases.end() || alias->target.empty()) break;
        if (canonicalFontFamilyName(alias->target) == key) break;
        if (result.weight == 0 && alias->weight != 0) result.weight = alias->weight;
        result.family = alias->target;
    }
    return result;
}

bool familyContains(const AndroidFontConfigFace &face, const std::string &family)
{
    const std::string key = canonicalFontFamilyName(family);
    return std::any_of(face.families.begin(), face.families.end(),
                       [&](const std::string &candidate) {
        return canonicalFontFamilyName(candidate) == key;
    });
}

bool aliasSelectsFace(const AliasResolution &alias,
                      const AndroidFontConfigFace &face)
{
    return familyContains(face, alias.family)
        && (alias.weight == 0 || face.weight == alias.weight);
}

int slantMatchRank(FontSlant actual, FontSlant requested)
{
    if (actual == requested) return 0;
    if (actual != FontSlant::NORMAL && requested != FontSlant::NORMAL) return 1;
    return 2;
}

std::pair<int, int> weightMatchRank(int actual, int requested)
{
    actual = std::clamp(actual, 1, 1000);
    requested = std::clamp(requested, 1, 1000);
    if (requested >= 400 && requested <= 500) {
        if (actual >= requested && actual <= 500) return {0, actual - requested};
        if (actual < requested) return {1, requested - actual};
        return {2, actual - 500};
    }
    if (requested < 400) {
        return actual <= requested
            ? std::pair<int, int>{0, requested - actual}
            : std::pair<int, int>{1, actual - requested};
    }
    return actual >= requested
        ? std::pair<int, int>{0, actual - requested}
        : std::pair<int, int>{1, requested - actual};
}

int localeRank(const AndroidFontConfigFace &face, const std::string &locale)
{
    if (face.locales.empty()) return 3;
    if (locale.empty()) return 3;
    std::string requested = lowerAscii(locale);
    std::replace(requested.begin(), requested.end(), '_', '-');
    const std::size_t separator = requested.find_first_of("-_");
    const std::string language = requested.substr(0, separator);
    std::string preferredScript;
    if (language == "zh") {
        if (requested.find("-tw") != std::string::npos
            || requested.find("-hk") != std::string::npos
            || requested.find("-mo") != std::string::npos
            || requested.find("-hant") != std::string::npos) {
            preferredScript = "hant";
        } else if (requested.find("-cn") != std::string::npos
                   || requested.find("-sg") != std::string::npos
                   || requested.find("-hans") != std::string::npos) {
            preferredScript = "hans";
        }
    }
    int best = 4;
    for (const std::string &candidateValue : face.locales) {
        std::string candidate = lowerAscii(candidateValue);
        std::replace(candidate.begin(), candidate.end(), '_', '-');
        if (candidate == requested) return 0;
        const std::size_t candidateSeparator = candidate.find_first_of("-_");
        if (candidate.substr(0, candidateSeparator) != language) continue;
        if (!preferredScript.empty()
            && candidate.find('-' + preferredScript) != std::string::npos) {
            best = std::min(best, 1);
        } else {
            best = std::min(best, 2);
        }
    }
    return best;
}

bool isEmojiPresentationFace(const AndroidFontConfigFace &face)
{
    return std::any_of(face.locales.begin(), face.locales.end(),
                       [](const std::string &locale) {
        const std::string key = lowerAscii(locale);
        return key == "und-zsye" || key.find("-zsye") != std::string::npos;
    });
}

int defaultVariantRank(const AndroidFontConfigFace &face)
{
    // Android's default character fallback policy prefers elegant before
    // compact. A default family participates in both passes.
    return face.variant == AndroidFontVariant::Compact ? 1 : 0;
}

std::uint16_t readU16BE(const unsigned char *bytes)
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[0]) << 8u)
        | static_cast<std::uint16_t>(bytes[1]));
}

std::uint32_t readU32BE(const unsigned char *bytes)
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24u)
        | (static_cast<std::uint32_t>(bytes[1]) << 16u)
        | (static_cast<std::uint32_t>(bytes[2]) << 8u)
        | static_cast<std::uint32_t>(bytes[3]);
}

class SfntReader
{
public:
    explicit SfntReader(const std::string &path)
        : stream_(std::filesystem::u8path(path), std::ios::binary)
    {
        if (!stream_) return;
        stream_.seekg(0, std::ios::end);
        const std::streamoff length = stream_.tellg();
        if (length <= 0) return;
        size_ = static_cast<std::uint64_t>(length);
    }

    bool valid() const { return stream_.is_open() && size_ > 0; }
    std::uint64_t size() const { return size_; }

    bool read(std::uint64_t offset, unsigned char *output,
              std::size_t length)
    {
        if (!valid() || output == nullptr || offset > size_
            || length > size_ - offset
            || offset > static_cast<std::uint64_t>(
                std::numeric_limits<std::streamoff>::max())) {
            return false;
        }
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!stream_) return false;
        stream_.read(reinterpret_cast<char *>(output),
                     static_cast<std::streamsize>(length));
        return stream_.good()
            || stream_.gcount() == static_cast<std::streamsize>(length);
    }

private:
    std::ifstream stream_;
    std::uint64_t size_ = 0;
};

struct SfntTableRecord
{
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
};

bool tagEquals(const unsigned char *value, const char tag[4])
{
    return value[0] == static_cast<unsigned char>(tag[0])
        && value[1] == static_cast<unsigned char>(tag[1])
        && value[2] == static_cast<unsigned char>(tag[2])
        && value[3] == static_cast<unsigned char>(tag[3]);
}

bool validSfntSignature(const unsigned char *value)
{
    return readU32BE(value) == 0x00010000u
        || tagEquals(value, "OTTO")
        || tagEquals(value, "true")
        || tagEquals(value, "typ1");
}

std::optional<std::uint64_t> sfntOffsetForFace(SfntReader &reader,
                                               int faceIndex)
{
    if (faceIndex < 0) return std::nullopt;
    unsigned char header[12] = {};
    if (!reader.read(0, header, sizeof(header))) return std::nullopt;
    if (!tagEquals(header, "ttcf")) {
        return faceIndex == 0 && validSfntSignature(header)
            ? std::optional<std::uint64_t>(0u) : std::nullopt;
    }

    const std::uint32_t faceCount = readU32BE(header + 8u);
    if (faceCount == 0u || static_cast<std::uint32_t>(faceIndex) >= faceCount) {
        return std::nullopt;
    }
    const std::uint64_t recordOffset = 12u
        + static_cast<std::uint64_t>(faceIndex) * 4u;
    unsigned char offsetBytes[4] = {};
    if (!reader.read(recordOffset, offsetBytes, sizeof(offsetBytes))) {
        return std::nullopt;
    }
    const std::uint64_t offset = readU32BE(offsetBytes);
    unsigned char signature[4] = {};
    if (!reader.read(offset, signature, sizeof(signature))
        || !validSfntSignature(signature)) {
        return std::nullopt;
    }
    return offset;
}

std::unordered_map<std::string, SfntTableRecord> readSfntDirectory(
    SfntReader &reader, std::uint64_t sfntOffset)
{
    unsigned char header[12] = {};
    if (!reader.read(sfntOffset, header, sizeof(header))
        || !validSfntSignature(header)) {
        return {};
    }
    const std::uint16_t tableCount = readU16BE(header + 4u);
    constexpr std::uint16_t kMaxTableCount = 4096;
    if (tableCount == 0u || tableCount > kMaxTableCount) return {};

    std::unordered_map<std::string, SfntTableRecord> tables;
    for (std::uint16_t index = 0; index < tableCount; ++index) {
        unsigned char record[16] = {};
        const std::uint64_t recordOffset = sfntOffset + 12u
            + static_cast<std::uint64_t>(index) * 16u;
        if (!reader.read(recordOffset, record, sizeof(record))) return {};
        const std::uint32_t offset = readU32BE(record + 8u);
        const std::uint32_t length = readU32BE(record + 12u);
        if (offset > reader.size() || length > reader.size() - offset) continue;
        tables.insert_or_assign(
            std::string(reinterpret_cast<const char *>(record), 4),
            SfntTableRecord{offset, length});
    }
    return tables;
}

bool readTablePrefix(SfntReader &reader,
                     const std::unordered_map<std::string, SfntTableRecord> &tables,
                     const char tag[4], unsigned char *output,
                     std::size_t length)
{
    const auto found = tables.find(std::string(tag, 4));
    return found != tables.end() && found->second.length >= length
        && reader.read(found->second.offset, output, length);
}

} // namespace

bool parseAndroidFontConfig(const std::string &xml,
                            const std::string &fontDirectory,
                            AndroidFontConfig &config)
{
    FamilyContext family;
    std::optional<TextCapture> capture;
    bool insideFamily = false;
    bool sawFamilySet = false;
    std::string familyListName;
    std::size_t cursor = 0;

    while (cursor < xml.size()) {
        const std::size_t tagStart = xml.find('<', cursor);
        if (tagStart == std::string::npos) break;
        if (capture) capture->text.append(xml, cursor, tagStart - cursor);

        if (xml.compare(tagStart, 4, "<!--") == 0) {
            const std::size_t end = xml.find("-->", tagStart + 4);
            if (end == std::string::npos) return false;
            cursor = end + 3;
            continue;
        }
        const std::size_t tagEnd = findTagEnd(xml, tagStart + 1);
        if (tagEnd == std::string::npos) return false;
        const std::string raw = xml.substr(tagStart + 1, tagEnd - tagStart - 1);
        cursor = tagEnd + 1;
        if (!raw.empty() && (raw.front() == '?' || raw.front() == '!')) continue;

        std::string name;
        Attributes attributes;
        bool closing = false;
        bool selfClosing = false;
        parseTag(raw, name, attributes, closing, selfClosing);
        if (name == "familyset" || name == "fonts-modification") sawFamilySet = true;

        if (closing) {
            if (capture && capture->tag == name) {
                capture->text = trim(decodeXml(std::move(capture->text)));
                if (insideFamily && name == "name" && !capture->text.empty()) {
                    family.families.push_back(capture->text);
                } else if (insideFamily && (name == "font" || name == "file")) {
                    finishFace(family, *capture, fontDirectory);
                }
                capture.reset();
            }
            if (name == "family" && insideFamily) {
                finishFamily(family, config);
                insideFamily = false;
            } else if (name == "family-list") {
                familyListName.clear();
            }
            continue;
        }

        if (name == "family-list") {
            familyListName = lowerAscii(trim(attribute(attributes, "name")));
            if (selfClosing) familyListName.clear();
        } else if (name == "family") {
            if (insideFamily) finishFamily(family, config);
            insideFamily = true;
            family = {};
            if (!familyListName.empty()) family.families.push_back(familyListName);
            const std::string familyName = attribute(attributes, "name");
            if (!familyName.empty()) family.families.push_back(familyName);
            family.locales = splitTags(attribute(attributes, "lang"));
            family.variant = parseVariant(attribute(attributes, "variant"));
            if (selfClosing) {
                finishFamily(family, config);
                insideFamily = false;
            }
        } else if (name == "alias") {
            AndroidFontConfigAlias alias;
            alias.name = attribute(attributes, "name");
            alias.target = attribute(attributes, "to");
            alias.weight = parseInt(attribute(attributes, "weight"), 0, 0, 1000);
            if (!alias.name.empty() && !alias.target.empty()) {
                config.aliases.push_back(std::move(alias));
            }
        } else if (insideFamily && name == "axis" && capture
                   && (capture->tag == "font" || capture->tag == "file")) {
            const std::string tag = attribute(attributes, "tag");
            const std::optional<float> value =
                parseFloat(attribute(attributes, "stylevalue"));
            const bool duplicate = std::any_of(
                capture->variationAxes.begin(), capture->variationAxes.end(),
                [&](const AndroidFontVariationAxis &axis) {
                    return axis.tag == tag;
                });
            if (tag.size() == 4 && value && !duplicate) {
                capture->variationAxes.push_back({tag, *value});
            }
        } else if (insideFamily && (name == "name" || name == "font" || name == "file")) {
            capture = TextCapture{name, std::move(attributes), {}, {}};
            if (selfClosing) capture.reset();
        }
    }

    if (insideFamily) finishFamily(family, config);
    return sawFamilySet;
}

bool hasAndroidFontConfigFamily(const AndroidFontConfig &config,
                                const std::string &family)
{
    const std::string key = canonicalFontFamilyName(family);
    if (key.empty()) return false;
    if (std::any_of(config.aliases.begin(), config.aliases.end(),
                    [&](const AndroidFontConfigAlias &alias) {
        return canonicalFontFamilyName(alias.name) == key;
    })) {
        return true;
    }
    return std::any_of(config.faces.begin(), config.faces.end(),
                       [&](const AndroidFontConfigFace &face) {
        return familyContains(face, family);
    });
}

bool resolveAndroidFontConfigFaceStyle(AndroidFontConfigFace &face)
{
    if (face.weightSpecified && face.slantSpecified) return true;
    SfntReader reader(face.path);
    if (!reader.valid()) return false;
    const std::optional<std::uint64_t> sfntOffset =
        sfntOffsetForFace(reader, face.faceIndex);
    if (!sfntOffset) return false;
    const auto tables = readSfntDirectory(reader, *sfntOffset);
    if (tables.empty()) return false;

    bool weightResolved = face.weightSpecified;
    bool slantResolved = face.slantSpecified;
    unsigned char os2[64] = {};
    if (readTablePrefix(reader, tables, "OS/2", os2, sizeof(os2))) {
        if (!weightResolved) {
            const int intrinsicWeight = static_cast<int>(readU16BE(os2 + 4u));
            if (intrinsicWeight >= 1 && intrinsicWeight <= 1000) {
                face.weight = intrinsicWeight;
                weightResolved = true;
            }
        }
        if (!slantResolved) {
            const std::uint16_t selection = readU16BE(os2 + 62u);
            face.slant = (selection & (1u << 9u)) != 0u
                ? FontSlant::OBLIQUE
                : ((selection & 1u) != 0u
                    ? FontSlant::ITALIC : FontSlant::NORMAL);
            slantResolved = true;
        }
    }

    unsigned char head[46] = {};
    if (readTablePrefix(reader, tables, "head", head, sizeof(head))) {
        const std::uint16_t macStyle = readU16BE(head + 44u);
        if (!weightResolved) {
            face.weight = (macStyle & 1u) != 0u ? 700 : 400;
            weightResolved = true;
        }
        if (!slantResolved) {
            face.slant = (macStyle & 2u) != 0u
                ? FontSlant::ITALIC : FontSlant::NORMAL;
            slantResolved = true;
        }
    }

    if (!slantResolved) {
        unsigned char post[8] = {};
        if (readTablePrefix(reader, tables, "post", post, sizeof(post))) {
            face.slant = readU32BE(post + 4u) != 0u
                ? FontSlant::ITALIC : FontSlant::NORMAL;
            slantResolved = true;
        }
    }
    return weightResolved || slantResolved;
}

std::vector<const AndroidFontConfigFace *> matchAndroidFontConfig(
    const AndroidFontConfig &config, const std::string &family, int weight,
    FontSlant slant, const std::string &locale,
    bool preferEmojiPresentation, bool preferTextPresentation)
{
    const AliasResolution alias = resolveAlias(config, family);
    const std::string &resolvedFamily = alias.family;
    const std::string familyKey = canonicalFontFamilyName(resolvedFamily);
    const int requestedWeight = alias.weight == 0
        ? std::clamp(weight, 1, 1000) : alias.weight;
    std::vector<const AndroidFontConfigFace *> result;
    result.reserve(config.faces.size());
    for (const AndroidFontConfigFace &face : config.faces) {
        const bool named = aliasSelectsFace(alias, face);
        const bool targeted = !face.fallbackFor.empty()
            && canonicalFontFamilyName(face.fallbackFor) == familyKey;
        const bool generalFallback = face.families.empty()
            && face.fallbackFor.empty();
        if (named || targeted || generalFallback) result.push_back(&face);
    }
    std::stable_sort(result.begin(), result.end(), [&](const auto *left, const auto *right) {
        const auto rank = [&](const AndroidFontConfigFace *face) {
            const bool namedFamily = aliasSelectsFace(alias, *face);
            const bool targetedFallback = !face->fallbackFor.empty()
                && canonicalFontFamilyName(face->fallbackFor) == familyKey;
            const int familyRank = namedFamily ? 0 : 1;
            const bool emojiFace = isEmojiPresentationFace(*face);
            const int presentationRank = preferEmojiPresentation
                ? (emojiFace ? 0 : 1)
                : (preferTextPresentation && emojiFace ? 1 : 0);
            const auto weightRank = weightMatchRank(
                face->weight, requestedWeight);
            return std::tuple<int, int, int, int, int, int, int, int, std::size_t>(
                familyRank, presentationRank, targetedFallback ? 0 : 1,
                defaultVariantRank(*face), localeRank(*face, locale),
                slantMatchRank(face->slant, slant),
                weightRank.first, weightRank.second, face->order);
        };
        return rank(left) < rank(right);
    });
    return result;
}

} // namespace wsc::text::detail
