#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "include/core/SkFontScanner.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkStream.h"
#include "include/core/SkString.h"
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
        else if (ch < 0x20u) {
            output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                   << static_cast<int>(ch) << std::dec << std::setfill(' ');
        } else output << static_cast<char>(ch);
    }
    output << '"';
    return output.str();
}

std::string tagString(SkFourByteTag tag)
{
    std::string value(4, '\0');
    value[0] = static_cast<char>((tag >> 24u) & 0xffu);
    value[1] = static_cast<char>((tag >> 16u) & 0xffu);
    value[2] = static_cast<char>((tag >> 8u) & 0xffu);
    value[3] = static_cast<char>(tag & 0xffu);
    return value;
}

const char *slantName(SkFontStyle::Slant slant)
{
    if (slant == SkFontStyle::kItalic_Slant) return "italic";
    if (slant == SkFontStyle::kOblique_Slant) return "oblique";
    return "normal";
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: SkiaFontScannerProbe <font> [face-index] [instance-index]\n";
        return EXIT_FAILURE;
    }
    const int faceIndex = argc >= 3 ? std::atoi(argv[2]) : 0;
    const int instanceIndex = argc >= 4 ? std::atoi(argv[3]) : 0;
    if (faceIndex < 0 || instanceIndex < 0) return EXIT_FAILURE;

    SkFILEStream stream(argv[1]);
    if (!stream.isValid()) {
        std::cerr << "Cannot open font: " << argv[1] << '\n';
        return EXIT_FAILURE;
    }
    std::unique_ptr<SkFontScanner> scanner = SkFontScanner_Make_FreeType();
    if (!scanner) return EXIT_FAILURE;
    int faceCount = 0;
    int instanceCount = 0;
    if (!scanner->scanFile(&stream, &faceCount)
        || !scanner->scanFace(&stream, faceIndex, &instanceCount)) {
        std::cerr << "FreeType scanner rejected the font or face index.\n";
        return EXIT_FAILURE;
    }
    SkString name;
    SkFontStyle style;
    bool fixedPitch = false;
    SkFontScanner::AxisDefinitions axes;
    SkFontScanner::VariationPosition position;
    if (!scanner->scanInstance(&stream, faceIndex, instanceIndex, &name, &style,
                               &fixedPitch, &axes, &position)) {
        std::cerr << "FreeType scanner rejected the instance index.\n";
        return EXIT_FAILURE;
    }

    std::ostringstream output;
    output << std::setprecision(9)
           << "{\"schema\":\"whatscanvas.skia-font-scanner.v1\","
              "\"engine\":\"skia-freetype\",\"family\":"
           << jsonString(name.c_str())
           << ",\"faceCount\":" << faceCount
           << ",\"instanceCount\":" << instanceCount
           << ",\"faceIndex\":" << faceIndex
           << ",\"instanceIndex\":" << instanceIndex
           << ",\"weight\":" << style.weight()
           << ",\"width\":" << style.width()
           << ",\"slant\":" << jsonString(slantName(style.slant()))
           << ",\"fixedPitch\":" << (fixedPitch ? "true" : "false")
           << ",\"axes\":[";
    for (int index = 0; index < axes.size(); ++index) {
        if (index) output << ',';
        const auto &axis = axes[index];
        output << "{\"tag\":" << jsonString(tagString(axis.tag))
               << ",\"min\":" << axis.min << ",\"default\":" << axis.def
               << ",\"max\":" << axis.max
               << ",\"hidden\":" << (axis.isHidden() ? "true" : "false") << '}';
    }
    output << "],\"position\":[";
    for (int index = 0; index < position.size(); ++index) {
        if (index) output << ',';
        output << "{\"tag\":" << jsonString(tagString(position[index].axis))
               << ",\"value\":" << position[index].value << '}';
    }
    output << "]}\n";
    std::cout << output.str();
    return EXIT_SUCCESS;
}
