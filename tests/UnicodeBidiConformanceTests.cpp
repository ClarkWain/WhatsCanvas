#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "text/UnicodeBidi.h"

namespace {

using wsc::text::BidiClass;
using wsc::text::BidiParagraphDirection;

std::string trim(const std::string &text)
{
    std::size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) {
        --last;
    }
    return text.substr(first, last - first);
}

std::vector<std::string> split(const std::string &text, char delimiter)
{
    std::vector<std::string> fields;
    std::stringstream stream(text);
    std::string field;
    while (std::getline(stream, field, delimiter)) {
        fields.push_back(trim(field));
    }
    return fields;
}

std::optional<BidiClass> parseBidiClass(const std::string &token)
{
    if (token == "L") return BidiClass::L;
    if (token == "R") return BidiClass::R;
    if (token == "AL") return BidiClass::AL;
    if (token == "EN") return BidiClass::EN;
    if (token == "AN") return BidiClass::AN;
    if (token == "ES") return BidiClass::ES;
    if (token == "ET") return BidiClass::ET;
    if (token == "CS") return BidiClass::CS;
    if (token == "NSM") return BidiClass::NSM;
    if (token == "BN") return BidiClass::BN;
    if (token == "B") return BidiClass::B;
    if (token == "S") return BidiClass::S;
    if (token == "WS") return BidiClass::WS;
    if (token == "ON") return BidiClass::ON;
    if (token == "LRE") return BidiClass::LRE;
    if (token == "RLE") return BidiClass::RLE;
    if (token == "LRO") return BidiClass::LRO;
    if (token == "RLO") return BidiClass::RLO;
    if (token == "PDF") return BidiClass::PDF;
    if (token == "LRI") return BidiClass::LRI;
    if (token == "RLI") return BidiClass::RLI;
    if (token == "FSI") return BidiClass::FSI;
    if (token == "PDI") return BidiClass::PDI;
    return std::nullopt;
}

std::vector<BidiClass> parseClassList(const std::string &field)
{
    std::vector<BidiClass> classes;
    std::stringstream stream(field);
    std::string token;
    while (stream >> token) {
        const std::optional<BidiClass> bidiClass = parseBidiClass(token);
        if (!bidiClass.has_value()) {
            throw std::runtime_error("unknown bidi class token: " + token);
        }
        classes.push_back(*bidiClass);
    }
    return classes;
}

std::vector<std::uint32_t> parseCodepointList(const std::string &field)
{
    std::vector<std::uint32_t> codepoints;
    std::stringstream stream(field);
    std::string token;
    while (stream >> token) {
        codepoints.push_back(static_cast<std::uint32_t>(std::stoul(token, nullptr, 16)));
    }
    return codepoints;
}

std::vector<std::optional<int>> parseLevels(const std::string &field)
{
    std::vector<std::optional<int>> levels;
    std::stringstream stream(field);
    std::string token;
    while (stream >> token) {
        if (token == "x") {
            levels.push_back(std::nullopt);
        } else {
            levels.push_back(std::stoi(token));
        }
    }
    return levels;
}

std::vector<int> parseOrder(const std::string &field)
{
    std::vector<int> order;
    std::stringstream stream(field);
    std::string token;
    while (stream >> token) {
        order.push_back(std::stoi(token));
    }
    return order;
}

std::vector<int> visualOrderFromLevels(const std::vector<std::optional<int>> &levels)
{
    std::vector<int> order;
    int maxLevel = 0;
    int minOddLevel = 126;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (!levels[i].has_value()) {
            continue;
        }
        order.push_back(static_cast<int>(i));
        maxLevel = std::max(maxLevel, *levels[i]);
        if ((*levels[i] % 2) == 1) {
            minOddLevel = std::min(minOddLevel, *levels[i]);
        }
    }
    if (minOddLevel == 126) {
        return order;
    }
    for (int level = maxLevel; level >= minOddLevel; --level) {
        std::size_t start = 0;
        while (start < order.size()) {
            while (start < order.size() && *levels[order[start]] < level) {
                ++start;
            }
            std::size_t end = start;
            while (end < order.size() && *levels[order[end]] >= level) {
                ++end;
            }
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(start),
                         order.begin() + static_cast<std::ptrdiff_t>(end));
            start = end;
        }
    }
    return order;
}

std::string formatLevels(const std::vector<std::optional<int>> &levels)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (i > 0) {
            out << ' ';
        }
        if (levels[i].has_value()) {
            out << *levels[i];
        } else {
            out << 'x';
        }
    }
    return out.str();
}

std::string formatOrder(const std::vector<int> &order)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (i > 0) {
            out << ' ';
        }
        out << order[i];
    }
    return out.str();
}

struct Totals
{
    int cases = 0;
    int failures = 0;
    int skipped = 0;
};

bool containsUnsupportedControlClass(const std::vector<BidiClass> &classes)
{
    return std::any_of(classes.begin(), classes.end(), [](BidiClass bidiClass) {
        return bidiClass == BidiClass::LRI || bidiClass == BidiClass::RLI
            || bidiClass == BidiClass::FSI || bidiClass == BidiClass::PDI
            || bidiClass == BidiClass::LRE || bidiClass == BidiClass::RLE
            || bidiClass == BidiClass::LRO || bidiClass == BidiClass::RLO
            || bidiClass == BidiClass::PDF;
    });
}

bool checkCase(const std::string &sourceName, int lineNumber, const std::string &input,
               const std::vector<std::optional<int>> &expectedLevels,
               const std::vector<int> &expectedOrder,
               const std::vector<std::optional<int>> &actualLevels,
               Totals &totals)
{
    ++totals.cases;
    if (actualLevels == expectedLevels && visualOrderFromLevels(actualLevels) == expectedOrder) {
        return true;
    }

    ++totals.failures;
    if (totals.failures <= 20) {
        std::cerr << sourceName << ':' << lineNumber << " failed for [" << input << "]\n";
        std::cerr << "  expected levels: " << formatLevels(expectedLevels) << "\n";
        std::cerr << "  actual levels:   " << formatLevels(actualLevels) << "\n";
        std::cerr << "  expected order:  " << formatOrder(expectedOrder) << "\n";
        std::cerr << "  actual order:    " << formatOrder(visualOrderFromLevels(actualLevels)) << "\n";
    }
    return false;
}

Totals runBidiTest(const std::string &path, bool exhaustive)
{
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("unable to open " + path);
    }

    Totals totals;
    std::vector<std::optional<int>> expectedLevels;
    std::vector<int> expectedOrder;
    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.resize(comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        if (line.rfind("@Levels:", 0) == 0) {
            expectedLevels = parseLevels(trim(line.substr(8)));
            continue;
        }
        if (line.rfind("@Reorder:", 0) == 0) {
            expectedOrder = parseOrder(trim(line.substr(9)));
            continue;
        }
        if (line[0] == '@') {
            continue;
        }

        const std::vector<std::string> fields = split(line, ';');
        if (fields.size() < 2) {
            throw std::runtime_error("malformed BidiTest line " + std::to_string(lineNumber));
        }
        const std::vector<BidiClass> classes = parseClassList(fields[0]);
        if (!exhaustive && containsUnsupportedControlClass(classes)) {
            ++totals.skipped;
            continue;
        }
        const int bitset = std::stoi(fields[1], nullptr, 16);
        if ((bitset & 1) != 0) {
            checkCase(path, lineNumber, fields[0], expectedLevels, expectedOrder,
                      wsc::text::resolveUnicodeBidiLevelsForClasses(classes, BidiParagraphDirection::Auto),
                      totals);
        }
        if ((bitset & 2) != 0) {
            checkCase(path, lineNumber, fields[0], expectedLevels, expectedOrder,
                      wsc::text::resolveUnicodeBidiLevelsForClasses(classes, BidiParagraphDirection::LeftToRight),
                      totals);
        }
        if ((bitset & 4) != 0) {
            checkCase(path, lineNumber, fields[0], expectedLevels, expectedOrder,
                      wsc::text::resolveUnicodeBidiLevelsForClasses(classes, BidiParagraphDirection::RightToLeft),
                      totals);
        }
    }
    return totals;
}

Totals runBidiCharacterTest(const std::string &path)
{
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("unable to open " + path);
    }

    Totals totals;
    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.resize(comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const std::vector<std::string> fields = split(line, ';');
        if (fields.size() < 5) {
            throw std::runtime_error("malformed BidiCharacterTest line " + std::to_string(lineNumber));
        }
        const std::vector<std::uint32_t> codepoints = parseCodepointList(fields[0]);
        const int direction = std::stoi(fields[1]);
        const BidiParagraphDirection paragraphDirection = direction == 0
            ? BidiParagraphDirection::LeftToRight
            : (direction == 1 ? BidiParagraphDirection::RightToLeft : BidiParagraphDirection::Auto);

        checkCase(path, lineNumber, fields[0], parseLevels(fields[3]), parseOrder(fields[4]),
                  wsc::text::resolveUnicodeBidiLevelsForCodepoints(codepoints, paragraphDirection),
                  totals);
    }
    return totals;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: UnicodeBidiConformanceTests <unicode-data-dir> [--exhaustive]\n";
        return 2;
    }
    const bool exhaustive = argc == 3 && std::string(argv[2]) == "--exhaustive";

    try {
        Totals totals;
        const Totals bidiTest = runBidiTest(std::string(argv[1]) + "/BidiTest.txt", exhaustive);
        Totals bidiCharacterTest;
        if (exhaustive) {
            bidiCharacterTest = runBidiCharacterTest(std::string(argv[1]) + "/BidiCharacterTest.txt");
        }
        totals.cases = bidiTest.cases + bidiCharacterTest.cases;
        totals.failures = bidiTest.failures + bidiCharacterTest.failures;
        totals.skipped = bidiTest.skipped + bidiCharacterTest.skipped;

        std::cout << "Unicode bidi conformance cases: " << totals.cases << "\n";
        std::cout << "Unicode bidi conformance skipped: " << totals.skipped << "\n";
        std::cout << "Unicode bidi conformance failures: " << totals.failures << "\n";
        if (!exhaustive) {
            std::cout << "Unicode bidi exhaustive character/isolate profile: not run by default\n";
        }
        return totals.failures == 0 ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 2;
    }
}
