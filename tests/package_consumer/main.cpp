#include <wsc/wsc.h>
#include <wsc/FontResolver.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

int main()
{
    wsc::Paint paint;
    paint.setColor(wsc::Color(32, 96, 192, 255));
    paint.setAntiAlias(true);
    paint.setTextSize(18.0f);
    paint.setFontFamily("Inter");

    wsc::Path path;
    path.moveTo(0.0f, 0.0f);
    path.lineTo(24.0f, 0.0f);
    path.lineTo(24.0f, 24.0f);
    path.close();

    wsc::FontFace face = wsc::FontFace::fromMemory(
        wsc::FontDescriptor("MemoryFont"),
        std::vector<std::uint8_t>{0, 1, 2, 3});
    if (!face.isValid()) {
        return 1;
    }

    wsc::FontFallbackChain chain("MemoryFont");
    chain.addFallbackFamily("sans-serif");
    if (chain.familiesInResolutionOrder().empty()) {
        return 2;
    }

    const auto backend =
#if defined(WHATSCANVAS_PACKAGE_USE_SOFTWARE)
        wsc::Canvas::Backend::Software;
#else
        wsc::Canvas::Backend::OpenGL;
#endif
    auto canvasOwner = wsc::Canvas::create(backend, 0, 0);
    if (!canvasOwner) {
        return 3;
    }
    wsc::Canvas &canvas = *canvasOwner;
    canvas.setSize(64, 64);
    auto assets = std::make_shared<wsc::LazyFontProvider>(
        wsc::FontProviderKind::ASSET, "consumer-assets",
        [](const std::string &)
            -> std::optional<std::vector<std::uint8_t>> {
            return std::nullopt;
        });
    wsc::LazyFontSource lazySource;
    lazySource.descriptor = wsc::FontDescriptor("Consumer Lazy");
    lazySource.sourceId = "fonts/consumer.ttf";
    if (!assets->registerSource(lazySource)
        || !canvas.addFontProvider(assets)) {
        return 4;
    }
    auto remote = std::make_shared<wsc::RemoteFontProvider>(
        wsc::FontProviderKind::DYNAMIC, "consumer-remote");
    wsc::RemoteFontSource remoteSource;
    remoteSource.font.descriptor = wsc::FontDescriptor("Consumer Remote");
    remoteSource.font.sourceId = "https://fonts.example/consumer.ttf";
    remoteSource.font.codepointRanges.emplace_back(0x4E00, 0x9FFF);
    remoteSource.expectedBytes = 1024;
    if (!remote->registerSource(remoteSource)
        || !canvas.addFontProvider(remote)) {
        return 5;
    }
    const auto changedFamilies = remote->takeChangedFamilies();
    if (changedFamilies.size() != 1
        || changedFamilies.front() != "Consumer Remote") {
        return 6;
    }
    wsc::FontMatchRequest remoteMatch;
    remoteMatch.family = "Consumer Remote";
    remoteMatch.codepoints = {0x4E2D};
    (void)remote->match(remoteMatch);
    const auto remoteRequests = remote->takeDownloadRequests();
    if (remoteRequests.size() != 1
        || remoteRequests.front().sourceId != remoteSource.font.sourceId) {
        return 7;
    }
    (void)canvas.getWidth();
    (void)paint;
    (void)path;
    return 0;
}
