#include <wsc/wsc.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

// OpenGL is host-owned, so the benchmark needs a small context provider.  The
// CMake target can force this off/on; otherwise an available GLFW header is a
// useful build-time indication that the OpenGL benchmark can be compiled.
#if defined(WHATSCANVAS_FILTER_BENCHMARK_DISABLE_OPENGL)
#define WSC_FILTER_BENCH_HAS_OPENGL 0
#elif defined(WHATSCANVAS_FILTER_BENCHMARK_ENABLE_OPENGL) \
    || defined(WHATSCANVAS_BENCHMARK_ENABLE_OPENGL)
#define WSC_FILTER_BENCH_HAS_OPENGL 1
#elif defined(__has_include)
#if __has_include(<GLFW/glfw3.h>)
#define WSC_FILTER_BENCH_HAS_OPENGL 1
#else
#define WSC_FILTER_BENCH_HAS_OPENGL 0
#endif
#else
#define WSC_FILTER_BENCH_HAS_OPENGL 0
#endif

#if WSC_FILTER_BENCH_HAS_OPENGL
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

#if defined(WHATSCANVAS_FILTER_BENCHMARK_DISABLE_VULKAN)
#define WSC_FILTER_BENCH_HAS_VULKAN 0
#elif defined(WHATSCANVAS_FILTER_BENCHMARK_ENABLE_VULKAN) \
    || defined(WHATSCANVAS_BENCHMARK_ENABLE_VULKAN) \
    || defined(WHATSCANVAS_ENABLE_VULKAN)
#define WSC_FILTER_BENCH_HAS_VULKAN 1
#elif defined(__has_include)
#if __has_include(<vulkan/vulkan.h>)
#define WSC_FILTER_BENCH_HAS_VULKAN 1
#else
#define WSC_FILTER_BENCH_HAS_VULKAN 0
#endif
#else
#define WSC_FILTER_BENCH_HAS_VULKAN 0
#endif

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kDefaultFrames = 10;
constexpr int kDefaultWarmup = 3;
constexpr int kDefaultWidth = 960;
constexpr int kDefaultHeight = 540;
constexpr int kMaxDimension = 8192;
constexpr std::uint64_t kMaxPixels = 100000000;

enum class Backend
{
    Software,
    OpenGL,
    Vulkan,
};

struct Options
{
    Backend backend = Backend::Software;
    int frames = kDefaultFrames;
    int warmup = kDefaultWarmup;
    int width = kDefaultWidth;
    int height = kDefaultHeight;
};

struct SampleSummary
{
    double medianMs = 0.0;
    double p95Ms = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
};

void printUsage(std::ostream &out, const char *program)
{
    out << "Usage: " << program
        << " [--backend software|opengl|vulkan]"
        << " [--frames N] [--warmup N] [--width N] [--height N]\n";
}

bool parseNonNegativeInt(const std::string &text, int &value)
{
    if (text.empty()) {
        return false;
    }
    int parsed = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end || parsed < 0) {
        return false;
    }
    value = parsed;
    return true;
}

bool parseOptions(int argc, char **argv, Options &options, std::string &error)
{
    for (int i = 1; i < argc; ++i) {
        const std::string argument(argv[i]);
        if (argument == "--help" || argument == "-h") {
            printUsage(std::cout, argv[0]);
            std::exit(0);
        }
        if (i + 1 >= argc) {
            error = "missing value for " + argument;
            return false;
        }

        const std::string value(argv[++i]);
        if (argument == "--backend") {
            if (value == "software") {
                options.backend = Backend::Software;
            } else if (value == "opengl") {
                options.backend = Backend::OpenGL;
            } else if (value == "vulkan") {
                options.backend = Backend::Vulkan;
            } else {
                error = "invalid backend '" + value
                    + "' (expected software, opengl, or vulkan)";
                return false;
            }
            continue;
        }

        int parsed = 0;
        if (!parseNonNegativeInt(value, parsed)) {
            error = "invalid non-negative integer '" + value + "' for " + argument;
            return false;
        }
        if (argument == "--frames") {
            options.frames = parsed;
        } else if (argument == "--warmup") {
            options.warmup = parsed;
        } else if (argument == "--width") {
            options.width = parsed;
        } else if (argument == "--height") {
            options.height = parsed;
        } else {
            error = "unknown option '" + argument + "'";
            return false;
        }
    }

    if (options.frames <= 0) {
        error = "--frames must be greater than zero";
        return false;
    }
    if (options.width <= 0 || options.height <= 0) {
        error = "--width and --height must be greater than zero";
        return false;
    }
    if (options.width > kMaxDimension || options.height > kMaxDimension
        || static_cast<std::uint64_t>(options.width)
            * static_cast<std::uint64_t>(options.height) > kMaxPixels) {
        error = "requested image size is unreasonably large";
        return false;
    }
    return true;
}

const char *backendName(Backend backend)
{
    switch (backend) {
    case Backend::Software:
        return "software";
    case Backend::OpenGL:
        return "opengl";
    case Backend::Vulkan:
        return "vulkan";
    }
    return "unknown";
}

wsc::Canvas::Backend canvasBackend(Backend backend)
{
    switch (backend) {
    case Backend::Software:
        return wsc::Canvas::Backend::Software;
    case Backend::OpenGL:
        return wsc::Canvas::Backend::OpenGL;
    case Backend::Vulkan:
        return wsc::Canvas::Backend::Vulkan;
    }
    return wsc::Canvas::Backend::Software;
}

bool backendCompiled(Backend backend)
{
    switch (backend) {
    case Backend::Software:
        return true;
    case Backend::OpenGL:
        return WSC_FILTER_BENCH_HAS_OPENGL != 0;
    case Backend::Vulkan:
        return WSC_FILTER_BENCH_HAS_VULKAN != 0;
    }
    return false;
}

wsc::Paint solid(const wsc::Color &color, bool antiAlias = true)
{
    wsc::Paint paint;
    paint.setStyle(wsc::Paint::Style::FILL);
    paint.setColor(color);
    paint.setAntiAlias(antiAlias);
    return paint;
}

void drawBackground(wsc::Canvas &canvas, int width, int height)
{
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    wsc::Paint gradient;
    gradient.setStyle(wsc::Paint::Style::FILL);
    gradient.setLinearGradient(
        0.0f, 0.0f, w, h,
        wsc::Color(22, 36, 67, 255), wsc::Color(94, 35, 79, 255));
    canvas.drawRect(wsc::RectF(0.0f, 0.0f, w, h), gradient);

    const wsc::Color colors[] = {
        wsc::Color(36, 211, 196, 210),
        wsc::Color(255, 154, 80, 205),
        wsc::Color(188, 229, 74, 195),
        wsc::Color(103, 135, 255, 205),
    };
    for (int i = 0; i < 18; ++i) {
        const float x = static_cast<float>((i * 137) % 1000) * w / 1000.0f;
        const float y = static_cast<float>((i * 83 + 41) % 600) * h / 600.0f;
        const float radius = (24.0f + static_cast<float>((i * 17) % 58))
            * std::min(w / 960.0f, h / 540.0f);
        canvas.drawCircle(x, y, radius, solid(colors[i % 4]));
    }

    const float stripeWidth = std::max(2.0f, w / 80.0f);
    const wsc::Paint stripe = solid(wsc::Color(255, 255, 255, 38), false);
    for (float x = -h; x < w; x += stripeWidth * 5.0f) {
        wsc::Path path;
        path.moveTo(x, 0.0f);
        path.lineTo(x + stripeWidth, 0.0f);
        path.lineTo(x + h + stripeWidth, h);
        path.lineTo(x + h, h);
        path.close();
        canvas.drawPath(path, stripe);
    }
}

void drawOverlappingFrostedGlass(wsc::Canvas &canvas, int width, int height)
{
    drawBackground(canvas, width, height);
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float scale = std::min(w / 960.0f, h / 540.0f);
    const wsc::RectF panels[] = {
        wsc::RectF(0.10f * w, 0.15f * h, 0.43f * w, 0.43f * h),
        wsc::RectF(0.34f * w, 0.09f * h, 0.43f * w, 0.43f * h),
        wsc::RectF(0.18f * w, 0.43f * h, 0.43f * w, 0.43f * h),
        wsc::RectF(0.47f * w, 0.38f * h, 0.43f * w, 0.43f * h),
    };
    const wsc::Color tints[] = {
        wsc::Color(115, 239, 228, 42),
        wsc::Color(255, 177, 118, 42),
        wsc::Color(212, 239, 104, 42),
        wsc::Color(143, 161, 255, 42),
    };

    wsc::LayerOptions options;
    options.setBackdropFilter(
        wsc::ImageFilter::frostedGlass(12.0f * scale, 1.12f, 1.03f, 1.02f, 0.0f));
    const wsc::Paint layerPaint = solid(wsc::Color(255, 255, 255, 255));
    for (int i = 0; i < 4; ++i) {
        // Each layer is restored before the next capture, so overlapping
        // panels observe the fully composited result of all earlier panels.
        canvas.save();
        wsc::Path clip;
        clip.addRoundRect(panels[i], 24.0f * scale);
        canvas.clipPath(clip);
        canvas.saveLayer(panels[i], layerPaint, options);
        canvas.drawRect(panels[i], solid(tints[i]));
        canvas.restore();
        canvas.restore();
    }
}

void drawInnerShadowGrid(wsc::Canvas &canvas, int width, int height)
{
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float scale = std::min(w / 960.0f, h / 540.0f);
    canvas.drawColor(wsc::Color(24, 29, 42, 255));

    const float marginX = 0.055f * w;
    const float marginY = 0.075f * h;
    const float gapX = 0.018f * w;
    const float gapY = 0.026f * h;
    const float cellWidth = (w - 2.0f * marginX - 5.0f * gapX) / 6.0f;
    const float cellHeight = (h - 2.0f * marginY - 3.0f * gapY) / 4.0f;
    const wsc::Color fills[] = {
        wsc::Color(61, 174, 224, 255),
        wsc::Color(107, 201, 148, 255),
        wsc::Color(237, 164, 82, 255),
        wsc::Color(178, 119, 219, 255),
    };

    wsc::LayerOptions options;
    options.setImageFilter(wsc::ImageFilter::innerShadow(
        8.0f * scale, 3.0f * scale, 4.0f * scale,
        wsc::Color(5, 10, 22, 190)));
    const wsc::Paint layerPaint = solid(wsc::Color(255, 255, 255, 255));
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 6; ++column) {
            const wsc::RectF bounds(
                marginX + static_cast<float>(column) * (cellWidth + gapX),
                marginY + static_cast<float>(row) * (cellHeight + gapY),
                cellWidth, cellHeight);
            canvas.saveLayer(bounds, layerPaint, options);
            canvas.drawRoundRect(bounds, 12.0f * scale,
                                 solid(fills[(row + column) % 4]));
            canvas.restore();
        }
    }
}

#if WSC_FILTER_BENCH_HAS_OPENGL
class GlfwContext
{
public:
    ~GlfwContext()
    {
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
        }
        if (initialized_) {
            glfwTerminate();
        }
    }

    bool initialize(int width, int height, std::string &error)
    {
        if (!glfwInit()) {
            error = "glfwInit failed";
            return false;
        }
        initialized_ = true;
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
        window_ = glfwCreateWindow(width, height, "WhatsCanvas filter benchmark",
                                   nullptr, nullptr);
        if (window_ == nullptr) {
            error = "unable to create a hidden OpenGL 3.3 context";
            return false;
        }
        glfwMakeContextCurrent(window_);
        glfwSwapInterval(0);
        if (!wsc::Canvas::loadOpenGL(
                reinterpret_cast<wsc::Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
            error = "Canvas::loadOpenGL failed";
            return false;
        }
        finish_ = reinterpret_cast<FinishProc>(glfwGetProcAddress("glFinish"));
        if (finish_ == nullptr) {
            error = "unable to resolve glFinish";
            return false;
        }
        return true;
    }

    void finish() const { finish_(); }

private:
    using FinishProc = void (*)();
    GLFWwindow *window_ = nullptr;
    FinishProc finish_ = nullptr;
    bool initialized_ = false;
};
#endif

class BenchmarkContext
{
public:
    bool initialize(const Options &options, std::string &error)
    {
        if (!backendCompiled(options.backend)) {
            error = std::string("backend '") + backendName(options.backend)
                + "' was not compiled into this benchmark";
            return false;
        }
#if WSC_FILTER_BENCH_HAS_OPENGL
        if (options.backend == Backend::OpenGL
            && !glfw_.initialize(options.width, options.height, error)) {
            return false;
        }
#endif
        const wsc::Canvas::Backend requested = canvasBackend(options.backend);
        if (!wsc::Canvas::isBackendAvailable(requested)) {
            error = std::string("backend '") + backendName(options.backend)
                + "' is unavailable in this build or on this host";
            return false;
        }
        canvas_ = wsc::Canvas::create(requested, options.width, options.height);
        if (!canvas_) {
            error = std::string("Canvas::create failed for backend '")
                + backendName(options.backend) + "'";
            return false;
        }
        if (!canvas_->initializeContext()) {
            error = std::string("Canvas context initialization failed for backend '")
                + backendName(options.backend) + "'";
            return false;
        }
        canvas_->setSize(options.width, options.height);
#if WSC_FILTER_BENCH_HAS_OPENGL
        if (options.backend == Backend::OpenGL
            && !canvas_->setOutputTarget(wsc::OutputTarget::GLFramebuffer(
                0, options.width, options.height, true))) {
            error = "failed to configure the hidden OpenGL framebuffer";
            return false;
        }
#endif
        backend_ = options.backend;
        return true;
    }

    wsc::Canvas &canvas() { return *canvas_; }

    void finishFrame() const
    {
#if WSC_FILTER_BENCH_HAS_OPENGL
        if (backend_ == Backend::OpenGL) {
            // GPU completion belongs to the timed frame; readback does not.
            glfw_.finish();
        }
#endif
    }

private:
#if WSC_FILTER_BENCH_HAS_OPENGL
    // Declared before the Canvas so reverse destruction keeps the GL context
    // alive while Canvas releases its backend resources.
    GlfwContext glfw_;
#endif
    std::unique_ptr<wsc::Canvas> canvas_;
    Backend backend_ = Backend::Software;
};

SampleSummary summarize(std::vector<double> samples)
{
    std::sort(samples.begin(), samples.end());
    SampleSummary result;
    result.minMs = samples.front();
    result.maxMs = samples.back();
    const std::size_t middle = samples.size() / 2u;
    result.medianMs = (samples.size() % 2u) != 0u
        ? samples[middle]
        : (samples[middle - 1u] + samples[middle]) * 0.5;
    const std::size_t p95Index = std::min(
        samples.size() - 1u,
        static_cast<std::size_t>((samples.size() * 95u + 99u) / 100u - 1u));
    result.p95Ms = samples[p95Index];
    return result;
}

std::uint64_t hashPixels(const std::vector<unsigned char> &pixels)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned char byte : pixels) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

std::string formatHash(std::uint64_t hash)
{
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(16) << hash;
    return text.str();
}

using DrawWorkload = void (*)(wsc::Canvas &, int, int);

bool runWorkload(BenchmarkContext &context, const Options &options,
                 const char *workloadName, DrawWorkload draw, std::string &error)
{
    wsc::Canvas &canvas = context.canvas();
    auto renderFrame = [&]() {
        canvas.beginFrame();
        draw(canvas, options.width, options.height);
        canvas.endFrame();
        context.finishFrame();
    };

    for (int i = 0; i < options.warmup; ++i) {
        renderFrame();
    }

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(options.frames));
    for (int i = 0; i < options.frames; ++i) {
        const Clock::time_point start = Clock::now();
        renderFrame();
        const Clock::time_point end = Clock::now();
        samples.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
    }

    // Diagnostics and readback are intentionally outside every timed interval.
    const wsc::Canvas::RenderStats stats = canvas.getRenderStats();
    std::vector<unsigned char> pixels;
    const std::size_t expectedBytes = static_cast<std::size_t>(options.width)
        * static_cast<std::size_t>(options.height) * 4u;
    if (!canvas.readPixelsRGBA(pixels) || pixels.size() != expectedBytes) {
        error = std::string("pixel readback failed for workload '")
            + workloadName + "'";
        return false;
    }

    const SampleSummary summary = summarize(std::move(samples));
    const double fps = summary.medianMs > 0.0
        ? 1000.0 / summary.medianMs
        : std::numeric_limits<double>::infinity();
    std::cout << std::fixed << std::setprecision(3)
              << "FILTER_BENCHMARK"
              << " backend=" << backendName(options.backend)
              << " workload=" << workloadName
              << " size=" << options.width << 'x' << options.height
              << " frames=" << options.frames
              << " warmup=" << options.warmup
              << " median_ms=" << summary.medianMs
              << " p95_ms=" << summary.p95Ms
              << " min_ms=" << summary.minMs
              << " max_ms=" << summary.maxMs
              << " fps=" << fps
              << " hash=" << formatHash(hashPixels(pixels))
              << " filterCount=" << stats.filterCount
              << " filterPassCount=" << stats.filterPassCount
              << " downsampledFilterCount=" << stats.downsampledFilterCount
              << " filterInputPixelCount=" << stats.filterInputPixelCount
              << " filterPixelPassCount=" << stats.filterPixelPassCount
              << '\n';
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    Options options;
    std::string error;
    if (!parseOptions(argc, argv, options, error)) {
        std::cerr << "FILTER_BENCHMARK_ERROR message=\"" << error << "\"\n";
        printUsage(std::cerr, argv[0]);
        return 2;
    }

    try {
        BenchmarkContext context;
        if (!context.initialize(options, error)) {
            std::cerr << "FILTER_BENCHMARK_ERROR backend="
                      << backendName(options.backend)
                      << " message=\"" << error << "\"\n";
            return 3;
        }
        if (!runWorkload(context, options, "overlapping_frosted_glass",
                         drawOverlappingFrostedGlass, error)
            || !runWorkload(context, options, "inner_shadow_grid",
                            drawInnerShadowGrid, error)) {
            std::cerr << "FILTER_BENCHMARK_ERROR backend="
                      << backendName(options.backend)
                      << " message=\"" << error << "\"\n";
            return 4;
        }
    } catch (const std::exception &exception) {
        std::cerr << "FILTER_BENCHMARK_ERROR backend="
                  << backendName(options.backend)
                  << " message=\"" << exception.what() << "\"\n";
        return 5;
    } catch (...) {
        std::cerr << "FILTER_BENCHMARK_ERROR backend="
                  << backendName(options.backend)
                  << " message=\"unknown exception\"\n";
        return 5;
    }
    return 0;
}
