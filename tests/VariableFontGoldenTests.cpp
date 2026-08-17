#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "wsc/wsc.h"

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 96;
constexpr std::uint64_t kNarrowHash = 8211557627910472001ull;
constexpr std::uint64_t kWideHash = 8341808591449791857ull;

struct Snapshot
{
    std::vector<unsigned char> rgba;
    std::uint64_t hash = 0;
    int inkPixels = 0;
    int left = kWidth;
    int right = -1;
    int top = kHeight;
    int bottom = -1;
};

std::uint64_t hashRgba(const std::vector<unsigned char> &pixels)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char value : pixels) {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ull;
    }
    return hash;
}

Snapshot render(float widthAxis)
{
    Snapshot snapshot;
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software,
                                      kWidth, kHeight);
    if (!canvas || !canvas->initializeContext()) return snapshot;
    if (!canvas->setTextBackend(wsc::Canvas::TextBackend::Portable)) {
        return snapshot;
    }

    wsc::FontFace face = wsc::FontFace::fromFile(
        wsc::FontDescriptor("VariableGolden"),
        WHATSCANVAS_TEST_VARIABLE_FONT);
    (void)face.setVariationCoordinate("wdth", 100.0f);
    if (!canvas->registerFontFace(face)) return snapshot;

    canvas->beginFrame();
    wsc::Paint background;
    background.setAntiAlias(false);
    background.setColor(wsc::Color::WHITE);
    canvas->drawRect(wsc::RectF(0.0f, 0.0f,
                                static_cast<float>(kWidth),
                                static_cast<float>(kHeight)), background);

    wsc::Paint text;
    text.setAntiAlias(true);
    text.setColor(wsc::Color::BLACK);
    text.setFontFamily("VariableGolden");
    text.setTextSize(52.0f);
    text.setFontVariation("wdth", widthAxis);
    canvas->drawText("Hamburgefontsiv 0123", 12.0f, 18.0f, text);
    canvas->endFrame();

    if (!canvas->readPixelsRGBA(snapshot.rgba)
        || snapshot.rgba.size()
            != static_cast<std::size_t>(kWidth) * kHeight * 4u) {
        snapshot.rgba.clear();
        return snapshot;
    }
    snapshot.hash = hashRgba(snapshot.rgba);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * kWidth
                 + static_cast<std::size_t>(x)) * 4u;
            if (snapshot.rgba[offset] >= 248
                && snapshot.rgba[offset + 1u] >= 248
                && snapshot.rgba[offset + 2u] >= 248) {
                continue;
            }
            ++snapshot.inkPixels;
            snapshot.left = std::min(snapshot.left, x);
            snapshot.right = std::max(snapshot.right, x);
            snapshot.top = std::min(snapshot.top, y);
            snapshot.bottom = std::max(snapshot.bottom, y);
        }
    }
    return snapshot;
}

bool updateRequested()
{
    const char *value = std::getenv("WHATSCANVAS_UPDATE_VARIABLE_FONT_HASHES");
    return value != nullptr && value[0] != '\0'
        && !(value[0] == '0' && value[1] == '\0');
}

} // namespace

int main()
{
    const Snapshot narrow = render(50.0f);
    const Snapshot wide = render(150.0f);
    if (narrow.rgba.empty() || wide.rgba.empty()
        || narrow.inkPixels == 0 || wide.inkPixels == 0) {
        std::cerr << "FAILED: variable-font golden scene did not render.\n";
        return EXIT_FAILURE;
    }

    std::size_t changedPixels = 0;
    for (std::size_t index = 0; index + 3u < narrow.rgba.size(); index += 4u) {
        if (!std::equal(narrow.rgba.begin() + static_cast<std::ptrdiff_t>(index),
                        narrow.rgba.begin() + static_cast<std::ptrdiff_t>(index + 4u),
                        wide.rgba.begin() + static_cast<std::ptrdiff_t>(index))) {
            ++changedPixels;
        }
    }

    std::cout << "VARIABLE_FONT_GOLDEN narrow_hash=" << narrow.hash
              << " wide_hash=" << wide.hash
              << " narrow_bounds=" << narrow.left << ',' << narrow.top << '-'
              << narrow.right << ',' << narrow.bottom
              << " wide_bounds=" << wide.left << ',' << wide.top << '-'
              << wide.right << ',' << wide.bottom
              << " changed_pixels=" << changedPixels << '\n';

    if (updateRequested()) {
        std::cout << "Copy the two printed hashes into kNarrowHash/kWideHash, "
                     "then rerun without WHATSCANVAS_UPDATE_VARIABLE_FONT_HASHES.\n";
        return EXIT_SUCCESS;
    }
    bool ok = true;
    if (narrow.hash != kNarrowHash) {
        std::cerr << "FAILED: narrow variable-font pixels changed; expected "
                  << kNarrowHash << ", got " << narrow.hash << ".\n";
        ok = false;
    }
    if (wide.hash != kWideHash) {
        std::cerr << "FAILED: wide variable-font pixels changed; expected "
                  << kWideHash << ", got " << wide.hash << ".\n";
        ok = false;
    }
    if (narrow.hash == wide.hash || changedPixels < 500u
        || wide.right <= narrow.right) {
        std::cerr << "FAILED: wdth axis did not produce a materially wider pixel scene.\n";
        ok = false;
    }
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
