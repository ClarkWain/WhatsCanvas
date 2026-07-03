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

bool testFontFaceCodepointRanges()
{
    wsc::FontFace face = wsc::FontFace::fromFile(wsc::FontDescriptor("Fallback"), "fallback.ttf");
    face.addCodepointRange(0x1F300, 0x1FAFF);
    face.addCodepointRange(126, 32);

    return expect(face.hasCodepointRanges(), "font face should report declared ranges")
        && expect(face.codepointRanges().size() == 1, "invalid codepoint ranges should be ignored")
        && expect(face.supportsCodepoint(0x1F600), "font face should match codepoints inside a range")
        && expect(!face.supportsCodepoint('A'), "font face should reject codepoints outside declared ranges");
}

bool testFontFaceCollectionIndex()
{
    wsc::FontFace defaultFile = wsc::FontFace::fromFile(wsc::FontDescriptor("Default"), "collection.ttc");
    wsc::FontFace indexedFile = wsc::FontFace::fromFile(wsc::FontDescriptor("Indexed"), "collection.ttc", 2);
    wsc::FontFace clampedFile = wsc::FontFace::fromFile(wsc::FontDescriptor("Clamped"), "collection.ttc", -3);
    wsc::FontFace indexedMemory =
        wsc::FontFace::fromMemory(wsc::FontDescriptor("MemoryIndexed"), {1, 2, 3, 4}, 1);
    wsc::FontManager manager;
    manager.registerFontFile(wsc::FontDescriptor("ManagedFile"), "collection.ttc", 3);
    manager.registerFontMemory(wsc::FontDescriptor("ManagedMemory"), {5, 6, 7, 8}, 4);
    const wsc::FontFace *managedFile = manager.findFirstFace("ManagedFile");
    const wsc::FontFace *managedMemory = manager.findFirstFace("ManagedMemory");

    return expect(defaultFile.faceIndex() == 0, "file font should default to collection face 0")
        && expect(indexedFile.faceIndex() == 2, "file font should preserve explicit collection face index")
        && expect(clampedFile.faceIndex() == 0, "negative file face index should clamp to 0")
        && expect(indexedMemory.faceIndex() == 1, "memory font should preserve explicit collection face index")
        && expect(managedFile != nullptr && managedFile->faceIndex() == 3,
                  "font manager file registration should preserve collection face index")
        && expect(managedMemory != nullptr && managedMemory->faceIndex() == 4,
                  "font manager memory registration should preserve collection face index");
}

bool testFallbackResolutionOrder()
{
    wsc::FontManager manager;
    manager.registerFontFile(wsc::FontDescriptor("Primary"), "primary.ttf");
    manager.registerFontFile(wsc::FontDescriptor("CJK"), "cjk.otf");
    manager.registerFontFile(wsc::FontDescriptor("Fallback"), "fallback.ttf");

    const bool firstFallback = manager.addFallbackFamily("Primary", "CJK");
    const bool secondFallback = manager.addFallbackFamily("Primary", "Fallback");
    manager.addFallbackFamily("Primary", "CJK");
    const auto families = manager.resolveFamilies("Primary");

    return expect(firstFallback && secondFallback, "registered families should form fallback chain")
        && expect(families.size() == 3, "fallback chain should include primary and unique fallbacks")
        && expect(families[0] == "Primary", "primary family should resolve first")
        && expect(families[1] == "CJK", "first fallback should resolve second")
        && expect(families[2] == "Fallback", "second fallback should resolve third")
        && expect(!manager.addFallbackFamily("Primary", "Missing"), "missing fallback should be rejected");
}

} // namespace

int main()
{
    const bool ok = testRegisterFontFile()
        && testRegisterFontMemory()
        && testFontFaceCodepointRanges()
        && testFontFaceCollectionIndex()
        && testFallbackResolutionOrder();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
