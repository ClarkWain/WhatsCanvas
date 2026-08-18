#include "text/TextUtils.h"
#include "text/platform/AndroidFontConfig.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(
    const std::uint8_t *data, std::size_t size)
{
    // Keep a single input from monopolizing a worker while still allowing
    // complete real-world Android font configurations in the seed corpus.
    constexpr std::size_t kMaximumInputBytes = 1024u * 1024u;
    if (data == nullptr || size > kMaximumInputBytes) {
        return 0;
    }

    const std::string input(
        reinterpret_cast<const char *>(data), size);
    const auto decoded = wsc::text::decodeUtf8(input);
    const std::string normalized = wsc::text::normalizeUtf8ForText(input);
    (void)wsc::text::isValidUtf8(input);
    (void)wsc::text::countUtf8Codepoints(normalized);
    (void)wsc::text::buildTextBreakTokens(
        normalized, 0u, normalized.size());
    const auto clusters = wsc::text::buildFontFallbackClusters(
        normalized, 0u, normalized.size());
    for (const auto &cluster : clusters) {
        (void)wsc::text::classifyEmojiPresentation(cluster.codepoints);
        (void)wsc::text::encodeCodepointsToUtf16(cluster.codepoints);
    }

    // XML and arbitrary bytes share the same entry point so mutations can
    // cross the UTF-8/XML boundary and exercise error recovery as well as
    // valid Android system/vendor/product schemas.
    wsc::text::detail::AndroidFontConfig config;
    if (input.find('<') != std::string::npos) {
        (void)wsc::text::detail::parseAndroidFontConfig(
            input, "/system/fonts", config);
        for (const char *family : {"sans-serif", "serif", "emoji", ""}) {
            (void)wsc::text::detail::matchAndroidFontConfig(
                config, family, 400, wsc::FontSlant::NORMAL, "zh-CN");
        }
    }

    // Keep decoded live so optimizers cannot discard the decoder path.
    return decoded.size() > size + 1u ? 1 : 0;
}
