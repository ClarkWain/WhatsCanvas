#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "text/platform/AndroidFontConfig.h"

namespace {

struct Query
{
    std::string family;
    int weight = 400;
    wsc::FontSlant slant = wsc::FontSlant::NORMAL;
    std::string locale;
    bool emoji = false;
    bool text = false;
};

std::string readTextFile(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
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
            break;
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
    const std::vector<std::string> fields = split(value, '|');
    if (fields.size() != 5u || fields[0].empty()) return std::nullopt;
    char *end = nullptr;
    const long weight = std::strtol(fields[1].c_str(), &end, 10);
    if (end == fields[1].c_str() || *end != '\0'
        || weight < 1 || weight > 1000) {
        return std::nullopt;
    }

    Query query;
    query.family = fields[0];
    query.weight = static_cast<int>(weight);
    if (fields[2] == "normal") {
        query.slant = wsc::FontSlant::NORMAL;
    } else if (fields[2] == "italic") {
        query.slant = wsc::FontSlant::ITALIC;
    } else if (fields[2] == "oblique") {
        query.slant = wsc::FontSlant::OBLIQUE;
    } else {
        return std::nullopt;
    }
    query.locale = fields[3];
    if (fields[4] == "emoji") {
        query.emoji = true;
    } else if (fields[4] == "text") {
        query.text = true;
    } else if (fields[4] != "default") {
        return std::nullopt;
    }
    return query;
}

const char *slantName(wsc::FontSlant slant)
{
    switch (slant) {
    case wsc::FontSlant::NORMAL: return "normal";
    case wsc::FontSlant::ITALIC: return "italic";
    case wsc::FontSlant::OBLIQUE: return "oblique";
    }
    return "normal";
}

const char *variantName(wsc::text::detail::AndroidFontVariant variant)
{
    using Variant = wsc::text::detail::AndroidFontVariant;
    switch (variant) {
    case Variant::Default: return "default";
    case Variant::Compact: return "compact";
    case Variant::Elegant: return "elegant";
    }
    return "default";
}

std::string relativeFontPath(const std::string &path,
                             const std::string &fontDirectory)
{
    if (fontDirectory.empty()) return path;
    std::string prefix = fontDirectory;
    while (prefix.size() > 1u
           && (prefix.back() == '/' || prefix.back() == '\\')) {
        prefix.pop_back();
    }
    if (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0
        && (path[prefix.size()] == '/' || path[prefix.size()] == '\\')) {
        return path.substr(prefix.size() + 1u);
    }
    return path;
}

void appendStringArray(std::ostringstream &output,
                       const std::vector<std::string> &values)
{
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0u) output << ',';
        output << jsonString(values[index]);
    }
    output << ']';
}

void printUsage()
{
    std::cerr
        << "Usage: WhatsCanvasAndroidFontOracleProbe --config <xml> <font-dir> "
           "[--config <xml> <font-dir> ...] "
           "[--query <family|weight|slant|locale|presentation> ...]\n"
           "  slant: normal, italic, or oblique\n"
           "  presentation: default, emoji, or text\n";
}

} // namespace

int main(int argc, char **argv)
{
    wsc::text::detail::AndroidFontConfig config;
    std::vector<Query> queries;
    std::vector<std::string> fontDirectories;
    bool loadedConfig = false;

    for (int index = 1; index < argc;) {
        const std::string argument = argv[index++];
        if (argument == "--config") {
            if (index + 1 >= argc) {
                printUsage();
                return EXIT_FAILURE;
            }
            const std::string path = argv[index++];
            const std::string fontDirectory = argv[index++];
            const std::string xml = readTextFile(path);
            if (xml.empty() || !wsc::text::detail::parseAndroidFontConfig(
                                   xml, fontDirectory, config)) {
                std::cerr << "Failed to parse Android font config: " << path << '\n';
                return EXIT_FAILURE;
            }
            fontDirectories.push_back(fontDirectory);
            loadedConfig = true;
        } else if (argument == "--query") {
            if (index >= argc) {
                printUsage();
                return EXIT_FAILURE;
            }
            const auto query = parseQuery(argv[index++]);
            if (!query) {
                std::cerr << "Invalid query specification.\n";
                return EXIT_FAILURE;
            }
            queries.push_back(*query);
        } else {
            printUsage();
            return EXIT_FAILURE;
        }
    }
    if (!loadedConfig) {
        printUsage();
        return EXIT_FAILURE;
    }

    for (auto &face : config.faces) {
        if (!face.weightSpecified || !face.slantSpecified) {
            (void)wsc::text::detail::resolveAndroidFontConfigFaceStyle(face);
        }
    }

    std::ostringstream output;
    output << std::setprecision(9);
    output << "{\"schema\":\"whatscanvas.android-font-oracle.v1\","
              "\"engine\":\"whatscanvas\",\"faces\":[";
    for (std::size_t index = 0; index < config.faces.size(); ++index) {
        const auto &face = config.faces[index];
        if (index != 0u) output << ',';
        std::string normalizedPath = face.path;
        for (const std::string &fontDirectory : fontDirectories) {
            const std::string candidate = relativeFontPath(face.path, fontDirectory);
            if (candidate != face.path) {
                normalizedPath = candidate;
                break;
            }
        }
        output << "{\"index\":" << index << ",\"families\":";
        appendStringArray(output, face.families);
        output << ",\"locales\":";
        appendStringArray(output, face.locales);
        output << ",\"fallbackFor\":" << jsonString(face.fallbackFor)
               << ",\"path\":" << jsonString(normalizedPath)
               << ",\"faceIndex\":" << face.faceIndex
               << ",\"weight\":" << face.weight
               << ",\"slant\":" << jsonString(slantName(face.slant))
               << ",\"weightSpecified\":"
               << (face.weightSpecified ? "true" : "false")
               << ",\"slantSpecified\":"
               << (face.slantSpecified ? "true" : "false")
               << ",\"variant\":" << jsonString(variantName(face.variant))
               << ",\"axes\":[";
        for (std::size_t axisIndex = 0;
             axisIndex < face.variationAxes.size(); ++axisIndex) {
            if (axisIndex != 0u) output << ',';
            const auto &axis = face.variationAxes[axisIndex];
            output << "{\"tag\":" << jsonString(axis.tag)
                   << ",\"value\":" << axis.value << '}';
        }
        output << "]}";
    }
    output << "],\"aliases\":[";
    for (std::size_t index = 0; index < config.aliases.size(); ++index) {
        if (index != 0u) output << ',';
        const auto &alias = config.aliases[index];
        output << "{\"name\":" << jsonString(alias.name)
               << ",\"target\":" << jsonString(alias.target)
               << ",\"weight\":" << alias.weight << '}';
    }
    output << "],\"queries\":[";
    for (std::size_t queryIndex = 0; queryIndex < queries.size(); ++queryIndex) {
        if (queryIndex != 0u) output << ',';
        const Query &query = queries[queryIndex];
        const auto matches = wsc::text::detail::matchAndroidFontConfig(
            config, query.family, query.weight, query.slant, query.locale,
            query.emoji, query.text);
        output << "{\"family\":" << jsonString(query.family)
               << ",\"weight\":" << query.weight
               << ",\"slant\":" << jsonString(slantName(query.slant))
               << ",\"locale\":" << jsonString(query.locale)
               << ",\"presentation\":"
               << jsonString(query.emoji ? "emoji" : (query.text ? "text" : "default"))
               << ",\"matches\":[";
        for (std::size_t matchIndex = 0; matchIndex < matches.size(); ++matchIndex) {
            if (matchIndex != 0u) output << ',';
            const auto *face = matches[matchIndex];
            output << static_cast<std::size_t>(face - config.faces.data());
        }
        output << "]}";
    }
    output << "]}\n";
    std::cout << output.str();
    return EXIT_SUCCESS;
}
