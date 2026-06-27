#include "wsc/wsc.h"

#include "command/DrawCommand.h"
#include "render/IRenderer.h"
#include "text/BasicTextBackend.h"
#include "text/ITextBackend.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
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

void benchmarkTextLayout(int iterations)
{
    wsc::Canvas canvas;
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
    wsc::Canvas canvas;
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
            canvas->flush();
        }
    });

    printMetric("frame_flush_single_rect", iterations, ms);
    std::cout << "BENCHMARK_DETAIL name=frame_flush_single_rect flushes="
              << rawRenderer->flushCount << std::endl;
}

} // namespace

int main()
{
    const int iterations = benchmarkIterations();
    std::cout << "WHATSCANVAS_CORE_BENCHMARKS iterations=" << iterations << std::endl;
    benchmarkTextLayout(iterations);
    benchmarkTextCacheHitPath(iterations);
    benchmarkPathMetrics(iterations);
    benchmarkPixelHash(std::max(1, iterations / 100));
    benchmarkCommandRecording(iterations);
    benchmarkImageUpload(std::max(1, iterations / 10));
    benchmarkFrameFlush(iterations);
    return 0;
}
