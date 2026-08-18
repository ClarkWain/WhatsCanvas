#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include <expat.h>

#include "src/ports/SkFontMgr_android_parser.h"

namespace {

enum class Slant { Normal, Italic, Oblique };
enum class Variant { Default, Compact, Elegant };

struct Alias {
    std::string name;
    std::string target;
    int weight = 0;
};

struct Axis {
    std::string tag;
    float value = 0.0f;
};

struct Face {
    std::vector<std::string> families;
    std::vector<std::string> locales;
    std::string fallbackFor;
    std::string path;
    int faceIndex = 0;
    int weight = 400;
    Slant slant = Slant::Normal;
    bool weightSpecified = false;
    bool slantSpecified = false;
    Variant variant = Variant::Default;
    std::vector<Axis> axes;
    std::size_t order = 0;
};

struct Query {
    std::string family;
    int weight = 400;
    Slant slant = Slant::Normal;
    std::string locale;
    bool emoji = false;
    bool text = false;
};

struct XmlMetadata {
    std::vector<Alias> aliases;
    std::vector<std::string> fallbackForOrder;
    std::string error;
};

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

const char *attribute(const XML_Char **attributes, const char *name)
{
    if (!attributes) return nullptr;
    for (std::size_t index = 0; attributes[index] != nullptr; index += 2u) {
        if (std::string(attributes[index]) == name) return attributes[index + 1u];
    }
    return nullptr;
}

void XMLCALL startElement(void *userData, const XML_Char *name,
                          const XML_Char **attributes)
{
    auto &metadata = *static_cast<XmlMetadata *>(userData);
    if (std::string(name) == "alias") {
        const char *aliasName = attribute(attributes, "name");
        const char *target = attribute(attributes, "to");
        if (!aliasName || !target) return;
        Alias alias{lowerAscii(aliasName), lowerAscii(target), 0};
        if (const char *weight = attribute(attributes, "weight")) {
            char *end = nullptr;
            const long parsed = std::strtol(weight, &end, 10);
            if (end != weight && *end == '\0' && parsed >= 0 && parsed <= 1000) {
                alias.weight = static_cast<int>(parsed);
            }
        }
        metadata.aliases.push_back(std::move(alias));
    } else if (std::string(name) == "font") {
        if (const char *fallbackFor = attribute(attributes, "fallbackFor")) {
            const std::string value = fallbackFor;
            const std::string key = lowerAscii(value);
            const bool alreadySeen = std::any_of(
                metadata.fallbackForOrder.begin(), metadata.fallbackForOrder.end(),
                [&](const std::string &candidate) {
                    return lowerAscii(candidate) == key;
                });
            if (!alreadySeen) {
                metadata.fallbackForOrder.push_back(value);
            }
        }
    }
}

std::string readTextFile(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

bool parseXmlMetadata(const std::string &path, XmlMetadata &metadata)
{
    const std::string xml = readTextFile(path);
    if (xml.empty()) {
        metadata.error = "cannot read XML";
        return false;
    }
    XML_Parser parser = XML_ParserCreate(nullptr);
    if (!parser) {
        metadata.error = "cannot create Expat parser";
        return false;
    }
    XML_SetUserData(parser, &metadata);
    XML_SetElementHandler(parser, startElement, nullptr);
    const XML_Status status = XML_Parse(parser, xml.data(),
                                        static_cast<int>(xml.size()), XML_TRUE);
    if (status == XML_STATUS_ERROR) {
        std::ostringstream error;
        error << XML_ErrorString(XML_GetErrorCode(parser)) << " at line "
              << XML_GetCurrentLineNumber(parser);
        metadata.error = error.str();
    }
    XML_ParserFree(parser);
    return status != XML_STATUS_ERROR;
}

std::string jsonString(const std::string &value)
{
    std::ostringstream output;
    output << '"';
    for (unsigned char ch : value) {
        switch (ch) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (ch < 0x20u) {
                output << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0') << static_cast<int>(ch)
                       << std::dec << std::setfill(' ');
            } else {
                output << static_cast<char>(ch);
            }
        }
    }
    output << '"';
    return output.str();
}

std::vector<std::string> split(const std::string &value, char delimiter)
{
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(delimiter, start);
        result.push_back(value.substr(
            start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1u;
    }
    return result;
}

std::optional<Query> parseQuery(const std::string &value)
{
    const auto fields = split(value, '|');
    if (fields.size() != 5u || fields[0].empty()) return std::nullopt;
    char *end = nullptr;
    const long weight = std::strtol(fields[1].c_str(), &end, 10);
    if (end == fields[1].c_str() || *end != '\0' || weight < 1 || weight > 1000) {
        return std::nullopt;
    }
    Query query;
    query.family = fields[0];
    query.weight = static_cast<int>(weight);
    if (fields[2] == "normal") query.slant = Slant::Normal;
    else if (fields[2] == "italic") query.slant = Slant::Italic;
    else if (fields[2] == "oblique") query.slant = Slant::Oblique;
    else return std::nullopt;
    query.locale = fields[3];
    if (fields[4] == "emoji") query.emoji = true;
    else if (fields[4] == "text") query.text = true;
    else if (fields[4] != "default") return std::nullopt;
    return query;
}

const char *slantName(Slant slant)
{
    if (slant == Slant::Italic) return "italic";
    if (slant == Slant::Oblique) return "oblique";
    return "normal";
}

const char *variantName(Variant variant)
{
    if (variant == Variant::Compact) return "compact";
    if (variant == Variant::Elegant) return "elegant";
    return "default";
}

Variant convertVariant(FontVariant variant)
{
    if (variant == kCompact_FontVariant) return Variant::Compact;
    if (variant == kElegant_FontVariant) return Variant::Elegant;
    return Variant::Default;
}

std::vector<std::string> familyNames(const FontFamily &family,
                                     const std::unordered_set<std::string> &aliasNames)
{
    std::vector<std::string> names;
    for (const SkString &name : family.fNames) {
        const std::string value = name.c_str();
        if (!aliasNames.contains(lowerAscii(value))) names.push_back(value);
    }
    return names;
}

std::vector<std::string> locales(const FontFamily &family)
{
    std::vector<std::string> result;
    for (const SkLanguage &language : family.fLanguages) {
        result.emplace_back(language.getTag().c_str());
    }
    return result;
}

std::string axisTag(SkFourByteTag tag)
{
    std::string result(4, '\0');
    result[0] = static_cast<char>((tag >> 24u) & 0xffu);
    result[1] = static_cast<char>((tag >> 16u) & 0xffu);
    result[2] = static_cast<char>((tag >> 8u) & 0xffu);
    result[3] = static_cast<char>(tag & 0xffu);
    return result;
}

void appendFamilyFaces(const FontFamily &family,
                       const std::vector<std::string> &names,
                       std::vector<Face> &faces)
{
    for (const FontFileInfo &font : family.fFonts) {
        Face face;
        face.families = names;
        face.locales = locales(family);
        face.fallbackFor = family.fFallbackFor.c_str();
        face.path = font.fFileName.c_str();
        face.faceIndex = font.fIndex;
        face.weightSpecified = font.fWeight != 0;
        face.weight = face.weightSpecified ? font.fWeight : 400;
        face.slantSpecified = font.fStyle != FontFileInfo::Style::kAuto;
        face.slant = font.fStyle == FontFileInfo::Style::kItalic
            ? Slant::Italic : Slant::Normal;
        face.variant = convertVariant(family.fVariant);
        for (const auto &coordinate : font.fVariationDesignPosition) {
            face.axes.push_back({axisTag(coordinate.axis), coordinate.value});
        }
        face.order = faces.size();
        faces.push_back(std::move(face));
    }
}

bool familyContains(const Face &face, const std::string &family)
{
    const std::string key = lowerAscii(family);
    return std::any_of(face.families.begin(), face.families.end(),
                       [&](const std::string &candidate) {
        return lowerAscii(candidate) == key;
    });
}

struct AliasResolution { std::string family; int weight = 0; };

AliasResolution resolveAlias(const std::vector<Alias> &aliases,
                             const std::string &family)
{
    AliasResolution result{family, 0};
    for (std::size_t depth = 0; depth <= aliases.size(); ++depth) {
        const std::string key = lowerAscii(result.family);
        const auto found = std::find_if(aliases.begin(), aliases.end(),
            [&](const Alias &alias) { return lowerAscii(alias.name) == key; });
        if (found == aliases.end() || found->target.empty()
            || lowerAscii(found->target) == key) break;
        if (result.weight == 0 && found->weight != 0) result.weight = found->weight;
        result.family = found->target;
    }
    return result;
}

int slantRank(Slant actual, Slant requested)
{
    if (actual == requested) return 0;
    if (actual != Slant::Normal && requested != Slant::Normal) return 1;
    return 2;
}

std::pair<int, int> weightRank(int actual, int requested)
{
    actual = std::clamp(actual, 1, 1000);
    requested = std::clamp(requested, 1, 1000);
    if (requested >= 400 && requested <= 500) {
        if (actual >= requested && actual <= 500) return {0, actual - requested};
        if (actual < requested) return {1, requested - actual};
        return {2, actual - 500};
    }
    if (requested < 400) {
        return actual <= requested ? std::pair{0, requested - actual}
                                   : std::pair{1, actual - requested};
    }
    return actual >= requested ? std::pair{0, actual - requested}
                               : std::pair{1, requested - actual};
}

int localeRank(const Face &face, const std::string &locale)
{
    if (face.locales.empty() || locale.empty()) return 3;
    std::string requested = lowerAscii(locale);
    std::replace(requested.begin(), requested.end(), '_', '-');
    const std::size_t separator = requested.find('-');
    const std::string language = requested.substr(0, separator);
    std::string preferredScript;
    if (language == "zh") {
        if (requested.find("-tw") != std::string::npos
            || requested.find("-hk") != std::string::npos
            || requested.find("-mo") != std::string::npos
            || requested.find("-hant") != std::string::npos) preferredScript = "hant";
        else if (requested.find("-cn") != std::string::npos
                 || requested.find("-sg") != std::string::npos
                 || requested.find("-hans") != std::string::npos) preferredScript = "hans";
    }
    int best = 4;
    for (std::string candidate : face.locales) {
        candidate = lowerAscii(candidate);
        std::replace(candidate.begin(), candidate.end(), '_', '-');
        if (candidate == requested) return 0;
        const std::size_t candidateSeparator = candidate.find('-');
        if (candidate.substr(0, candidateSeparator) != language) continue;
        if (!preferredScript.empty()
            && candidate.find('-' + preferredScript) != std::string::npos) best = std::min(best, 1);
        else best = std::min(best, 2);
    }
    return best;
}

bool isEmoji(const Face &face)
{
    return std::any_of(face.locales.begin(), face.locales.end(), [](const std::string &locale) {
        const std::string key = lowerAscii(locale);
        return key == "und-zsye" || key.find("-zsye") != std::string::npos;
    });
}

std::vector<std::size_t> match(const std::vector<Face> &faces,
                               const std::vector<Alias> &aliases,
                               const Query &query)
{
    const AliasResolution alias = resolveAlias(aliases, query.family);
    const std::string familyKey = lowerAscii(alias.family);
    const int requestedWeight = alias.weight == 0 ? query.weight : alias.weight;
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < faces.size(); ++index) {
        const Face &face = faces[index];
        const bool named = familyContains(face, alias.family)
            && (alias.weight == 0 || face.weight == alias.weight);
        const bool targeted = !face.fallbackFor.empty()
            && lowerAscii(face.fallbackFor) == familyKey;
        const bool general = face.families.empty() && face.fallbackFor.empty();
        if (named || targeted || general) result.push_back(index);
    }
    std::stable_sort(result.begin(), result.end(), [&](std::size_t left, std::size_t right) {
        const auto rank = [&](std::size_t index) {
            const Face &face = faces[index];
            const bool named = familyContains(face, alias.family)
                && (alias.weight == 0 || face.weight == alias.weight);
            const bool targeted = !face.fallbackFor.empty()
                && lowerAscii(face.fallbackFor) == familyKey;
            const int presentation = query.emoji ? (isEmoji(face) ? 0 : 1)
                : (query.text && isEmoji(face) ? 1 : 0);
            const auto weight = weightRank(face.weight, requestedWeight);
            return std::tuple(named ? 0 : 1, presentation, targeted ? 0 : 1,
                face.variant == Variant::Compact ? 1 : 0, localeRank(face, query.locale),
                slantRank(face.slant, query.slant), weight.first, weight.second, face.order);
        };
        return rank(left) < rank(right);
    });
    return result;
}

void appendStrings(std::ostringstream &output, const std::vector<std::string> &values)
{
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) output << ',';
        output << jsonString(values[index]);
    }
    output << ']';
}

void usage()
{
    std::cerr << "Usage: SkiaAndroidFontOracleProbe --config <xml> <font-dir> "
                 "[--config ...] [--query <family|weight|slant|locale|presentation> ...]\n";
}

} // namespace

int main(int argc, char **argv)
{
    std::vector<std::unique_ptr<FontFamily>> parsedFamilies;
    XmlMetadata metadata;
    std::vector<Query> queries;
    bool loaded = false;
    for (int index = 1; index < argc;) {
        const std::string argument = argv[index++];
        if (argument == "--config") {
            if (index + 1 >= argc) { usage(); return EXIT_FAILURE; }
            const std::string xml = argv[index++];
            std::string basePath = argv[index++];
            if (!parseXmlMetadata(xml, metadata)) {
                std::cerr << "Failed to parse " << xml << ": " << metadata.error << '\n';
                return EXIT_FAILURE;
            }
            if (!basePath.empty() && basePath.back() != '/' && basePath.back() != '\\') {
                basePath.push_back('/');
            }
            SkFontMgr_Android_Parser::GetCustomFontFamilies(
                parsedFamilies, SkString(basePath.c_str()), xml.c_str(), nullptr, nullptr);
            loaded = true;
        } else if (argument == "--query") {
            if (index >= argc) { usage(); return EXIT_FAILURE; }
            auto query = parseQuery(argv[index++]);
            if (!query) { std::cerr << "Invalid query specification.\n"; return EXIT_FAILURE; }
            queries.push_back(std::move(*query));
        } else {
            usage();
            return EXIT_FAILURE;
        }
    }
    if (!loaded) { usage(); return EXIT_FAILURE; }

    std::unordered_set<std::string> aliasNames;
    for (const Alias &alias : metadata.aliases) aliasNames.insert(lowerAscii(alias.name));
    std::vector<Face> faces;
    for (const auto &familyPointer : parsedFamilies) {
        const FontFamily &family = *familyPointer;
        const auto names = familyNames(family, aliasNames);
        if (!family.fFonts.empty() && (!family.fNames.empty() ? !names.empty() : true)) {
            appendFamilyFaces(family, names, faces);
        }
        for (const std::string &fallbackFor : metadata.fallbackForOrder) {
            const auto *child = family.fallbackFamilies.find(SkString(fallbackFor.c_str()));
            if (child && child->get()) appendFamilyFaces(**child, {}, faces);
        }
    }

    std::ostringstream output;
    output << std::setprecision(9)
           << "{\"schema\":\"whatscanvas.android-font-oracle.v1\",\"engine\":\"skia\",\"faces\":[";
    for (std::size_t index = 0; index < faces.size(); ++index) {
        if (index) output << ',';
        const Face &face = faces[index];
        output << "{\"index\":" << index << ",\"families\":";
        appendStrings(output, face.families);
        output << ",\"locales\":";
        appendStrings(output, face.locales);
        output << ",\"fallbackFor\":" << jsonString(face.fallbackFor)
               << ",\"path\":" << jsonString(face.path)
               << ",\"faceIndex\":" << face.faceIndex
               << ",\"weight\":" << face.weight
               << ",\"slant\":" << jsonString(slantName(face.slant))
               << ",\"weightSpecified\":" << (face.weightSpecified ? "true" : "false")
               << ",\"slantSpecified\":" << (face.slantSpecified ? "true" : "false")
               << ",\"variant\":" << jsonString(variantName(face.variant)) << ",\"axes\":[";
        for (std::size_t axis = 0; axis < face.axes.size(); ++axis) {
            if (axis) output << ',';
            output << "{\"tag\":" << jsonString(face.axes[axis].tag)
                   << ",\"value\":" << face.axes[axis].value << '}';
        }
        output << "]}";
    }
    output << "],\"aliases\":[";
    for (std::size_t index = 0; index < metadata.aliases.size(); ++index) {
        if (index) output << ',';
        const Alias &alias = metadata.aliases[index];
        output << "{\"name\":" << jsonString(alias.name)
               << ",\"target\":" << jsonString(alias.target)
               << ",\"weight\":" << alias.weight << '}';
    }
    output << "],\"queries\":[";
    for (std::size_t index = 0; index < queries.size(); ++index) {
        if (index) output << ',';
        const Query &query = queries[index];
        const auto matches = match(faces, metadata.aliases, query);
        output << "{\"family\":" << jsonString(query.family)
               << ",\"weight\":" << query.weight
               << ",\"slant\":" << jsonString(slantName(query.slant))
               << ",\"locale\":" << jsonString(query.locale)
               << ",\"presentation\":" << jsonString(query.emoji ? "emoji" : (query.text ? "text" : "default"))
               << ",\"matches\":[";
        for (std::size_t matchIndex = 0; matchIndex < matches.size(); ++matchIndex) {
            if (matchIndex) output << ',';
            output << matches[matchIndex];
        }
        output << "]}";
    }
    output << "]}\n";
    std::cout << output.str();
    return EXIT_SUCCESS;
}
