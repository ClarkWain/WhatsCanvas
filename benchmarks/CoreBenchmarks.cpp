#include <wsc/FontSystem.h>

#include <wsc/CanvasStats.h>

#include "wsc/wsc.h"

#include "command/DrawCommand.h"
#include "render/IRenderer.h"
#include "text/BasicTextBackend.h"
#include "text/FontRasterizer.h"
#include "text/ITextBackend.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

class BenchmarkImageResource final : public ImageResource
{
public:
    BenchmarkImageResource(int width, int height, std::vector<unsigned char> pixels)
        : width_(width), height_(height), pixels_(std::move(pixels))
    {
    }

    bool isValid() const override { return width_ > 0 && height_ > 0; }
    void bind(const RenderContext &) const override {}

    bool updateRGBA(int x, int y, int width, int height, const unsigned char *pixels,
                    bool) override
    {
        if (pixels == nullptr || x < 0 || y < 0 || width <= 0 || height <= 0
            || x + width > width_ || y + height > height_) {
            return false;
        }

        for (int row = 0; row < height; ++row) {
            const std::size_t dst = (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(width_)
                + static_cast<std::size_t>(x)) * 4u;
            const std::size_t src = static_cast<std::size_t>(row) * static_cast<std::size_t>(width) * 4u;
            std::copy(pixels + src, pixels + src + static_cast<std::size_t>(width) * 4u,
                      pixels_.begin() + static_cast<std::ptrdiff_t>(dst));
        }
        return true;
    }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<unsigned char> pixels_;
};

class BenchmarkRenderer final : public IRenderer
{
public:
    void initializeBackend() override { initialized = true; }
    void finalizeBackend() override { initialized = false; }
    void setViewport(int width, int height) override
    {
        viewportWidth = width;
        viewportHeight = height;
    }

    void submit(std::unique_ptr<Command> &&command) override { commands.push_back(std::move(command)); }
    size_t commandCount() const override { return commands.size(); }

    std::vector<std::unique_ptr<Command>> takeCommandsFrom(size_t index) override
    {
        std::vector<std::unique_ptr<Command>> taken;
        if (index >= commands.size()) {
            return taken;
        }

        taken.reserve(commands.size() - index);
        for (size_t i = index; i < commands.size(); ++i) {
            taken.push_back(std::move(commands[i]));
        }
        commands.erase(commands.begin() + static_cast<std::ptrdiff_t>(index), commands.end());
        return taken;
    }

    void appendCommands(std::vector<std::unique_ptr<Command>> &&appended) override
    {
        for (auto &command : appended) {
            commands.push_back(std::move(command));
        }
    }

    bool readPixelsRGBA(std::vector<unsigned char> &) const override { return false; }
    SharedClipMaskResource createClipMaskResource(const ClipMaskPath &) const override { return {}; }

    SharedImageResource createImageResourceRGBA(int width, int height, const std::vector<unsigned char> &pixels) const override
    {
        ++createdImageCount;
        return std::make_shared<BenchmarkImageResource>(width, height, pixels);
    }

    SharedImageResource createImageResourceFromImageData(int width, int height, int, const unsigned char *pixels,
                                                         bool) const override
    {
        if (pixels == nullptr || width <= 0 || height <= 0) {
            return {};
        }
        ++createdImageCount;
        const std::size_t size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
        return std::make_shared<BenchmarkImageResource>(
            width, height, std::vector<unsigned char>(pixels, pixels + size));
    }

    bool updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width, int height,
                                 const unsigned char *pixels, bool regenerateMipmaps) const override
    {
        ++updatedImageCount;
        return imageResource && imageResource->updateRGBA(x, y, width, height, pixels, regenerateMipmaps);
    }

    SharedImageResource wrapExternalImageResource(ImageResourceHandle) const override { return {}; }
    const FrameStats &frameStats() const override { return stats; }
    void resetFrameStats() override { stats.reset(); }
    RenderResourceStats resourceStats() const override { return {}; }

    SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &,
                                                      const OffscreenRenderRequest &) const override
    {
        return {};
    }

    void resetRenderState() override {}
    void clear() override { commands.clear(); }

    void flush() override
    {
        stats.commandCount += commands.size();
        stats.drawCallCount += commands.size();
        commands.clear();
        ++flushCount;
    }

    bool initialized = false;
    int viewportWidth = 0;
    int viewportHeight = 0;
    std::vector<std::unique_ptr<Command>> commands;
    mutable FrameStats stats;
    mutable std::size_t createdImageCount = 0;
    mutable std::size_t updatedImageCount = 0;
    std::size_t flushCount = 0;
};

} // namespace

namespace wsc {

class CanvasLifecycleTestAccess
{
public:
    static std::unique_ptr<Canvas> create(std::unique_ptr<IRenderer> renderer)
    {
        return std::unique_ptr<Canvas>(new Canvas(std::move(renderer)));
    }
};

} // namespace wsc

namespace {

int benchmarkIterations()
{
    const char *value = std::getenv("WHATSCANVAS_BENCHMARK_ITERATIONS");
    if (value == nullptr) {
        return 10000;
    }

    const int parsed = std::atoi(value);
    return parsed > 0 ? parsed : 10000;
}

template <typename Func>
double timeMilliseconds(Func &&func)
{
    const auto start = Clock::now();
    func();
    const auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void printMetric(const std::string &name, int iterations, double totalMs)
{
    const double perIterationUs = totalMs * 1000.0 / static_cast<double>(iterations);
    const double iterationsPerSecond = static_cast<double>(iterations) * 1000.0 / totalMs;
    std::cout << "BENCHMARK name=" << name
              << " iterations=" << iterations
              << " total_ms=" << std::fixed << std::setprecision(3) << totalMs
              << " per_iteration_us=" << std::fixed << std::setprecision(3) << perIterationUs
              << " iterations_per_second=" << std::fixed << std::setprecision(1) << iterationsPerSecond
              << std::endl;
}

void printSkippedMetric(const std::string &name, const std::string &reason)
{
    std::cout << "BENCHMARK_SKIPPED name=" << name << " reason=\"" << reason << "\"" << std::endl;
}

std::optional<wsc::FontFace> findBenchmarkFont()
{
    const auto installed = wsc::FontSystem::discoverInstalledFontFaces();
    const char *preferredFamilies[] = {"Arial", "Segoe UI", "DejaVu Sans", "Helvetica"};
    for (const char *family : preferredFamilies) {
        const wsc::FontFace *best = nullptr;
        int bestDistance = std::numeric_limits<int>::max();
        for (const wsc::FontFace &face : installed) {
            if (face.family() == family && face.slant() == wsc::FontSlant::NORMAL) {
                const int distance = std::abs(face.weight() - 400);
                if (distance < bestDistance) {
                    best = &face;
                    bestDistance = distance;
                }
            }
        }
        if (best != nullptr) return *best;
    }
    const wsc::FontFace *best = nullptr;
    int bestDistance = std::numeric_limits<int>::max();
    for (const wsc::FontFace &face : installed) {
        if (face.slant() != wsc::FontSlant::NORMAL) continue;
        const int distance = std::abs(face.weight() - 400);
        if (distance < bestDistance) {
            best = &face;
            bestDistance = distance;
        }
    }
    if (best != nullptr) return *best;
    return std::nullopt;
}

wsc::FontFace benchmarkFace(const wsc::FontFace &source, const char *family)
{
    return wsc::FontFace::fromFile(wsc::FontDescriptor(family), source.path(), source.faceIndex());
}

void benchmarkTextLayout(int iterations)
{
    auto canvasOwner = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, 0, 0);
    wsc::Canvas &canvas = *canvasOwner;
    wsc::Paint paint;
    paint.setTextSize(16.0f);
    paint.setLetterSpacing(0.5f);
    const std::string text =
        "WhatsCanvas text layout benchmark mixes ASCII words with wrapped rows and ellipsis.";
    const wsc::RectF bounds(0.0f, 0.0f, 260.0f, 120.0f);
    std::size_t totalLines = 0;
    const double ms = timeMilliseconds([&]() {
        for (int i = 0; i < iterations; ++i) {
            const auto lines = canvas.layoutTextBox(text, bounds, 20.0f, 4, true, paint);
            totalLines += lines.size();
        }
    });
    printMetric("text_layout", iterations, ms);
    std::cout << "BENCHMARK_DETAIL name=text_layout total_lines=" << totalLines << std::endl;
}

void benchmarkTextCacheHitPath(int iterations)
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createBasicTextBackend();
    wsc::Paint paint;
    paint.setTextSize(18.0f);
    paint.setFontFamily("Arial");

    const std::string text = "Cached text render path";
    (void)backend->measureTextWidth(text, paint);
    (void)backend->renderText(text, 0.0f, 0.0f, paint);

    std::size_t renderedBytes = 0;
    const double cachedMs = timeMilliseconds([&]() {
        for (int i = 0; i < iterations; ++i) {
            const auto result = backend->renderText(text, 0.0f, 0.0f, paint);
            renderedBytes += result.bitmapPixels.size() + result.vertices.size() * sizeof(float);
        }
    });

    printMetric("text_cache_hit_path", iterations, cachedMs);
    std::cout << "BENCHMARK_DETAIL name=text_cache_hit_path rendered_bytes=" << renderedBytes << std::endl;
}

void benchmarkFontGlyphMetrics(int iterations)
{
    const auto systemFont = findBenchmarkFont();
    if (!systemFont) {
        printSkippedMetric("font_glyph_metrics_cache", "no benchmark font found");
        return;
    }

    wsc::text::FontRasterizer rasterizer;
    rasterizer.clearCache();
    rasterizer.setCacheCapacity(16);
    const wsc::FontFace face = benchmarkFace(*systemFont, "BenchmarkFontMetrics");
    const std::string glyphs = "WhatsCanvasTypography12345";
    float accumulatedAdvance = 0.0f;

    const double ms = timeMilliseconds([&]() {
        for (int i = 0; i < iterations; ++i) {
            const unsigned char ch = static_cast<unsigned char>(glyphs[static_cast<std::size_t>(i) % glyphs.size()]);
            const auto metrics = rasterizer.glyphMetrics(face, ch, 18.0f + static_cast<float>(i % 5));
            if (metrics) {
                accumulatedAdvance += metrics->advanceX;
            }
        }
    });

    const wsc::text::FontRasterizerCacheStats stats = rasterizer.cacheStats();
    printMetric("font_glyph_metrics_cache", iterations, ms);
    std::cout << "BENCHMARK_DETAIL name=font_glyph_metrics_cache accumulated_advance="
              << accumulatedAdvance
              << " cache_faces=" << stats.faceCount
              << " cache_hits=" << stats.hitCount
              << " cache_misses=" << stats.missCount
              << " cache_evictions=" << stats.evictionCount
              << std::endl;
}

void benchmarkFontGlyphRasterize(int iterations)
{
    const auto systemFont = findBenchmarkFont();
    if (!systemFont) {
        printSkippedMetric("font_glyph_rasterize", "no benchmark font found");
        return;
    }

    wsc::text::FontRasterizer rasterizer;
    rasterizer.clearCache();
    rasterizer.setCacheCapacity(16);
    const wsc::FontFace face = benchmarkFace(*systemFont, "BenchmarkFontRaster");
    const std::string glyphs = "GlyphAtlas";
    std::size_t totalPixels = 0;

    const double ms = timeMilliseconds([&]() {
        for (int i = 0; i < iterations; ++i) {
            const unsigned char ch = static_cast<unsigned char>(glyphs[static_cast<std::size_t>(i) % glyphs.size()]);
            const auto glyph = rasterizer.rasterizeGlyph(face, ch, 24.0f);
            if (glyph) {
                totalPixels += glyph->bitmap.alphaPixels.size() + glyph->bitmap.rgbaPixels.size();
            }
        }
    });

    const wsc::text::FontRasterizerCacheStats stats = rasterizer.cacheStats();
    printMetric("font_glyph_rasterize", iterations, ms);
    std::cout << "BENCHMARK_DETAIL name=font_glyph_rasterize total_pixels="
              << totalPixels
              << " cache_faces=" << stats.faceCount
              << " cache_hits=" << stats.hitCount
              << " cache_misses=" << stats.missCount
              << " cache_evictions=" << stats.evictionCount
              << std::endl;
}

void benchmarkPortableGlyphAtlasText(int iterations)
{
    const auto systemFont = findBenchmarkFont();
    if (!systemFont) {
        printSkippedMetric("portable_glyph_atlas_text", "no benchmark font found");
        return;
    }

    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    (void)backend->registerFontFace(benchmarkFace(*systemFont, "BenchmarkAtlasFont"));

    wsc::Paint paint;
    paint.setTextSize(22.0f);
    paint.setFontFamily("BenchmarkAtlasFont");
    paint.setLetterSpacing(0.25f);
    const std::string text = "Glyph atlas text benchmark 12345";
    std::size_t totalQuads = 0;
    std::size_t totalDirtyRects = 0;

    const double ms = timeMilliseconds([&]() {
        for (int i = 0; i < iterations; ++i) {
            const wsc::text::TextRenderResult result =
                backend->renderText(text, static_cast<float>(i % 7), 0.0f, paint);
            totalQuads += result.glyphAtlasQuads.size();
            totalDirtyRects += result.atlasDirtyRects.size();
        }
    });

    printMetric("portable_glyph_atlas_text", iterations, ms);
    std::cout << "BENCHMARK_DETAIL name=portable_glyph_atlas_text total_quads="
              << totalQuads
              << " total_dirty_rects=" << totalDirtyRects
              << std::endl;
}

void benchmarkPathMetrics(int iterations)
{
    wsc::Path path;
    path.moveTo(20.0f, 20.0f);
    path.cubicTo(80.0f, 0.0f, 160.0f, 120.0f, 220.0f, 40.0f);
    path.quadTo(280.0f, 140.0f, 360.0f, 80.0f);
    path.lineTo(420.0f, 160.0f);

    float accumulated = 0.0f;
    const double ms = timeMilliseconds([&]() {
        for (int i = 0; i < iterations; ++i) {
            const float length = path.length();
            wsc::PointF midpoint;
            accumulated += length;
            if (path.pointAtLength(length * 0.5f, midpoint)) {
                accumulated += midpoint.getX();
            }
        }
    });
    printMetric("path_metrics", iterations, ms);
    std::cout << "BENCHMARK_DETAIL name=path_metrics accumulated=" << accumulated << std::endl;
}

void benchmarkPixelHash(int iterations)
{
    std::vector<unsigned char> pixels(800u * 600u * 4u);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<unsigned char>((i * 31u + i / 7u) & 0xffu);
    }

    std::uint64_t hash = 0;
    const double ms = timeMilliseconds([&]() {
        for (int i = 0; i < iterations; ++i) {
            hash ^= wsc::Canvas::hashPixelsRGBA(pixels);
        }
    });
    printMetric("pixel_hash_rgba_800x600", iterations, ms);
    std::cout << "BENCHMARK_DETAIL name=pixel_hash_rgba_800x600 folded_hash=" << hash << std::endl;
}

void benchmarkCommandRecording(int iterations)
{
    auto canvasOwner = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, 0, 0);
    wsc::Canvas &canvas = *canvasOwner;
    canvas.setSize(800, 600);

    wsc::Paint paint;
    paint.setStyle(wsc::Paint::Style::FILL);
    paint.setColor(wsc::Color(20, 120, 220, 200));

    const double ms = timeMilliseconds([&]() {
        for (int i = 0; i < iterations; ++i) {
            const float x = static_cast<float>((i * 17) % 760);
            const float y = static_cast<float>((i * 29) % 560);
            canvas.drawRect(wsc::RectF(x, y, 32.0f, 24.0f), paint);
        }
    });
    printMetric("command_record_rect", iterations, ms);
    canvas.releaseResources();
}

void benchmarkImageUpload(int iterations)
{
    auto renderer = std::make_unique<BenchmarkRenderer>();
    BenchmarkRenderer *rawRenderer = renderer.get();
    std::unique_ptr<wsc::Canvas> canvas = wsc::CanvasLifecycleTestAccess::create(std::move(renderer));
    canvas->setSize(128, 128);
    canvas->initializeContext();

    constexpr int kWidth = 64;
    constexpr int kHeight = 64;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight) * 4u);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<unsigned char>((i * 13u + i / 3u) & 0xffu);
    }

    wsc::Image image;
    const double ms = timeMilliseconds([&]() {
        for (int i = 0; i < iterations; ++i) {
            pixels[0] = static_cast<unsigned char>(i & 0xff);
            image.replacePixelsRGBA(*canvas, pixels, kWidth, kHeight, false);
        }
    });

    printMetric("image_upload_rgba_64x64", iterations, ms);
    std::cout << "BENCHMARK_DETAIL name=image_upload_rgba_64x64 created_images="
              << rawRenderer->createdImageCount << std::endl;
}

void benchmarkFrameFlush(int iterations)
{
    auto renderer = std::make_unique<BenchmarkRenderer>();
    BenchmarkRenderer *rawRenderer = renderer.get();
    std::unique_ptr<wsc::Canvas> canvas = wsc::CanvasLifecycleTestAccess::create(std::move(renderer));
    canvas->setSize(320, 240);
    canvas->initializeContext();

    wsc::Paint paint;
    paint.setColor(wsc::Color(40, 120, 220, 180));

    const double ms = timeMilliseconds([&]() {
        for (int i = 0; i < iterations; ++i) {
            canvas->drawRect(wsc::RectF(0.0f, 0.0f, 24.0f, 18.0f), paint);
            canvas->endFrame();
        }
    });

    printMetric("frame_flush_single_rect", iterations, ms);
    std::cout << "BENCHMARK_DETAIL name=frame_flush_single_rect flushes="
              << rawRenderer->flushCount << std::endl;
}

void benchmarkSoftwareBackdropBlur(int iterations)
{
    const int filterIterations = std::max(1, iterations / 5000);
    std::unique_ptr<wsc::Canvas> canvas =
        wsc::Canvas::create(wsc::Canvas::Backend::Software, 320, 180);
    if (!canvas || !canvas->initializeContext()) {
        printSkippedMetric("software_backdrop_blur_320x180_r24",
                           "software canvas initialization failed");
        return;
    }
    canvas->setSize(320, 180);

    wsc::Paint black;
    black.setStyle(wsc::Paint::Style::FILL);
    black.setColor(wsc::Color(0, 0, 0, 255));
    black.setAntiAlias(false);
    wsc::Paint white = black;
    white.setColor(wsc::Color(255, 255, 255, 255));
    wsc::LayerOptions options;
    options.setBackdropFilter(wsc::ImageFilter::blur(24.0f));

    const double ms = timeMilliseconds([&]() {
        for (int i = 0; i < filterIterations; ++i) {
            canvas->beginFrame();
            canvas->drawRect(wsc::RectF(0.0f, 0.0f, 160.0f, 180.0f), black);
            canvas->drawRect(wsc::RectF(160.0f, 0.0f, 160.0f, 180.0f), white);
            canvas->saveLayer(wsc::RectF(0.0f, 0.0f, 320.0f, 180.0f), white, options);
            canvas->restore();
            canvas->endFrame();
        }
    });

    const wsc::Canvas::RenderStats stats = canvas->getRenderStats();
    printMetric("software_backdrop_blur_320x180_r24", filterIterations, ms);
    std::cout << "BENCHMARK_DETAIL name=software_backdrop_blur_320x180_r24"
              << " filter_count=" << stats.filterCount
              << " filter_passes=" << stats.filterPassCount
              << " input_pixels=" << stats.filterInputPixelCount
              << " pixel_passes=" << stats.filterPixelPassCount
              << std::endl;
}

} // namespace

int main()
{
    const int iterations = benchmarkIterations();
    std::cout << "WHATSCANVAS_CORE_BENCHMARKS iterations=" << iterations << std::endl;
    benchmarkTextLayout(iterations);
    benchmarkTextCacheHitPath(iterations);
    benchmarkFontGlyphMetrics(iterations);
    benchmarkFontGlyphRasterize(std::max(1, iterations / 10));
    benchmarkPortableGlyphAtlasText(std::max(1, iterations / 10));
    benchmarkPathMetrics(iterations);
    benchmarkPixelHash(std::max(1, iterations / 100));
    benchmarkCommandRecording(iterations);
    benchmarkImageUpload(std::max(1, iterations / 10));
    benchmarkFrameFlush(iterations);
    benchmarkSoftwareBackdropBlur(iterations);
    return 0;
}
