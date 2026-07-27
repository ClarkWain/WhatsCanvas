#include <wsc/wsc.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

#if defined(WHATSCANVAS_PERF_DISABLE_OPENGL)
#define WSC_PERF_HAS_OPENGL 0
#else
#define WSC_PERF_HAS_OPENGL 1
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

#if defined(WHATSCANVAS_PERF_ENABLE_VULKAN)
#define WSC_PERF_HAS_VULKAN 1
#include <vulkan/vulkan.h>
#else
#define WSC_PERF_HAS_VULKAN 0
#endif

#ifndef WHATSCANVAS_PERF_BUILD_TYPE
#define WHATSCANVAS_PERF_BUILD_TYPE "unknown"
#endif

#ifndef WHATSCANVAS_PERF_VERSION
#define WHATSCANVAS_PERF_VERSION "unknown"
#endif

#ifndef WHATSCANVAS_PERF_CONTRACT_FONT
#define WHATSCANVAS_PERF_CONTRACT_FONT ""
#endif

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kDefaultWidth = 1920;
constexpr int kDefaultHeight = 1080;
constexpr int kMaxDimension = 8192;
constexpr std::uint64_t kMaxPixels = UINT64_C(100000000);
constexpr int kSchemaVersion = 1;
constexpr float kPi = 3.14159265358979323846f;

enum class Backend
{
    Software,
    OpenGL,
    Vulkan,
};

struct Options
{
    Backend backend = Backend::Software;
    int frames = 30;
    int warmup = 5;
    int width = kDefaultWidth;
    int height = kDefaultHeight;
    std::string profile = "standard";
    std::string sceneFilter = "all";
    std::string outputPath;
    std::string captureDirectory;
    bool listScenes = false;
};

struct Distribution
{
    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
    double median = 0.0;
    double p90 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double standardDeviation = 0.0;
    double coefficientOfVariation = 0.0;
};

struct ProcessMemory
{
    std::uint64_t residentBytes = 0;
    std::uint64_t peakResidentBytes = 0;
    std::uint64_t privateOrVirtualBytes = 0;
};

struct SceneResources
{
    wsc::Image image;
    bool contractFontReady = false;
};

using DrawScene = void (*)(
    wsc::Canvas &, SceneResources &, int, int, int);

struct Scene
{
    const char *name = "";
    const char *category = "";
    const char *cacheMode = "";
    std::size_t operationsPerFrame = 0;
    DrawScene draw = nullptr;
};

struct FrameTimings
{
    double recordMs = 0.0;
    double submitMs = 0.0;
    double totalMs = 0.0;
};

std::string jsonEscape(std::string_view input)
{
    std::ostringstream output;
    for (const unsigned char ch : input) {
        switch (ch) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (ch < 0x20u) {
                output << "\\u"
                       << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(ch)
                       << std::dec;
            } else {
                output << static_cast<char>(ch);
            }
            break;
        }
    }
    return output.str();
}

std::string environmentValue(const char *name)
{
    const char *value = std::getenv(name);
    return value != nullptr ? value : "";
}

const char *backendName(Backend backend)
{
    switch (backend) {
    case Backend::Software: return "software";
    case Backend::OpenGL: return "opengl";
    case Backend::Vulkan: return "vulkan";
    }
    return "unknown";
}

wsc::Canvas::Backend canvasBackend(Backend backend)
{
    switch (backend) {
    case Backend::Software: return wsc::Canvas::Backend::Software;
    case Backend::OpenGL: return wsc::Canvas::Backend::OpenGL;
    case Backend::Vulkan: return wsc::Canvas::Backend::Vulkan;
    }
    return wsc::Canvas::Backend::Software;
}

bool backendCompiled(Backend backend)
{
    switch (backend) {
    case Backend::Software: return true;
    case Backend::OpenGL: return WSC_PERF_HAS_OPENGL != 0;
    case Backend::Vulkan: return WSC_PERF_HAS_VULKAN != 0;
    }
    return false;
}

const char *osName()
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

const char *architectureName()
{
#if defined(_M_X64) || defined(__x86_64__)
    return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

const char *compilerName()
{
#if defined(_MSC_VER)
    return "msvc";
#elif defined(__clang__)
    return "clang";
#elif defined(__GNUC__)
    return "gcc";
#else
    return "unknown";
#endif
}

std::string compilerVersion()
{
#if defined(_MSC_FULL_VER)
    return std::to_string(_MSC_FULL_VER);
#elif defined(__clang_version__)
    return __clang_version__;
#elif defined(__VERSION__)
    return __VERSION__;
#else
    return "unknown";
#endif
}

std::string cpuName()
{
#if defined(_WIN32)
    const std::string identifier = environmentValue("PROCESSOR_IDENTIFIER");
    return identifier.empty() ? "unknown" : identifier;
#elif defined(__APPLE__)
    std::size_t size = 0;
    if (sysctlbyname("machdep.cpu.brand_string", nullptr, &size, nullptr, 0)
            != 0
        || size == 0) {
        return "unknown";
    }
    std::string name(size, '\0');
    if (sysctlbyname(
            "machdep.cpu.brand_string", name.data(), &size, nullptr, 0)
        != 0) {
        return "unknown";
    }
    while (!name.empty()
           && (name.back() == '\0' || name.back() == '\n')) {
        name.pop_back();
    }
    return name;
#else
    std::ifstream cpuInfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuInfo, line)) {
        const std::size_t separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, separator);
        if (key.find("model name") == std::string::npos
            && key.find("Hardware") == std::string::npos) {
            continue;
        }
        const std::size_t valueStart =
            line.find_first_not_of(" \t", separator + 1u);
        return valueStart == std::string::npos
            ? "unknown" : line.substr(valueStart);
    }
    return "unknown";
#endif
}

ProcessMemory processMemory()
{
    ProcessMemory result;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
            sizeof(counters))) {
        result.residentBytes =
            static_cast<std::uint64_t>(counters.WorkingSetSize);
        result.peakResidentBytes =
            static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
        result.privateOrVirtualBytes =
            static_cast<std::uint64_t>(counters.PrivateUsage);
    }
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count)
        == KERN_SUCCESS) {
        result.residentBytes = static_cast<std::uint64_t>(info.resident_size);
        result.privateOrVirtualBytes =
            static_cast<std::uint64_t>(info.virtual_size);
    }
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        result.peakResidentBytes =
            static_cast<std::uint64_t>(usage.ru_maxrss);
    }
#else
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        std::istringstream fields(line);
        std::string key;
        std::uint64_t valueKb = 0;
        std::string unit;
        if (!(fields >> key >> valueKb >> unit)) {
            continue;
        }
        if (key == "VmRSS:") {
            result.residentBytes = valueKb * 1024u;
        } else if (key == "VmHWM:") {
            result.peakResidentBytes = valueKb * 1024u;
        } else if (key == "VmSize:") {
            result.privateOrVirtualBytes = valueKb * 1024u;
        }
    }
    if (result.peakResidentBytes == 0) {
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            result.peakResidentBytes =
                static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
        }
    }
#endif
    return result;
}

Distribution summarize(std::vector<double> samples)
{
    Distribution result;
    if (samples.empty()) {
        return result;
    }
    std::sort(samples.begin(), samples.end());
    result.minimum = samples.front();
    result.maximum = samples.back();
    result.mean =
        std::accumulate(samples.begin(), samples.end(), 0.0)
        / static_cast<double>(samples.size());
    auto percentile = [&](double ratio) {
        const std::size_t index = std::min(
            samples.size() - 1u,
            static_cast<std::size_t>(
                std::ceil(ratio * static_cast<double>(samples.size()))) - 1u);
        return samples[index];
    };
    const std::size_t middle = samples.size() / 2u;
    result.median = samples.size() % 2u == 0u
        ? (samples[middle - 1u] + samples[middle]) * 0.5
        : samples[middle];
    result.p90 = percentile(0.90);
    result.p95 = percentile(0.95);
    result.p99 = percentile(0.99);
    double squareSum = 0.0;
    for (const double sample : samples) {
        const double delta = sample - result.mean;
        squareSum += delta * delta;
    }
    result.standardDeviation =
        std::sqrt(squareSum / static_cast<double>(samples.size()));
    result.coefficientOfVariation =
        result.mean > 0.0 ? result.standardDeviation / result.mean : 0.0;
    return result;
}

void appendDistribution(
    std::ostringstream &json, const char *prefix, const Distribution &value)
{
    json << ",\"" << prefix << "_min_ms\":" << value.minimum
         << ",\"" << prefix << "_max_ms\":" << value.maximum
         << ",\"" << prefix << "_mean_ms\":" << value.mean
         << ",\"" << prefix << "_median_ms\":" << value.median
         << ",\"" << prefix << "_p90_ms\":" << value.p90
         << ",\"" << prefix << "_p95_ms\":" << value.p95
         << ",\"" << prefix << "_p99_ms\":" << value.p99
         << ",\"" << prefix << "_stddev_ms\":" << value.standardDeviation
         << ",\"" << prefix << "_cv\":" << value.coefficientOfVariation;
}

std::string formatHash(std::uint64_t hash)
{
    std::ostringstream output;
    output << std::hex << std::setw(16) << std::setfill('0') << hash;
    return output.str();
}

bool parseNonNegativeInt(const std::string &text, int &value)
{
    if (text.empty()) {
        return false;
    }
    int parsed = 0;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc()
        || result.ptr != text.data() + text.size()
        || parsed < 0) {
        return false;
    }
    value = parsed;
    return true;
}

void applyProfile(Options &options, const std::string &profile)
{
    options.profile = profile;
    if (profile == "quick") {
        options.frames = 3;
        options.warmup = 1;
    } else if (profile == "standard") {
        options.frames = 30;
        options.warmup = 5;
    } else if (profile == "thorough") {
        options.frames = 120;
        options.warmup = 20;
    }
}

void printUsage(std::ostream &output, const char *program)
{
    output
        << "Usage: " << program
        << " [--backend software|opengl|vulkan]"
        << " [--profile quick|standard|thorough]"
        << " [--scene all|name[,name...]]"
        << " [--frames N] [--warmup N] [--width N] [--height N]"
        << " [--output results.jsonl] [--capture-dir path]"
        << " [--list-scenes]\n";
}

bool parseOptions(
    int argc, char **argv, Options &options, std::string &error)
{
    for (int i = 1; i < argc; ++i) {
        const std::string argument(argv[i]);
        if (argument == "--help" || argument == "-h") {
            printUsage(std::cout, argv[0]);
            std::exit(0);
        }
        if (argument == "--list-scenes") {
            options.listScenes = true;
            continue;
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
                error = "invalid backend '" + value + "'";
                return false;
            }
        } else if (argument == "--profile") {
            if (value != "quick" && value != "standard"
                && value != "thorough") {
                error = "invalid profile '" + value + "'";
                return false;
            }
            applyProfile(options, value);
        } else if (argument == "--scene") {
            options.sceneFilter = value;
        } else if (argument == "--output") {
            options.outputPath = value;
        } else if (argument == "--capture-dir") {
            options.captureDirectory = value;
        } else {
            int parsed = 0;
            if (!parseNonNegativeInt(value, parsed)) {
                error = "invalid non-negative integer '" + value
                    + "' for " + argument;
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
    }

    if (options.frames <= 0) {
        error = "--frames must be greater than zero";
        return false;
    }
    if (options.width <= 0 || options.height <= 0
        || options.width > kMaxDimension || options.height > kMaxDimension
        || static_cast<std::uint64_t>(options.width)
            * static_cast<std::uint64_t>(options.height) > kMaxPixels) {
        error = "invalid or unreasonably large dimensions";
        return false;
    }
    return true;
}

class BenchmarkContext
{
public:
    ~BenchmarkContext()
    {
        canvas_.reset();
#if WSC_PERF_HAS_OPENGL
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
        }
        if (glfwInitialized_) {
            glfwTerminate();
        }
#endif
    }

    bool initialize(const Options &options, std::string &error)
    {
        if (!backendCompiled(options.backend)) {
            error = std::string("backend '") + backendName(options.backend)
                + "' was not compiled into this executable";
            return false;
        }
#if WSC_PERF_HAS_OPENGL
        if (options.backend == Backend::OpenGL) {
            if (!glfwInit()) {
                error = "glfwInit failed";
                return false;
            }
            glfwInitialized_ = true;
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
            window_ = glfwCreateWindow(
                options.width, options.height,
                "WhatsCanvas performance suite", nullptr, nullptr);
            if (window_ == nullptr) {
                error = "unable to create hidden OpenGL context";
                return false;
            }
            glfwMakeContextCurrent(window_);
            glfwSwapInterval(0);
            if (!wsc::Canvas::loadOpenGL(
                    reinterpret_cast<wsc::Canvas::OpenGLProcAddress>(
                        glfwGetProcAddress))) {
                error = "Canvas::loadOpenGL failed";
                return false;
            }
            glFinish_ =
                reinterpret_cast<GlFinishProc>(glfwGetProcAddress("glFinish"));
            glGetString_ = reinterpret_cast<GlGetStringProc>(
                glfwGetProcAddress("glGetString"));
            if (glFinish_ == nullptr || glGetString_ == nullptr) {
                error = "required OpenGL functions are unavailable";
                return false;
            }
        }
#endif
        const wsc::Canvas::Backend requested = canvasBackend(options.backend);
        if (!wsc::Canvas::isBackendAvailable(requested)) {
            error = std::string("backend '") + backendName(options.backend)
                + "' is unavailable on this host";
            return false;
        }
        canvas_ =
            wsc::Canvas::create(requested, options.width, options.height);
        if (!canvas_ || !canvas_->initializeContext()) {
            error = "Canvas initialization failed";
            return false;
        }
        canvas_->setSize(options.width, options.height);
#if WSC_PERF_HAS_OPENGL
        if (options.backend == Backend::OpenGL
            && !canvas_->setOutputTarget(wsc::OutputTarget::GLFramebuffer(
                0, options.width, options.height, true))) {
            error = "failed to configure the OpenGL output framebuffer";
            return false;
        }
#endif
        backend_ = options.backend;
        return true;
    }

    wsc::Canvas &canvas() { return *canvas_; }

    void finishFrame() const
    {
#if WSC_PERF_HAS_OPENGL
        if (backend_ == Backend::OpenGL) {
            glFinish_();
            return;
        }
#endif
#if WSC_PERF_HAS_VULKAN
        if (backend_ == Backend::Vulkan) {
            VkQueue queue =
                reinterpret_cast<VkQueue>(canvas_->vulkanQueue());
            if (queue != VK_NULL_HANDLE) {
                vkQueueWaitIdle(queue);
            }
        }
#endif
    }

    std::string deviceName() const
    {
#if WSC_PERF_HAS_OPENGL
        if (backend_ == Backend::OpenGL && glGetString_ != nullptr) {
            constexpr unsigned int kGlRenderer = 0x1F01u;
            return glString(kGlRenderer);
        }
#endif
#if WSC_PERF_HAS_VULKAN
        if (backend_ == Backend::Vulkan) {
            VkPhysicalDevice physicalDevice =
                reinterpret_cast<VkPhysicalDevice>(
                    canvas_->vulkanPhysicalDevice());
            if (physicalDevice != VK_NULL_HANDLE) {
                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties(physicalDevice, &properties);
                return properties.deviceName;
            }
        }
#endif
        return backend_ == Backend::Software ? "cpu" : "unknown";
    }

    std::string deviceVendor() const
    {
#if WSC_PERF_HAS_OPENGL
        if (backend_ == Backend::OpenGL && glGetString_ != nullptr) {
            constexpr unsigned int kGlVendor = 0x1F00u;
            return glString(kGlVendor);
        }
#endif
#if WSC_PERF_HAS_VULKAN
        if (backend_ == Backend::Vulkan) {
            VkPhysicalDeviceProperties properties{};
            if (vulkanProperties(properties)) {
                std::ostringstream vendor;
                vendor << "pci:0x" << std::hex << std::setw(4)
                       << std::setfill('0') << properties.vendorID;
                return vendor.str();
            }
        }
#endif
        return backend_ == Backend::Software ? "cpu" : "unknown";
    }

    std::string driverVersion() const
    {
#if WSC_PERF_HAS_OPENGL
        if (backend_ == Backend::OpenGL && glGetString_ != nullptr) {
            constexpr unsigned int kGlVersion = 0x1F02u;
            return glString(kGlVersion);
        }
#endif
#if WSC_PERF_HAS_VULKAN
        if (backend_ == Backend::Vulkan) {
            VkPhysicalDeviceProperties properties{};
            if (vulkanProperties(properties)) {
                std::ostringstream version;
                version << "driver:" << properties.driverVersion
                        << ";api:" << VK_VERSION_MAJOR(properties.apiVersion)
                        << "." << VK_VERSION_MINOR(properties.apiVersion)
                        << "." << VK_VERSION_PATCH(properties.apiVersion);
                return version.str();
            }
        }
#endif
        return backend_ == Backend::Software ? "n/a" : "unknown";
    }

private:
#if WSC_PERF_HAS_OPENGL
    using GlFinishProc = void (*)();
    using GlGetStringProc = const unsigned char *(*)(unsigned int);

    std::string glString(unsigned int name) const
    {
        const unsigned char *value = glGetString_(name);
        return value != nullptr
            ? reinterpret_cast<const char *>(value) : "unknown";
    }

    GLFWwindow *window_ = nullptr;
    bool glfwInitialized_ = false;
    GlFinishProc glFinish_ = nullptr;
    GlGetStringProc glGetString_ = nullptr;
#endif
#if WSC_PERF_HAS_VULKAN
    bool vulkanProperties(VkPhysicalDeviceProperties &properties) const
    {
        VkPhysicalDevice physicalDevice =
            reinterpret_cast<VkPhysicalDevice>(
                canvas_->vulkanPhysicalDevice());
        if (physicalDevice == VK_NULL_HANDLE) {
            return false;
        }
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);
        return true;
    }
#endif
    std::unique_ptr<wsc::Canvas> canvas_;
    Backend backend_ = Backend::Software;
};

wsc::Paint solid(const wsc::Color &color, bool antiAlias = true)
{
    wsc::Paint paint;
    paint.setStyle(wsc::Paint::Style::FILL);
    paint.setColor(color);
    paint.setAntiAlias(antiAlias);
    return paint;
}

void drawSolidRects(
    wsc::Canvas &canvas, SceneResources &, int width, int height, int frame)
{
    canvas.drawColor(wsc::Color(18, 22, 30, 255));
    constexpr int columns = 32;
    constexpr int rows = 18;
    const float cellWidth = static_cast<float>(width) / columns;
    const float cellHeight = static_cast<float>(height) / rows;
    const float shift = static_cast<float>(frame % 4) * 0.125f;
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            const int index = y * columns + x;
            wsc::Paint paint = solid(
                wsc::Color(
                    45 + (index * 37) % 190,
                    35 + (index * 61) % 200,
                    55 + (index * 29) % 180,
                    150 + (index * 17) % 106),
                false);
            canvas.drawRect(
                wsc::RectF(
                    x * cellWidth + shift, y * cellHeight,
                    cellWidth + 0.25f, cellHeight + 0.25f),
                paint);
        }
    }
}

void drawRoundedUi(
    wsc::Canvas &canvas, SceneResources &, int width, int height, int frame)
{
    canvas.drawColor(wsc::Color(242, 244, 248, 255));
    constexpr int columns = 12;
    constexpr int rows = 10;
    const float margin = std::min(width, height) * 0.025f;
    const float gap = std::max(3.0f, width * 0.006f);
    const float cellWidth =
        (width - margin * 2.0f - gap * (columns - 1)) / columns;
    const float cellHeight =
        (height - margin * 2.0f - gap * (rows - 1)) / rows;
    const float pulse = static_cast<float>(frame % 8) / 8.0f;
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            const int index = y * columns + x;
            wsc::Paint paint;
            paint.setStyle(wsc::Paint::Style::FILL);
            paint.setColor(wsc::Color(
                40 + (index * 23) % 170,
                70 + (index * 47) % 150,
                90 + (index * 31) % 150,
                210));
            if (index % 5 == 0) {
                paint.setLinearGradient(
                    0.0f, 0.0f, cellWidth, cellHeight,
                    wsc::Color(49, 175, 214, 230),
                    wsc::Color(236, 118, 129, 220));
            }
            const float radius =
                3.0f + static_cast<float>(index % 5) * 2.0f + pulse;
            canvas.drawRoundRect(
                wsc::RectF(
                    margin + x * (cellWidth + gap),
                    margin + y * (cellHeight + gap),
                    cellWidth, cellHeight),
                radius, paint);
        }
    }
}

wsc::Path makeStressPath(float cx, float cy, float radius, float phase)
{
    wsc::Path path;
    constexpr int points = 14;
    for (int i = 0; i < points; ++i) {
        const float angle =
            phase + static_cast<float>(i) * (2.0f * kPi / points);
        const float localRadius =
            radius * (i % 2 == 0 ? 1.0f : 0.42f);
        const float x = cx + std::cos(angle) * localRadius;
        const float y = cy + std::sin(angle) * localRadius;
        if (i == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    path.close();
    return path;
}

void drawPaths(
    wsc::Canvas &canvas, SceneResources &, int width, int height, int frame,
    bool churn)
{
    canvas.drawColor(wsc::Color(14, 18, 26, 255));
    constexpr int columns = 16;
    constexpr int rows = 10;
    const float cellWidth = static_cast<float>(width) / columns;
    const float cellHeight = static_cast<float>(height) / rows;
    const float phase = churn
        ? static_cast<float>(frame % 32) * 0.00625f : 0.0f;
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            const int index = y * columns + x;
            wsc::Path path = makeStressPath(
                (x + 0.5f) * cellWidth,
                (y + 0.5f) * cellHeight,
                std::min(cellWidth, cellHeight) * 0.42f,
                phase + static_cast<float>(index % 7) * 0.03f);
            wsc::Paint paint;
            paint.setStyle(index % 3 == 0
                ? wsc::Paint::Style::FILL_AND_STROKE
                : wsc::Paint::Style::FILL);
            paint.setColor(wsc::Color(
                60 + (index * 41) % 190,
                50 + (index * 29) % 190,
                80 + (index * 67) % 170, 205));
            paint.setStrokeColor(wsc::Color(245, 248, 252, 190));
            paint.setStrokeWidth(1.5f + static_cast<float>(index % 3));
            canvas.drawPath(path, paint);
        }
    }
}

void drawPathCached(
    wsc::Canvas &canvas, SceneResources &resources,
    int width, int height, int frame)
{
    drawPaths(canvas, resources, width, height, frame, false);
}

void drawPathChurn(
    wsc::Canvas &canvas, SceneResources &resources,
    int width, int height, int frame)
{
    drawPaths(canvas, resources, width, height, frame, true);
}

void drawGeometryStress(
    wsc::Canvas &canvas, SceneResources &, int width, int height, int frame)
{
    canvas.drawColor(wsc::Color(13, 17, 25, 255));
    constexpr int columns = 64;
    constexpr int rows = 36;
    const float cellWidth = static_cast<float>(width) / columns;
    const float cellHeight = static_cast<float>(height) / rows;
    const float inset =
        std::max(1.0f, std::min(cellWidth, cellHeight) * 0.12f);
    const float phase = static_cast<float>(frame % 8) * 0.0625f;

    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int index = row * columns + column;
            const float left = column * cellWidth + inset + phase;
            const float top = row * cellHeight + inset;
            const float shapeWidth = cellWidth - inset * 2.0f;
            const float shapeHeight = cellHeight - inset * 2.0f;
            const wsc::RectF bounds(
                left, top, shapeWidth, shapeHeight);
            wsc::Paint paint = solid(wsc::Color(
                35 + (index * 37) % 205,
                45 + (index * 53) % 195,
                55 + (index * 71) % 185,
                160 + (index * 17) % 96));

            switch (index % 6) {
            case 0:
                canvas.drawRect(bounds, paint);
                break;
            case 1:
                canvas.drawRoundRect(
                    bounds, std::min(shapeWidth, shapeHeight) * 0.24f,
                    paint);
                break;
            case 2:
                canvas.drawCircle(
                    left + shapeWidth * 0.5f,
                    top + shapeHeight * 0.5f,
                    std::min(shapeWidth, shapeHeight) * 0.45f,
                    paint);
                break;
            case 3:
                canvas.drawOval(bounds, paint);
                break;
            case 4: {
                wsc::Path triangle;
                triangle.moveTo(left + shapeWidth * 0.5f, top);
                triangle.lineTo(left + shapeWidth, top + shapeHeight);
                triangle.lineTo(left, top + shapeHeight);
                triangle.close();
                canvas.drawPath(triangle, paint);
                break;
            }
            default: {
                wsc::Path diamond;
                diamond.moveTo(left + shapeWidth * 0.5f, top);
                diamond.lineTo(
                    left + shapeWidth, top + shapeHeight * 0.5f);
                diamond.lineTo(
                    left + shapeWidth * 0.5f, top + shapeHeight);
                diamond.lineTo(left, top + shapeHeight * 0.5f);
                diamond.close();
                canvas.drawPath(diamond, paint);
                break;
            }
            }
        }
    }
}

void drawImageGrid(
    wsc::Canvas &canvas, SceneResources &resources,
    int width, int height, int frame)
{
    canvas.drawColor(wsc::Color(22, 26, 34, 255));
    constexpr int columns = 12;
    constexpr int rows = 8;
    const float cellWidth = static_cast<float>(width) / columns;
    const float cellHeight = static_cast<float>(height) / rows;
    wsc::Paint paint;
    paint.setColor(wsc::Color::WHITE);
    paint.setImageSampling(wsc::Paint::ImageSampling::LINEAR);
    const float wobble = static_cast<float>(frame % 5) * 0.15f;
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            const int index = y * columns + x;
            paint.setAlpha(0.68f + static_cast<float>(index % 4) * 0.08f);
            const float inset = 4.0f + static_cast<float>(index % 3);
            const wsc::RectF destination(
                x * cellWidth + inset + wobble,
                y * cellHeight + inset,
                cellWidth - inset * 2.0f,
                cellHeight - inset * 2.0f);
            if (index % 3 == 0) {
                canvas.drawImageRounded(
                    resources.image, destination, 7.0f, paint);
            } else {
                canvas.drawImage(resources.image, destination, paint);
            }
        }
    }
}

void drawClipLayers(
    wsc::Canvas &canvas, SceneResources &, int width, int height, int frame)
{
    canvas.drawColor(wsc::Color(28, 32, 45, 255));
    const float scale = std::min(
        static_cast<float>(width) / 960.0f,
        static_cast<float>(height) / 540.0f);
    const float phase = static_cast<float>(frame % 8) * scale;
    for (int i = 0; i < 16; ++i) {
        const int column = i % 4;
        const int row = i / 4;
        const wsc::RectF bounds(
            width * (0.04f + column * 0.24f),
            height * (0.06f + row * 0.235f),
            width * 0.20f, height * 0.18f);
        canvas.save();
        wsc::Path clip;
        clip.addRoundRect(bounds, (10.0f + i % 4 * 4.0f) * scale);
        canvas.clipPath(clip);
        wsc::Paint layerPaint = solid(wsc::Color(255, 255, 255, 190));
        layerPaint.setAlpha(0.72f + static_cast<float>(i % 3) * 0.08f);
        canvas.saveLayer(bounds, layerPaint);
        for (int stripe = 0; stripe < 8; ++stripe) {
            wsc::Paint stripePaint = solid(wsc::Color(
                40 + (i * 31 + stripe * 17) % 200,
                60 + (i * 47 + stripe * 29) % 180,
                90 + (i * 19 + stripe * 43) % 160, 210));
            canvas.drawRect(
                wsc::RectF(
                    bounds.getX() - phase + stripe * bounds.getWidth() / 6.0f,
                    bounds.getY(),
                    bounds.getWidth() / 8.0f,
                    bounds.getHeight()),
                stripePaint);
        }
        canvas.restore();
        canvas.restore();
    }
}

void drawShadows(
    wsc::Canvas &canvas, SceneResources &, int width, int height, int frame)
{
    canvas.drawColor(wsc::Color(232, 235, 241, 255));
    constexpr int columns = 9;
    constexpr int rows = 4;
    const float margin = width * 0.045f;
    const float gap = width * 0.012f;
    const float cellWidth =
        (width - margin * 2.0f - gap * (columns - 1)) / columns;
    const float cellHeight =
        (height - margin * 2.0f - gap * (rows - 1)) / rows;
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            const int index = y * columns + x;
            wsc::Paint paint = solid(wsc::Color(
                50 + (index * 37) % 170,
                80 + (index * 29) % 150,
                105 + (index * 19) % 135, 245));
            paint.setShadowLayer(
                7.0f + static_cast<float>(index % 4) * 2.0f,
                2.0f + static_cast<float>(frame % 3),
                4.0f,
                wsc::Color(8, 15, 30, 105));
            canvas.drawRoundRect(
                wsc::RectF(
                    margin + x * (cellWidth + gap),
                    margin + y * (cellHeight + gap),
                    cellWidth, cellHeight),
                9.0f, paint);
        }
    }
}

void drawTextScene(
    wsc::Canvas &canvas, SceneResources &, int width, int height, int frame,
    bool churn)
{
    canvas.drawColor(wsc::Color(248, 249, 251, 255));
    constexpr int rows = 30;
    constexpr int columns = 4;
    const float columnWidth = static_cast<float>(width) / columns;
    const float rowHeight = static_cast<float>(height) / rows;
    for (int column = 0; column < columns; ++column) {
        for (int row = 0; row < rows; ++row) {
            const int index = column * rows + row;
            wsc::Paint paint;
            paint.setColor(wsc::Color(
                20 + (index * 17) % 90,
                30 + (index * 11) % 100,
                45 + (index * 7) % 110, 255));
            paint.setTextSize(
                std::max(9.0f, rowHeight * (0.62f + 0.08f * (index % 3))));
            paint.setFontWeight(index % 5 == 0 ? 700 : 400);
            const int suffix = churn ? (frame + index) % 1000 : index % 10;
            canvas.drawText(
                "Canvas text Aa 123 " + std::to_string(suffix),
                column * columnWidth + 8.0f,
                row * rowHeight + 1.0f,
                paint);
        }
    }
}

void drawTextCached(
    wsc::Canvas &canvas, SceneResources &resources,
    int width, int height, int frame)
{
    drawTextScene(canvas, resources, width, height, frame, false);
}

void drawTextChurn(
    wsc::Canvas &canvas, SceneResources &resources,
    int width, int height, int frame)
{
    drawTextScene(canvas, resources, width, height, frame, true);
}

void drawTextStress(
    wsc::Canvas &canvas, SceneResources &, int width, int height, int)
{
    canvas.drawColor(wsc::Color(247, 248, 251, 255));
    constexpr int columns = 8;
    constexpr int rows = 72;
    static const std::array<std::string, 6> samples = {{
        "Canvas rendering Aa 123",
        u8"\u4e2d\u6587\u6392\u7248 Canvas 123",
        u8"Arabic \u0645\u0631\u062d\u0628\u0627 42",
        u8"Devanagari \u0928\u092e\u0938\u094d\u0924\u0947",
        "OpenType shaping AV fi",
        "Glyph atlas 0123456789",
    }};
    const float columnWidth = static_cast<float>(width) / columns;
    const float rowHeight = static_cast<float>(height) / rows;
    const float textSize =
        std::clamp(rowHeight * 0.76f, 10.0f, 18.0f);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int index = row * columns + column;
            wsc::Paint paint;
            paint.setColor(wsc::Color(
                18 + (index * 13) % 82,
                28 + (index * 17) % 92,
                42 + (index * 19) % 108, 255));
            paint.setTextSize(textSize);
            paint.setFontWeight(index % 7 == 0 ? 700 : 400);
            canvas.drawText(
                samples[static_cast<std::size_t>(index) % samples.size()],
                column * columnWidth + 4.0f,
                (row + 0.82f) * rowHeight,
                paint);
        }
    }
}

void drawContractTextLatin(
    wsc::Canvas &canvas, SceneResources &resources,
    int width, int height, int)
{
    canvas.drawColor(wsc::Color(247, 248, 251, 255));
    if (!resources.contractFontReady) {
        return;
    }
    constexpr int columns = 8;
    constexpr int rows = 72;
    static const std::array<std::string, 6> samples = {{
        "Canvas rendering Aa 123",
        "OpenType shaping AV fi",
        "Glyph atlas 0123456789",
        "Vector paths and images",
        "Quick brown fox 24680",
        "Reusable 2D render batch",
    }};
    const float columnWidth = static_cast<float>(width) / columns;
    const float rowHeight = static_cast<float>(height) / rows;
    const float textSize =
        std::clamp(rowHeight * 0.76f, 10.0f, 18.0f);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int index = row * columns + column;
            wsc::Paint paint;
            paint.setColor(wsc::Color(
                18 + (index * 13) % 82,
                28 + (index * 17) % 92,
                42 + (index * 19) % 108, 255));
            paint.setTextSize(textSize);
            paint.setFontFamily("CrossLibraryRoboto");
            canvas.drawText(
                samples[static_cast<std::size_t>(index)
                        % samples.size()],
                column * columnWidth + 4.0f,
                (row + 0.82f) * rowHeight,
                paint);
        }
    }
}

void drawFilterBackground(wsc::Canvas &canvas, int width, int height)
{
    wsc::Paint gradient;
    gradient.setStyle(wsc::Paint::Style::FILL);
    gradient.setLinearGradient(
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
        wsc::Color(23, 37, 69, 255), wsc::Color(104, 38, 82, 255));
    canvas.drawRect(
        wsc::RectF(0.0f, 0.0f, static_cast<float>(width),
                   static_cast<float>(height)),
        gradient);
    for (int i = 0; i < 16; ++i) {
        canvas.drawCircle(
            static_cast<float>((i * 137) % width),
            static_cast<float>((i * 83 + 41) % height),
            24.0f + static_cast<float>((i * 17) % 52),
            solid(wsc::Color(
                45 + (i * 43) % 190,
                80 + (i * 67) % 170,
                70 + (i * 31) % 180, 190)));
    }
}

void drawFrostedGlass(
    wsc::Canvas &canvas, SceneResources &, int width, int height, int)
{
    drawFilterBackground(canvas, width, height);
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float scale = std::min(w / 960.0f, h / 540.0f);
    const std::array<wsc::RectF, 4> panels = {
        wsc::RectF(0.10f * w, 0.15f * h, 0.43f * w, 0.43f * h),
        wsc::RectF(0.34f * w, 0.09f * h, 0.43f * w, 0.43f * h),
        wsc::RectF(0.18f * w, 0.43f * h, 0.43f * w, 0.43f * h),
        wsc::RectF(0.47f * w, 0.38f * h, 0.43f * w, 0.43f * h),
    };
    wsc::LayerOptions options;
    options.setBackdropFilter(
        wsc::ImageFilter::frostedGlass(
            12.0f * scale, 1.12f, 1.03f, 1.02f, 0.0f));
    for (std::size_t i = 0; i < panels.size(); ++i) {
        canvas.save();
        wsc::Path clip;
        clip.addRoundRect(panels[i], 24.0f * scale);
        canvas.clipPath(clip);
        canvas.saveLayer(
            panels[i], solid(wsc::Color(255, 255, 255, 255)), options);
        canvas.drawRect(
            panels[i], solid(wsc::Color(
                100 + static_cast<int>(i) * 35,
                220 - static_cast<int>(i) * 19,
                170 + static_cast<int>(i) * 18, 42)));
        canvas.restore();
        canvas.restore();
    }
}

void drawInnerShadow(
    wsc::Canvas &canvas, SceneResources &, int width, int height, int)
{
    canvas.drawColor(wsc::Color(24, 29, 42, 255));
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float scale = std::min(w / 960.0f, h / 540.0f);
    const float marginX = 0.055f * w;
    const float marginY = 0.075f * h;
    const float gapX = 0.018f * w;
    const float gapY = 0.026f * h;
    const float cellWidth =
        (w - 2.0f * marginX - 5.0f * gapX) / 6.0f;
    const float cellHeight =
        (h - 2.0f * marginY - 3.0f * gapY) / 4.0f;
    wsc::LayerOptions options;
    options.setImageFilter(wsc::ImageFilter::innerShadow(
        8.0f * scale, 3.0f * scale, 4.0f * scale,
        wsc::Color(5, 10, 22, 190)));
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 6; ++column) {
            const int index = row * 6 + column;
            const wsc::RectF bounds(
                marginX + column * (cellWidth + gapX),
                marginY + row * (cellHeight + gapY),
                cellWidth, cellHeight);
            canvas.saveLayer(
                bounds, solid(wsc::Color(255, 255, 255, 255)), options);
            canvas.drawRoundRect(
                bounds, 12.0f * scale,
                solid(wsc::Color(
                    55 + (index * 47) % 180,
                    85 + (index * 31) % 150,
                    105 + (index * 19) % 140, 255)));
            canvas.restore();
        }
    }
}

const std::array<Scene, 14> &scenes()
{
    static const std::array<Scene, 14> value = {{
        {"solid_rects", "raster", "churn", 576, drawSolidRects},
        {"rounded_ui", "raster", "churn", 120, drawRoundedUi},
        {"path_cached", "path", "hot", 160, drawPathCached},
        {"path_churn", "path", "churn", 160, drawPathChurn},
        {"geometry_stress", "geometry", "churn", 2304, drawGeometryStress},
        {"image_grid", "image", "hot", 96, drawImageGrid},
        {"clip_layers", "layer", "churn", 144, drawClipLayers},
        {"shadow_grid", "effect", "churn", 36, drawShadows},
        {"text_cached", "text", "hot", 120, drawTextCached},
        {"text_churn", "text", "churn", 120, drawTextChurn},
        {"text_stress", "text", "hot", 576, drawTextStress},
        {"contract_text_latin", "text", "hot", 576, drawContractTextLatin},
        {"frosted_glass", "filter", "hot", 4, drawFrostedGlass},
        {"inner_shadow", "filter", "hot", 24, drawInnerShadow},
    }};
    return value;
}

bool sceneSelected(const std::string &filter, std::string_view name)
{
    if (filter == "all") {
        return true;
    }
    std::size_t begin = 0;
    while (begin <= filter.size()) {
        const std::size_t end = filter.find(',', begin);
        const std::string_view token(
            filter.data() + begin,
            (end == std::string::npos ? filter.size() : end) - begin);
        if (token == name) {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1u;
    }
    return false;
}

class ResultWriter
{
public:
    bool open(const std::string &path, std::string &error)
    {
        if (path.empty()) {
            return true;
        }
        const std::filesystem::path output(path);
        if (output.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(output.parent_path(), ec);
            if (ec) {
                error = "unable to create output directory: " + ec.message();
                return false;
            }
        }
        file_.open(path, std::ios::out | std::ios::trunc);
        if (!file_) {
            error = "unable to open output file '" + path + "'";
            return false;
        }
        return true;
    }

    void write(const char *prefix, const std::string &json)
    {
        std::cout << prefix << ' ' << json << '\n';
        if (file_) {
            file_ << json << '\n';
            file_.flush();
        }
    }

private:
    std::ofstream file_;
};

FrameTimings renderFrame(
    BenchmarkContext &context, const Scene &scene,
    SceneResources &resources, const Options &options, int frame)
{
    const Clock::time_point start = Clock::now();
    wsc::Canvas &canvas = context.canvas();
    canvas.beginFrame();
    scene.draw(canvas, resources, options.width, options.height, frame);
    const Clock::time_point recorded = Clock::now();
    canvas.endFrame();
    context.finishFrame();
    const Clock::time_point finished = Clock::now();
    return {
        std::chrono::duration<double, std::milli>(recorded - start).count(),
        std::chrono::duration<double, std::milli>(finished - recorded).count(),
        std::chrono::duration<double, std::milli>(finished - start).count(),
    };
}

bool initializeResources(
    wsc::Canvas &canvas, SceneResources &resources, const Options &options,
    std::string &error)
{
    constexpr int imageWidth = 128;
    constexpr int imageHeight = 128;
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(imageWidth) * imageHeight * 4u);
    for (int y = 0; y < imageHeight; ++y) {
        for (int x = 0; x < imageWidth; ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * imageWidth + x) * 4u;
            const bool checker = ((x / 16) + (y / 16)) % 2 == 0;
            pixels[index] = static_cast<unsigned char>(
                checker ? 235 : (x * 255) / (imageWidth - 1));
            pixels[index + 1u] = static_cast<unsigned char>(
                checker ? (y * 255) / (imageHeight - 1) : 75);
            pixels[index + 2u] = static_cast<unsigned char>(
                checker ? 90 : 220);
            pixels[index + 3u] = static_cast<unsigned char>(
                170 + ((x + y) % 86));
        }
    }
    if (!resources.image.loadFromRGBA(
            canvas, pixels, imageWidth, imageHeight, true)) {
        error = "failed to create benchmark image";
        return false;
    }
    if (sceneSelected(options.sceneFilter, "contract_text_latin")) {
        const std::filesystem::path contractFont =
            WHATSCANVAS_PERF_CONTRACT_FONT;
        if (std::filesystem::is_regular_file(contractFont)) {
            resources.contractFontReady = canvas.registerFontFace(
                wsc::FontFace::fromFile(
                    wsc::FontDescriptor("CrossLibraryRoboto"),
                    contractFont.string()));
        }
        if (!resources.contractFontReady) {
            error = "failed to register cross-library contract font: "
                + contractFont.string();
            return false;
        }
    }
    return true;
}

std::string metadataJson(
    const Options &options, const BenchmarkContext &context,
    double initializationMs)
{
    const ProcessMemory memory = processMemory();
    std::ostringstream json;
    json << std::fixed << std::setprecision(6)
         << "{\"type\":\"metadata\",\"schema\":" << kSchemaVersion
         << ",\"suite\":\"WhatsCanvasPerformanceSuite\""
         << ",\"library\":\"WhatsCanvas\""
         << ",\"library_version\":\""
         << jsonEscape(WHATSCANVAS_PERF_VERSION) << "\""
         << ",\"synchronization\":\"gpu_complete\""
         << ",\"cross_library_contract\":\"1.1.0\""
         << ",\"version\":\"" << jsonEscape(WHATSCANVAS_PERF_VERSION) << "\""
         << ",\"commit\":\""
         << jsonEscape(environmentValue("WHATSCANVAS_PERF_COMMIT")) << "\""
         << ",\"backend\":\"" << backendName(options.backend) << "\""
         << ",\"device\":\"" << jsonEscape(context.deviceName()) << "\""
         << ",\"device_vendor\":\""
         << jsonEscape(context.deviceVendor()) << "\""
         << ",\"driver\":\""
         << jsonEscape(context.driverVersion()) << "\""
         << ",\"os\":\"" << osName() << "\""
         << ",\"architecture\":\"" << architectureName() << "\""
         << ",\"cpu\":\"" << jsonEscape(cpuName()) << "\""
         << ",\"compiler\":\"" << compilerName() << "\""
         << ",\"compiler_version\":\""
         << jsonEscape(compilerVersion()) << "\""
         << ",\"build_type\":\""
         << jsonEscape(WHATSCANVAS_PERF_BUILD_TYPE) << "\""
         << ",\"profile\":\"" << jsonEscape(options.profile) << "\""
         << ",\"width\":" << options.width
         << ",\"height\":" << options.height
         << ",\"frames\":" << options.frames
         << ",\"warmup\":" << options.warmup
         << ",\"hardware_threads\":" << std::thread::hardware_concurrency()
         << ",\"initialization_ms\":" << initializationMs
         << ",\"initial_rss_bytes\":" << memory.residentBytes
         << ",\"initial_peak_rss_bytes\":" << memory.peakResidentBytes
         << ",\"initial_private_or_virtual_bytes\":"
         << memory.privateOrVirtualBytes
         << "}";
    return json.str();
}

bool runScene(
    BenchmarkContext &context, SceneResources &resources,
    const Options &options, const Scene &scene,
    ResultWriter &writer, std::string &error)
{
    const ProcessMemory before = processMemory();
    const FrameTimings cold =
        renderFrame(context, scene, resources, options, 0);
    for (int i = 0; i < options.warmup; ++i) {
        renderFrame(context, scene, resources, options, i + 1);
    }

    std::vector<double> recordSamples;
    std::vector<double> submitSamples;
    std::vector<double> totalSamples;
    recordSamples.reserve(static_cast<std::size_t>(options.frames));
    submitSamples.reserve(static_cast<std::size_t>(options.frames));
    totalSamples.reserve(static_cast<std::size_t>(options.frames));
    for (int i = 0; i < options.frames; ++i) {
        const FrameTimings timings =
            renderFrame(
                context, scene, resources, options,
                options.warmup + i + 1);
        recordSamples.push_back(timings.recordMs);
        submitSamples.push_back(timings.submitMs);
        totalSamples.push_back(timings.totalMs);
    }

    // A fixed validation frame makes the hash independent of profile/frame
    // count. Readback and capture stay outside the measured intervals.
    renderFrame(context, scene, resources, options, 0);
    std::vector<unsigned char> pixels;
    const Clock::time_point readbackStart = Clock::now();
    if (!context.canvas().readPixelsRGBA(pixels)) {
        error = "pixel readback failed for scene '" + std::string(scene.name)
            + "'";
        return false;
    }
    const double readbackMs =
        std::chrono::duration<double, std::milli>(
            Clock::now() - readbackStart).count();
    const std::size_t expectedBytes =
        static_cast<std::size_t>(options.width)
        * static_cast<std::size_t>(options.height) * 4u;
    if (pixels.size() != expectedBytes) {
        error = "unexpected readback size for scene '"
            + std::string(scene.name) + "'";
        return false;
    }

    if (!options.captureDirectory.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(
            options.captureDirectory, ec);
        if (ec) {
            error = "unable to create capture directory: " + ec.message();
            return false;
        }
        const std::filesystem::path capture =
            std::filesystem::path(options.captureDirectory)
            / (std::string(backendName(options.backend)) + "_"
               + scene.name + ".ppm");
        if (!context.canvas().savePixelsPPM(capture.string())) {
            error = "failed to save capture '" + capture.string() + "'";
            return false;
        }
    }

    const Distribution record = summarize(std::move(recordSamples));
    const Distribution submit = summarize(std::move(submitSamples));
    const Distribution total = summarize(std::move(totalSamples));
    const ProcessMemory after = processMemory();
    const wsc::Canvas::RenderStats stats = context.canvas().getRenderStats();
    const double fps =
        total.median > 0.0 ? 1000.0 / total.median : 0.0;
    const double operationsPerSecond =
        total.median > 0.0
        ? static_cast<double>(scene.operationsPerFrame) * 1000.0
            / total.median
        : 0.0;
    const std::int64_t rssDelta =
        static_cast<std::int64_t>(after.residentBytes)
        - static_cast<std::int64_t>(before.residentBytes);

    std::ostringstream json;
    json << std::fixed << std::setprecision(6)
         << "{\"type\":\"result\",\"schema\":" << kSchemaVersion
         << ",\"backend\":\"" << backendName(options.backend) << "\""
         << ",\"scene\":\"" << scene.name << "\""
         << ",\"category\":\"" << scene.category << "\""
         << ",\"cache_mode\":\"" << scene.cacheMode << "\""
         << ",\"width\":" << options.width
         << ",\"height\":" << options.height
         << ",\"frames\":" << options.frames
         << ",\"warmup\":" << options.warmup
         << ",\"operations_per_frame\":" << scene.operationsPerFrame
         << ",\"cold_total_ms\":" << cold.totalMs;
    appendDistribution(json, "record", record);
    appendDistribution(json, "submit", submit);
    appendDistribution(json, "total", total);
    json << ",\"fps\":" << fps
         << ",\"operations_per_second\":" << operationsPerSecond
         << ",\"readback_ms\":" << readbackMs
         << ",\"readback_bytes\":" << pixels.size()
         << ",\"pixel_hash\":\""
         << formatHash(wsc::Canvas::hashPixelsRGBA(pixels)) << "\""
         << ",\"rss_before_bytes\":" << before.residentBytes
         << ",\"rss_after_bytes\":" << after.residentBytes
         << ",\"rss_delta_bytes\":" << rssDelta
         << ",\"peak_rss_bytes\":" << after.peakResidentBytes
         << ",\"private_or_virtual_bytes\":"
         << after.privateOrVirtualBytes
         << ",\"command_count\":" << stats.commandCount
         << ",\"draw_call_count\":" << stats.drawCallCount
         << ",\"merged_batch_count\":" << stats.mergedBatchCount
         << ",\"render_target_switches\":" << stats.renderTargetSwitches
         << ",\"filter_count\":" << stats.filterCount
         << ",\"filter_pass_count\":" << stats.filterPassCount
         << ",\"filter_pixel_pass_count\":"
         << stats.filterPixelPassCount
         << ",\"path_vertex_count\":" << stats.pathVertexCount
         << ",\"path_upload_count\":" << stats.pathUploadCount
         << ",\"path_upload_bytes\":" << stats.pathUploadBytes
         << ",\"pooled_render_target_bytes\":"
         << stats.pooledRenderTargetBytes
         << ",\"glyph_atlas_texture_bytes\":"
         << stats.glyphAtlasTextureBytes
         << ",\"tessellation_cache_hits\":"
         << stats.tessellationCacheHits
         << ",\"tessellation_cache_misses\":"
         << stats.tessellationCacheMisses
         << ",\"tessellation_cache_bytes\":"
         << stats.tessellationCacheBytes
         << ",\"aa_cache_hits\":" << stats.aaCacheHits
         << ",\"aa_cache_misses\":" << stats.aaCacheMisses
         << ",\"aa_cache_size\":" << stats.aaCacheSize
         << ",\"aa_cache_bytes\":" << stats.aaCacheBytes
         << ",\"stroke_cache_hits\":" << stats.strokeCacheHits
         << ",\"stroke_cache_misses\":" << stats.strokeCacheMisses
         << ",\"stroke_cache_bytes\":" << stats.strokeCacheBytes
         << ",\"bitmap_text_cache_bytes\":"
         << stats.bitmapTextCacheBytes
         << "}";
    writer.write("PERF_RESULT", json.str());
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    Options options;
    std::string error;
    if (!parseOptions(argc, argv, options, error)) {
        std::cerr << "PERF_ERROR message=\"" << error << "\"\n";
        printUsage(std::cerr, argv[0]);
        return 2;
    }

    if (options.listScenes) {
        for (const Scene &scene : scenes()) {
            std::cout << scene.name << '\t' << scene.category << '\t'
                      << scene.cacheMode << '\n';
        }
        return 0;
    }

    ResultWriter writer;
    if (!writer.open(options.outputPath, error)) {
        std::cerr << "PERF_ERROR message=\"" << error << "\"\n";
        return 3;
    }

    try {
        BenchmarkContext context;
        const Clock::time_point initializationStart = Clock::now();
        if (!context.initialize(options, error)) {
            std::cerr << "PERF_ERROR backend=" << backendName(options.backend)
                      << " message=\"" << error << "\"\n";
            return 4;
        }
        SceneResources resources;
        if (!initializeResources(
                context.canvas(), resources, options, error)) {
            std::cerr << "PERF_ERROR backend=" << backendName(options.backend)
                      << " message=\"" << error << "\"\n";
            return 5;
        }
        const double initializationMs =
            std::chrono::duration<double, std::milli>(
                Clock::now() - initializationStart).count();
        writer.write(
            "PERF_METADATA", metadataJson(options, context, initializationMs));

        bool ranScene = false;
        for (const Scene &scene : scenes()) {
            if (!sceneSelected(options.sceneFilter, scene.name)) {
                continue;
            }
            ranScene = true;
            if (!runScene(
                    context, resources, options, scene, writer, error)) {
                std::cerr << "PERF_ERROR backend="
                          << backendName(options.backend)
                          << " scene=" << scene.name
                          << " message=\"" << error << "\"\n";
                return 6;
            }
        }
        if (!ranScene) {
            std::cerr << "PERF_ERROR message=\"no matching scenes\"\n";
            return 7;
        }
    } catch (const std::exception &exception) {
        std::cerr << "PERF_ERROR message=\"" << exception.what() << "\"\n";
        return 8;
    }
    return 0;
}
