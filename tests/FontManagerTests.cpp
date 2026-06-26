#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "wsc/Font.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }

    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

bool testRegisterFontFile()
{
    wsc::FontManager manager;
    const bool registered = manager.registerFontFile(wsc::FontDescriptor("Inter", 400), "fonts/Inter-Regular.ttf");
    const auto *face = manager.findFirstFace("Inter");

    return expect(registered, "valid file font should register")
        && expect(manager.hasFamily("Inter"), "registered family should be discoverable")
        && expect(face != nullptr, "registered face should be found")
        && expect(face->sourceType() == wsc::FontSourceType::FILE, "file font should keep source type")
        && expect(face->path() == "fonts/Inter-Regular.ttf", "file font should keep path");
}

bool testRegisterFontMemory()
{
    wsc::FontManager manager;
    std::vector<std::uint8_t> bytes = {0, 1, 2, 3};
    const bool registered = manager.registerFontMemory(wsc::FontDescriptor("MemoryFace"), bytes);
    const auto *face = manager.findFirstFace("MemoryFace");

    return expect(registered, "valid memory font should register")
        && expect(face != nullptr, "memory face should be found")
        && expect(face->sourceType() == wsc::FontSourceType::MEMORY, "memory font should keep source type")
        && expect(face->bytes() != nullptr && face->bytes()->size() == 4, "memory font should keep bytes");
}

bool testFallbackResolutionOrder()
{
    wsc::FontManager manager;
    manager.registerFontFile(wsc::FontDescriptor("Primary"), "primary.ttf");
    manager.registerFontFile(wsc::FontDescriptor("CJK"), "cjk.otf");
    manager.registerFontFile(wsc::FontDescriptor("Emoji"), "emoji.ttf");

    const bool firstFallback = manager.addFallbackFamily("Primary", "CJK");
    const bool secondFallback = manager.addFallbackFamily("Primary", "Emoji");
    manager.addFallbackFamily("Primary", "CJK");
    const auto families = manager.resolveFamilies("Primary");

    return expect(firstFallback && secondFallback, "registered families should form fallback chain")
        && expect(families.size() == 3, "fallback chain should include primary and unique fallbacks")
        && expect(families[0] == "Primary", "primary family should resolve first")
        && expect(families[1] == "CJK", "first fallback should resolve second")
        && expect(families[2] == "Emoji", "second fallback should resolve third")
        && expect(!manager.addFallbackFamily("Primary", "Missing"), "missing fallback should be rejected");
}

} // namespace

int main()
{
    const bool ok = testRegisterFontFile()
        && testRegisterFontMemory()
        && testFallbackResolutionOrder();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
