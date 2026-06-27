#include "wsc/wsc.h"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

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

} // namespace

int main()
{
    const int iterations = benchmarkIterations();
    std::cout << "WHATSCANVAS_CORE_BENCHMARKS iterations=" << iterations << std::endl;
    benchmarkTextLayout(iterations);
    benchmarkPathMetrics(iterations);
    benchmarkPixelHash(std::max(1, iterations / 100));
    benchmarkCommandRecording(iterations);
    return 0;
}
