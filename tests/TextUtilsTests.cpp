#include <cstdlib>
#include <iostream>
#include <string>

#include "text/TextUtils.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }

    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

bool testDecodeValidUtf8()
{
    const std::string text = "A\xe4\xb8\xad\xf0\x9f\x98\x80";
    const auto codepoints = wsc::text::decodeUtf8(text);

    return expect(codepoints.size() == 3, "valid UTF-8 should decode to three codepoints")
        && expect(codepoints[0].value == 'A', "ASCII codepoint should be preserved")
        && expect(codepoints[1].value == 0x4E2D, "CJK codepoint should be decoded")
        && expect(codepoints[2].value == 0x1F600, "four-byte codepoint should be decoded")
        && expect(wsc::text::isValidUtf8(text), "valid UTF-8 should validate");
}

bool testInvalidUtf8Replacement()
{
    const std::string text = std::string("A") + static_cast<char>(0xC0) + "B";
    const auto codepoints = wsc::text::decodeUtf8(text);

    return expect(codepoints.size() == 3, "invalid byte should consume one byte")
        && expect(!codepoints[1].valid, "invalid byte should be marked invalid")
        && expect(!wsc::text::isValidUtf8(text), "invalid UTF-8 should not validate")
        && expect(wsc::text::normalizeUtf8ForText(text) == (std::string("A") + "\xEF\xBF\xBD" + "B"),
                  "invalid byte should normalize to replacement character");
}

bool testAsciiFallbackKeepsShape()
{
    const std::string text = "Hi\t\xe4\xb8\xad\n!";
    const std::string fallback = wsc::text::makeAsciiFallbackText(text);

    return expect(fallback == "Hi    ?\n!", "fallback should preserve ASCII controls and replace non-ASCII")
        && expect(wsc::text::countUtf8Codepoints("A\xe4\xb8\xad") == 2,
                  "codepoint count should count Unicode scalar positions, not bytes");
}

} // namespace

int main()
{
    const bool ok = testDecodeValidUtf8()
        && testInvalidUtf8Replacement()
        && testAsciiFallbackKeepsShape();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
