#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "nanovg.h"
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"

#ifndef WHATSCANVAS_NANOVG_VERSION
#define WHATSCANVAS_NANOVG_VERSION "unknown"
#endif

#ifndef WHATSCANVAS_NANOVG_BUILD_TYPE
#define WHATSCANVAS_NANOVG_BUILD_TYPE "unknown"
#endif

#ifndef WHATSCANVAS_NANOVG_CONTRACT_FONT
#define WHATSCANVAS_NANOVG_CONTRACT_FONT ""
#endif

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kDefaultWidth = 1920;
constexpr int kDefaultHeight = 1080;
constexpr int kMaximumDimension = 16384;
constexpr std::uint64_t kMaximumPixels = UINT64_C(100000000);

enum class WorkloadMode
{
    Fixed,
    Stable,
    DynamicData,
    DynamicStructure,
};

struct Options
{
    int width = kDefaultWidth;
    int height = kDefaultHeight;
    int frames = 30;
    int warmup = 5;
    std::string profile = "standard";
    std::string scene;
    std::string outputPath;
    std::string captureDirectory;
    bool listScenes = false;
    WorkloadMode workloadMode = WorkloadMode::Fixed;
    int operations = 0;
    std::uint32_t seed = 1;
    int textureCount = 1;
    double roundedRatio = 1.0 / 3.0;
    double stateChangeRate = 0.0;
    int textLength = 24;
    bool workloadCustomized = false;
};

struct FrameTiming
{
    double recordMs = 0.0;
    double submitMs = 0.0;
    double totalMs = 0.0;
};

struct Distribution
{
    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
    double median = 0.0;
    double p95 = 0.0;
};

struct Resources
{
    int image = 0;
    std::vector<int> workloadImages;
    std::vector<std::string> workloadTextVariants;
    int font = -1;
};

constexpr std::array<std::string_view, 3> kScenes = {{
    "geometry_stress",
    "image_grid",
    "contract_text_latin",
}};

std::string jsonEscape(std::string_view value)
{
    std::ostringstream escaped;
    for (const char character : value) {
        switch (character) {
        case '\\': escaped << "\\\\"; break;
        case '"': escaped << "\\\""; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default: escaped << character; break;
        }
    }
    return escaped.str();
}

bool parseInteger(
    const char *value, int &result, std::string &error)
{
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        if (consumed != std::string(value).size()) {
            throw std::invalid_argument("trailing characters");
        }
        result = parsed;
        return true;
    } catch (const std::exception &) {
        error = std::string("invalid integer '") + value + "'";
        return false;
    }
}

bool parseDouble(
    const char *value, double &result, std::string &error)
{
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(value, &consumed);
        if (consumed != std::string(value).size()
            || !std::isfinite(parsed)) {
            throw std::invalid_argument("invalid value");
        }
        result = parsed;
        return true;
    } catch (const std::exception &) {
        error = std::string("invalid number '") + value + "'";
        return false;
    }
}

const char *workloadModeName(WorkloadMode mode)
{
    switch (mode) {
    case WorkloadMode::Fixed: return "fixed";
    case WorkloadMode::Stable: return "stable";
    case WorkloadMode::DynamicData: return "dynamic-data";
    case WorkloadMode::DynamicStructure: return "dynamic-structure";
    }
    return "unknown";
}

bool applyProfile(Options &options, std::string &error)
{
    if (options.profile == "quick") {
        options.frames = 3;
        options.warmup = 1;
    } else if (options.profile == "standard") {
        options.frames = 30;
        options.warmup = 5;
    } else if (options.profile == "thorough") {
        options.frames = 120;
        options.warmup = 20;
    } else {
        error = "unknown profile '" + options.profile + "'";
        return false;
    }
    return true;
}

bool parseOptions(
    int argc, char **argv, Options &options, std::string &error)
{
    bool framesOverridden = false;
    bool warmupOverridden = false;
    bool workloadModeSpecified = false;
    bool workloadParametersSpecified = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--list-scenes") {
            options.listScenes = true;
            continue;
        }
        if (index + 1 >= argc) {
            error = "missing value for '" + argument + "'";
            return false;
        }
        const char *value = argv[++index];
        if (argument == "--profile") {
            options.profile = value;
        } else if (argument == "--scene") {
            options.scene = value;
        } else if (argument == "--width") {
            if (!parseInteger(value, options.width, error)) {
                return false;
            }
        } else if (argument == "--height") {
            if (!parseInteger(value, options.height, error)) {
                return false;
            }
        } else if (argument == "--frames") {
            if (!parseInteger(value, options.frames, error)) {
                return false;
            }
            framesOverridden = true;
        } else if (argument == "--warmup") {
            if (!parseInteger(value, options.warmup, error)) {
                return false;
            }
            warmupOverridden = true;
        } else if (argument == "--output") {
            options.outputPath = value;
        } else if (argument == "--capture-dir") {
            options.captureDirectory = value;
        } else if (argument == "--workload") {
            const std::string_view mode(value);
            if (mode == "fixed") {
                options.workloadMode = WorkloadMode::Fixed;
            } else if (mode == "stable") {
                options.workloadMode = WorkloadMode::Stable;
            } else if (mode == "dynamic-data") {
                options.workloadMode = WorkloadMode::DynamicData;
            } else if (mode == "dynamic-structure") {
                options.workloadMode = WorkloadMode::DynamicStructure;
            } else {
                error = "unknown workload mode";
                return false;
            }
            workloadModeSpecified = true;
        } else if (argument == "--operations") {
            if (!parseInteger(value, options.operations, error)) {
                return false;
            }
            workloadParametersSpecified = true;
        } else if (argument == "--seed") {
            int seed = 0;
            if (!parseInteger(value, seed, error) || seed < 0) {
                error = "--seed must be non-negative";
                return false;
            }
            options.seed = static_cast<std::uint32_t>(seed);
            workloadParametersSpecified = true;
        } else if (argument == "--texture-count") {
            if (!parseInteger(value, options.textureCount, error)) {
                return false;
            }
            workloadParametersSpecified = true;
        } else if (argument == "--rounded-ratio") {
            if (!parseDouble(value, options.roundedRatio, error)) {
                return false;
            }
            workloadParametersSpecified = true;
        } else if (argument == "--state-change-rate") {
            if (!parseDouble(value, options.stateChangeRate, error)) {
                return false;
            }
            workloadParametersSpecified = true;
        } else if (argument == "--text-length") {
            if (!parseInteger(value, options.textLength, error)) {
                return false;
            }
            workloadParametersSpecified = true;
        } else if (argument == "--backend") {
            if (std::string_view(value) != "opengl") {
                error = "NanoVG adapter only supports the OpenGL backend";
                return false;
            }
        } else {
            error = "unknown option '" + argument + "'";
            return false;
        }
    }

    const int requestedFrames = options.frames;
    const int requestedWarmup = options.warmup;
    if (!applyProfile(options, error)) {
        return false;
    }
    if (framesOverridden) {
        options.frames = requestedFrames;
    }
    if (warmupOverridden) {
        options.warmup = requestedWarmup;
    }
    if (options.listScenes) {
        return true;
    }
    if (std::find(kScenes.begin(), kScenes.end(), options.scene)
        == kScenes.end()) {
        error = "unsupported or missing --scene";
        return false;
    }
    if (options.outputPath.empty() || options.captureDirectory.empty()) {
        error = "--output and --capture-dir are required";
        return false;
    }
    if (options.frames <= 0 || options.warmup < 0) {
        error = "invalid frame or warmup count";
        return false;
    }
    if (options.operations < 0 || options.operations > 200000
        || options.textureCount <= 0 || options.textureCount > 256
        || options.roundedRatio < 0.0 || options.roundedRatio > 1.0
        || options.stateChangeRate < 0.0
        || options.stateChangeRate > 1.0
        || options.textLength <= 0 || options.textLength > 1024) {
        error = "invalid parameterized workload option";
        return false;
    }
    if (workloadParametersSpecified
        && workloadModeSpecified
        && options.workloadMode == WorkloadMode::Fixed) {
        error = "--workload fixed cannot be combined with workload parameters";
        return false;
    }
    if (workloadParametersSpecified
        && options.workloadMode == WorkloadMode::Fixed) {
        options.workloadMode = WorkloadMode::Stable;
    }
    options.workloadCustomized =
        workloadParametersSpecified
        || options.workloadMode != WorkloadMode::Fixed;
    if (options.width <= 0 || options.height <= 0
        || options.width > kMaximumDimension
        || options.height > kMaximumDimension
        || static_cast<std::uint64_t>(options.width)
            * static_cast<std::uint64_t>(options.height)
            > kMaximumPixels) {
        error = "invalid or unreasonably large dimensions";
        return false;
    }
    return true;
}

double percentile(const std::vector<double> &sorted, double value)
{
    if (sorted.empty()) {
        return 0.0;
    }
    const double position = value * static_cast<double>(sorted.size() - 1u);
    const std::size_t lower = static_cast<std::size_t>(position);
    const std::size_t upper =
        std::min(lower + 1u, sorted.size() - 1u);
    const double fraction = position - static_cast<double>(lower);
    return sorted[lower] * (1.0 - fraction) + sorted[upper] * fraction;
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
    for (const double sample : samples) {
        result.mean += sample;
    }
    result.mean /= static_cast<double>(samples.size());
    result.median = percentile(samples, 0.5);
    result.p95 = percentile(samples, 0.95);
    return result;
}

void appendDistribution(
    std::ostringstream &json, std::string_view name,
    const Distribution &distribution)
{
    json << ",\"" << name << "_min_ms\":" << distribution.minimum
         << ",\"" << name << "_max_ms\":" << distribution.maximum
         << ",\"" << name << "_mean_ms\":" << distribution.mean
         << ",\"" << name << "_median_ms\":" << distribution.median
         << ",\"" << name << "_p95_ms\":" << distribution.p95;
}

void appendSamples(
    std::ostringstream &json, std::string_view name,
    const std::vector<double> &samples)
{
    json << ",\"" << name << "\":[";
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (index > 0u) {
            json << ',';
        }
        json << samples[index];
    }
    json << ']';
}

std::uint64_t hashPixels(const std::vector<unsigned char> &rgba)
{
    std::uint64_t hash = UINT64_C(1469598103934665603);
    for (const unsigned char value : rgba) {
        hash ^= value;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

std::string formatHash(std::uint64_t hash)
{
    std::ostringstream value;
    value << std::hex << std::setfill('0') << std::setw(16) << hash;
    return value.str();
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

std::string environmentValue(const char *name)
{
    const char *value = std::getenv(name);
    return value != nullptr ? value : "";
}

NVGcolor color(
    int red, int green, int blue, int alpha = 255)
{
    return nvgRGBA(
        static_cast<unsigned char>(red),
        static_cast<unsigned char>(green),
        static_cast<unsigned char>(blue),
        static_cast<unsigned char>(alpha));
}

std::uint32_t workloadRandom(
    std::uint32_t seed, std::uint32_t index,
    std::uint32_t stream, std::uint32_t frame)
{
    std::uint32_t value =
        seed ^ (index * 0x9e3779b9u)
        ^ (stream * 0x85ebca6bu)
        ^ (frame * 0xc2b2ae35u);
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float workloadUnit(
    std::uint32_t seed, std::uint32_t index,
    std::uint32_t stream, std::uint32_t frame)
{
    return static_cast<float>(
        workloadRandom(seed, index, stream, frame) & 0x00ffffffu)
        / static_cast<float>(0x01000000u);
}

std::pair<int, int> workloadGrid(
    int operations, int width, int height)
{
    const double aspect =
        static_cast<double>(std::max(1, width))
        / static_cast<double>(std::max(1, height));
    const int columns = std::max(
        1, static_cast<int>(std::ceil(std::sqrt(
            static_cast<double>(operations) * aspect))));
    const int rows = std::max(
        1, (operations + columns - 1) / columns);
    return {columns, rows};
}

void applyWorkloadState(
    NVGcontext *context, const Options &options,
    std::uint32_t index, std::uint32_t structureFrame)
{
    if (workloadUnit(
            options.seed, index, 71u, structureFrame)
        >= options.stateChangeRate) {
        nvgGlobalCompositeOperation(context, NVG_SOURCE_OVER);
        return;
    }
    switch (workloadRandom(
        options.seed, index, 73u, structureFrame) % 3u) {
    case 0:
        nvgGlobalCompositeBlendFuncSeparate(
            context, NVG_SRC_ALPHA, NVG_ONE, NVG_ONE, NVG_ONE);
        break;
    case 1:
        nvgGlobalCompositeBlendFuncSeparate(
            context, NVG_DST_COLOR, NVG_ZERO, NVG_DST_ALPHA, NVG_ZERO);
        break;
    default:
        nvgGlobalCompositeBlendFuncSeparate(
            context, NVG_ONE, NVG_ONE_MINUS_SRC_COLOR,
            NVG_ONE, NVG_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

void fillCurrentPath(NVGcontext *context, NVGcolor value)
{
    nvgFillColor(context, value);
    nvgFill(context);
}

void drawBackground(
    NVGcontext *context, const Options &options, NVGcolor value)
{
    nvgGlobalCompositeOperation(context, NVG_SOURCE_OVER);
    nvgBeginPath(context);
    nvgRect(
        context, 0.0f, 0.0f,
        static_cast<float>(options.width),
        static_cast<float>(options.height));
    fillCurrentPath(context, value);
}

void drawGeometryStress(
    NVGcontext *context, const Options &options, int frame)
{
    drawBackground(context, options, color(13, 17, 25));
    constexpr int columns = 64;
    constexpr int rows = 36;
    const float cellWidth = static_cast<float>(options.width) / columns;
    const float cellHeight = static_cast<float>(options.height) / rows;
    const float inset =
        std::max(1.0f, std::min(cellWidth, cellHeight) * 0.12f);
    const float phase = static_cast<float>(frame % 8) * 0.0625f;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int index = row * columns + column;
            const float left = column * cellWidth + inset + phase;
            const float top = row * cellHeight + inset;
            const float width = cellWidth - inset * 2.0f;
            const float height = cellHeight - inset * 2.0f;
            nvgBeginPath(context);
            switch (index % 6) {
            case 0:
                nvgRect(context, left, top, width, height);
                break;
            case 1:
                nvgRoundedRect(
                    context, left, top, width, height,
                    std::min(width, height) * 0.24f);
                break;
            case 2:
                nvgCircle(
                    context, left + width * 0.5f,
                    top + height * 0.5f,
                    std::min(width, height) * 0.45f);
                break;
            case 3:
                nvgEllipse(
                    context, left + width * 0.5f,
                    top + height * 0.5f,
                    width * 0.5f, height * 0.5f);
                break;
            case 4:
                nvgMoveTo(context, left + width * 0.5f, top);
                nvgLineTo(context, left + width, top + height);
                nvgLineTo(context, left, top + height);
                nvgClosePath(context);
                break;
            default:
                nvgMoveTo(context, left + width * 0.5f, top);
                nvgLineTo(
                    context, left + width, top + height * 0.5f);
                nvgLineTo(
                    context, left + width * 0.5f, top + height);
                nvgLineTo(context, left, top + height * 0.5f);
                nvgClosePath(context);
                break;
            }
            fillCurrentPath(
                context,
                color(
                    35 + (index * 37) % 205,
                    45 + (index * 53) % 195,
                    55 + (index * 71) % 185,
                    160 + (index * 17) % 96));
        }
    }
}

void drawGeometryWorkload(
    NVGcontext *context, const Options &options, int frame)
{
    drawBackground(context, options, color(13, 17, 25));
    const int operations =
        options.operations > 0 ? options.operations : 2304;
    const auto [columns, rows] =
        workloadGrid(operations, options.width, options.height);
    const float cellWidth =
        static_cast<float>(options.width) / columns;
    const float cellHeight =
        static_cast<float>(options.height) / rows;
    const float inset =
        std::max(0.25f, std::min(cellWidth, cellHeight) * 0.12f);
    const std::uint32_t dataFrame =
        options.workloadMode == WorkloadMode::Stable
        ? 0u : static_cast<std::uint32_t>(frame);
    const std::uint32_t structureFrame =
        options.workloadMode == WorkloadMode::DynamicStructure
        ? static_cast<std::uint32_t>(frame) : 0u;
    for (int operation = 0; operation < operations; ++operation) {
        const int column = operation % columns;
        const int row = operation / columns;
        const std::uint32_t index =
            static_cast<std::uint32_t>(operation);
        const float left =
            column * cellWidth + inset
            + (workloadUnit(
                   options.seed, index, 1u, dataFrame) - 0.5f)
                * cellWidth * 0.16f;
        const float top =
            row * cellHeight + inset
            + (workloadUnit(
                   options.seed, index, 2u, dataFrame) - 0.5f)
                * cellHeight * 0.16f;
        const float width =
            std::max(0.5f, cellWidth - inset * 2.0f);
        const float height =
            std::max(0.5f, cellHeight - inset * 2.0f);
        applyWorkloadState(context, options, index, structureFrame);
        nvgBeginPath(context);
        switch (workloadRandom(
            options.seed, index, 7u, structureFrame) % 6u) {
        case 0:
            nvgRect(context, left, top, width, height);
            break;
        case 1:
            nvgRoundedRect(
                context, left, top, width, height,
                std::min(width, height) * 0.24f);
            break;
        case 2:
            nvgCircle(
                context, left + width * 0.5f,
                top + height * 0.5f,
                std::min(width, height) * 0.45f);
            break;
        case 3:
            nvgEllipse(
                context, left + width * 0.5f,
                top + height * 0.5f,
                width * 0.5f, height * 0.5f);
            break;
        case 4:
            nvgMoveTo(context, left + width * 0.5f, top);
            nvgLineTo(context, left + width, top + height);
            nvgLineTo(context, left, top + height);
            nvgClosePath(context);
            break;
        default:
            nvgMoveTo(context, left + width * 0.5f, top);
            nvgLineTo(
                context, left + width, top + height * 0.5f);
            nvgLineTo(
                context, left + width * 0.5f, top + height);
            nvgLineTo(context, left, top + height * 0.5f);
            nvgClosePath(context);
            break;
        }
        fillCurrentPath(
            context,
            color(
                35 + static_cast<int>(workloadRandom(
                    options.seed, index, 3u, dataFrame) % 205u),
                45 + static_cast<int>(workloadRandom(
                    options.seed, index, 4u, dataFrame) % 195u),
                55 + static_cast<int>(workloadRandom(
                    options.seed, index, 5u, dataFrame) % 185u),
                160 + static_cast<int>(workloadRandom(
                    options.seed, index, 6u, dataFrame) % 96u)));
    }
}

void drawImageGrid(
    NVGcontext *context, const Resources &resources,
    const Options &options, int frame)
{
    drawBackground(context, options, color(22, 26, 34));
    constexpr int columns = 12;
    constexpr int rows = 8;
    const float cellWidth = static_cast<float>(options.width) / columns;
    const float cellHeight = static_cast<float>(options.height) / rows;
    const float wobble = static_cast<float>(frame % 5) * 0.15f;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int index = row * columns + column;
            const float alpha =
                0.68f + static_cast<float>(index % 4) * 0.08f;
            const float inset = 4.0f + static_cast<float>(index % 3);
            const float left = column * cellWidth + inset + wobble;
            const float top = row * cellHeight + inset;
            const float width = cellWidth - inset * 2.0f;
            const float height = cellHeight - inset * 2.0f;
            nvgBeginPath(context);
            if (index % 3 == 0) {
                nvgRoundedRect(
                    context, left, top, width, height, 7.0f);
            } else {
                nvgRect(context, left, top, width, height);
            }
            nvgFillPaint(
                context,
                nvgImagePattern(
                    context, left, top, width, height, 0.0f,
                    resources.image, alpha));
            nvgFill(context);
        }
    }
}

void drawImageWorkload(
    NVGcontext *context, const Resources &resources,
    const Options &options, int frame)
{
    drawBackground(context, options, color(22, 26, 34));
    const int operations =
        options.operations > 0 ? options.operations : 96;
    const auto [columns, rows] =
        workloadGrid(operations, options.width, options.height);
    const float cellWidth =
        static_cast<float>(options.width) / columns;
    const float cellHeight =
        static_cast<float>(options.height) / rows;
    const std::uint32_t dataFrame =
        options.workloadMode == WorkloadMode::Stable
        ? 0u : static_cast<std::uint32_t>(frame);
    const std::uint32_t structureFrame =
        options.workloadMode == WorkloadMode::DynamicStructure
        ? static_cast<std::uint32_t>(frame) : 0u;
    for (int operation = 0; operation < operations; ++operation) {
        const int column = operation % columns;
        const int row = operation / columns;
        const std::uint32_t index =
            static_cast<std::uint32_t>(operation);
        const float inset = std::max(
            0.5f, std::min(cellWidth, cellHeight)
                * (0.04f + workloadUnit(
                    options.seed, index, 11u, 0u) * 0.04f));
        const float left =
            column * cellWidth + inset
            + (workloadUnit(
                   options.seed, index, 12u, dataFrame) - 0.5f)
                * cellWidth * 0.12f;
        const float top =
            row * cellHeight + inset
            + (workloadUnit(
                   options.seed, index, 13u, dataFrame) - 0.5f)
                * cellHeight * 0.12f;
        const float width =
            std::max(0.5f, cellWidth - inset * 2.0f);
        const float height =
            std::max(0.5f, cellHeight - inset * 2.0f);
        const float alpha =
            0.62f + workloadUnit(
                options.seed, index, 14u, dataFrame) * 0.38f;
        applyWorkloadState(context, options, index, structureFrame);
        const int image =
            resources.workloadImages.empty()
            ? resources.image
            : resources.workloadImages[
                static_cast<std::size_t>(workloadRandom(
                    options.seed, index, 15u, structureFrame))
                % resources.workloadImages.size()];
        const bool rounded =
            workloadUnit(
                options.seed, index, 16u, structureFrame)
            < options.roundedRatio;
        nvgBeginPath(context);
        if (rounded) {
            nvgRoundedRect(
                context, left, top, width, height,
                std::min(width, height) * 0.12f);
        } else {
            nvgRect(context, left, top, width, height);
        }
        nvgFillPaint(
            context,
            nvgImagePattern(
                context, left, top, width, height, 0.0f,
                image, alpha));
        nvgFill(context);
    }
}

void drawContractTextLatin(
    NVGcontext *context, const Resources &resources,
    const Options &options)
{
    drawBackground(context, options, color(247, 248, 251));
    constexpr int columns = 8;
    constexpr int rows = 72;
    static constexpr std::array<std::string_view, 6> samples = {{
        "Canvas rendering Aa 123",
        "OpenType shaping AV fi",
        "Glyph atlas 0123456789",
        "Vector paths and images",
        "Quick brown fox 24680",
        "Reusable 2D render batch",
    }};
    const float columnWidth = static_cast<float>(options.width) / columns;
    const float rowHeight = static_cast<float>(options.height) / rows;
    const float textSize =
        std::clamp(rowHeight * 0.76f, 10.0f, 18.0f);
    nvgFontFaceId(context, resources.font);
    // NanoVG's font-size unit maps to a larger Roboto em box than
    // WhatsCanvas's pixel-height contract. This fixed conversion aligns the
    // rendered cap/x-height without changing the font or scene workload.
    nvgFontSize(context, textSize * 0.875f);
    nvgTextAlign(context, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int index = row * columns + column;
            nvgFillColor(
                context,
                color(
                    18 + (index * 13) % 82,
                    28 + (index * 17) % 92,
                    42 + (index * 19) % 108));
            const std::string_view sample =
                samples[static_cast<std::size_t>(index) % samples.size()];
            nvgText(
                context,
                column * columnWidth + 4.0f,
                (row + 0.82f) * rowHeight + textSize * 0.35f,
                sample.data(), sample.data() + sample.size());
        }
    }
}

void drawTextWorkload(
    NVGcontext *context, const Resources &resources,
    const Options &options, int frame)
{
    drawBackground(context, options, color(247, 248, 251));
    const int operations =
        options.operations > 0 ? options.operations : 576;
    const auto [columns, rows] =
        workloadGrid(operations, options.width, options.height);
    const float columnWidth =
        static_cast<float>(options.width) / columns;
    const float rowHeight =
        static_cast<float>(options.height) / rows;
    const float textSize =
        std::clamp(rowHeight * 0.76f, 6.0f, 24.0f);
    const std::uint32_t dataFrame =
        options.workloadMode == WorkloadMode::Stable
        ? 0u : static_cast<std::uint32_t>(frame);
    const std::uint32_t structureFrame =
        options.workloadMode == WorkloadMode::DynamicStructure
        ? static_cast<std::uint32_t>(frame) : 0u;
    nvgFontFaceId(context, resources.font);
    nvgTextAlign(context, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    for (int operation = 0; operation < operations; ++operation) {
        const int column = operation % columns;
        const int row = operation / columns;
        const std::uint32_t index =
            static_cast<std::uint32_t>(operation);
        static constexpr std::array<float, 3> sizeBuckets = {
            0.9f, 1.0f, 1.1f};
        const float sizeVariation =
            options.workloadMode == WorkloadMode::DynamicStructure
            ? sizeBuckets[static_cast<std::size_t>(workloadRandom(
                options.seed, index, 24u, structureFrame)
                % sizeBuckets.size())]
            : 1.0f;
        const float effectiveSize = textSize * sizeVariation;
        nvgFontSize(context, effectiveSize * 0.875f);
        applyWorkloadState(context, options, index, structureFrame);
        nvgFillColor(
            context,
            color(
                18 + static_cast<int>(workloadRandom(
                    options.seed, index, 21u, dataFrame) % 82u),
                28 + static_cast<int>(workloadRandom(
                    options.seed, index, 22u, dataFrame) % 92u),
                42 + static_cast<int>(workloadRandom(
                    options.seed, index, 23u, dataFrame) % 108u)));
        const std::size_t variant =
            resources.workloadTextVariants.empty()
            ? 0u
            : static_cast<std::size_t>(workloadRandom(
                options.seed, index, 26u,
                options.workloadMode == WorkloadMode::Stable
                    ? 0u : dataFrame))
                % resources.workloadTextVariants.size();
        static const std::string fallback = "Canvas text Aa 123";
        const std::string &text =
            resources.workloadTextVariants.empty()
            ? fallback : resources.workloadTextVariants[variant];
        nvgText(
            context,
            column * columnWidth + 2.0f,
            (row + 0.82f) * rowHeight + effectiveSize * 0.35f,
            text.data(), text.data() + text.size());
    }
}

void drawScene(
    NVGcontext *context, const Resources &resources,
    const Options &options, int frame)
{
    nvgBeginFrame(
        context, static_cast<float>(options.width),
        static_cast<float>(options.height), 1.0f);
    if (options.scene == "geometry_stress") {
        if (options.workloadCustomized) {
            drawGeometryWorkload(context, options, frame);
        } else {
            drawGeometryStress(context, options, frame);
        }
    } else if (options.scene == "image_grid") {
        if (options.workloadCustomized) {
            drawImageWorkload(context, resources, options, frame);
        } else {
            drawImageGrid(context, resources, options, frame);
        }
    } else if (options.workloadCustomized) {
        drawTextWorkload(context, resources, options, frame);
    } else {
        drawContractTextLatin(context, resources, options);
    }
}

FrameTiming renderFrame(
    NVGcontext *context, const Resources &resources,
    const Options &options, int frame)
{
    // Stencil maintenance is not part of the Canvas workload. Color clearing
    // is an opaque full-frame src-over draw in every adapter.
    glClear(GL_STENCIL_BUFFER_BIT);
    glFinish();
    const Clock::time_point start = Clock::now();
    drawScene(context, resources, options, frame);
    const Clock::time_point recorded = Clock::now();
    nvgEndFrame(context);
    glFinish();
    const Clock::time_point finished = Clock::now();
    return {
        std::chrono::duration<double, std::milli>(
            recorded - start).count(),
        std::chrono::duration<double, std::milli>(
            finished - recorded).count(),
        std::chrono::duration<double, std::milli>(
            finished - start).count(),
    };
}

bool createResources(
    NVGcontext *context, const Options &options,
    Resources &resources, std::string &error)
{
    constexpr int width = 128;
    constexpr int height = 128;
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * height * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * width + x) * 4u;
            const bool checker = ((x / 16) + (y / 16)) % 2 == 0;
            pixels[index] = static_cast<unsigned char>(
                checker ? 235 : (x * 255) / (width - 1));
            pixels[index + 1u] = static_cast<unsigned char>(
                checker ? (y * 255) / (height - 1) : 75);
            pixels[index + 2u] =
                static_cast<unsigned char>(checker ? 90 : 220);
            pixels[index + 3u] =
                static_cast<unsigned char>(170 + ((x + y) % 86));
        }
    }
    resources.image = nvgCreateImageRGBA(
        context, width, height, 0, pixels.data());
    if (resources.image == 0) {
        error = "unable to create NanoVG benchmark image";
        return false;
    }
    if (options.workloadCustomized
        && options.scene == "image_grid") {
        constexpr int workloadWidth = 64;
        constexpr int workloadHeight = 64;
        std::vector<unsigned char> workloadPixels(
            static_cast<std::size_t>(workloadWidth)
            * workloadHeight * 4u);
        resources.workloadImages.reserve(
            static_cast<std::size_t>(options.textureCount));
        for (int texture = 0;
             texture < options.textureCount; ++texture) {
            for (int y = 0; y < workloadHeight; ++y) {
                for (int x = 0; x < workloadWidth; ++x) {
                    const std::size_t pixel =
                        (static_cast<std::size_t>(y)
                             * workloadWidth
                         + static_cast<std::size_t>(x)) * 4u;
                    const std::uint32_t texel =
                        static_cast<std::uint32_t>(
                            y * workloadWidth + x);
                    workloadPixels[pixel] =
                        static_cast<unsigned char>(workloadRandom(
                            options.seed,
                            static_cast<std::uint32_t>(texture),
                            31u, texel) & 0xffu);
                    workloadPixels[pixel + 1u] =
                        static_cast<unsigned char>(workloadRandom(
                            options.seed,
                            static_cast<std::uint32_t>(texture),
                            32u, texel) & 0xffu);
                    workloadPixels[pixel + 2u] =
                        static_cast<unsigned char>(workloadRandom(
                            options.seed,
                            static_cast<std::uint32_t>(texture),
                            33u, texel) & 0xffu);
                    workloadPixels[pixel + 3u] =
                        static_cast<unsigned char>(
                            160u + (workloadRandom(
                                options.seed,
                                static_cast<std::uint32_t>(texture),
                                34u, texel) % 96u));
                }
            }
            const int image = nvgCreateImageRGBA(
                context, workloadWidth, workloadHeight, 0,
                workloadPixels.data());
            if (image == 0) {
                error = "unable to create NanoVG workload image "
                    + std::to_string(texture);
                return false;
            }
            resources.workloadImages.push_back(image);
        }
    }
    if (options.workloadCustomized
        && options.scene == "contract_text_latin") {
        static constexpr std::string_view alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789 .,;:!?+-*/()[]";
        const int variantCount = std::min(
            256, std::max(
                16, options.operations > 0
                    ? options.operations : 576));
        resources.workloadTextVariants.reserve(
            static_cast<std::size_t>(variantCount));
        for (int variant = 0; variant < variantCount; ++variant) {
            int length = options.textLength;
            if (options.workloadMode
                == WorkloadMode::DynamicStructure) {
                length = std::max(
                    1, length / 2
                        + static_cast<int>(workloadRandom(
                            options.seed,
                            static_cast<std::uint32_t>(variant),
                            41u, 0u)
                            % static_cast<std::uint32_t>(
                                std::max(1, length))));
            }
            std::string text;
            text.reserve(static_cast<std::size_t>(length));
            for (int character = 0;
                 character < length; ++character) {
                text.push_back(
                    alphabet[static_cast<std::size_t>(workloadRandom(
                        options.seed,
                        static_cast<std::uint32_t>(variant),
                        42u,
                        static_cast<std::uint32_t>(character)))
                        % alphabet.size()]);
            }
            resources.workloadTextVariants.push_back(std::move(text));
        }
    }
    if (options.scene == "contract_text_latin") {
        const std::string runtimeContractFont =
            environmentValue("WHATSCANVAS_CROSS_LIBRARY_FONT_PATH");
        const std::filesystem::path font =
            runtimeContractFont.empty()
            ? std::filesystem::path(
                WHATSCANVAS_NANOVG_CONTRACT_FONT)
            : std::filesystem::path(runtimeContractFont);
        resources.font =
            nvgCreateFont(context, "CrossLibraryRoboto", font.string().c_str());
        if (resources.font < 0) {
            error = "unable to load contract font: " + font.string();
            return false;
        }
    }
    return true;
}

std::vector<unsigned char> readPixels(
    int width, int height)
{
    std::vector<unsigned char> bottomUp(
        static_cast<std::size_t>(width) * height * 4u);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(
        0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
        bottomUp.data());
    std::vector<unsigned char> topDown(bottomUp.size());
    const std::size_t stride = static_cast<std::size_t>(width) * 4u;
    for (int row = 0; row < height; ++row) {
        std::copy_n(
            bottomUp.data()
                + static_cast<std::size_t>(height - 1 - row) * stride,
            stride,
            topDown.data() + static_cast<std::size_t>(row) * stride);
    }
    return topDown;
}

bool savePpm(
    const std::filesystem::path &path,
    const std::vector<unsigned char> &rgba,
    int width, int height, std::string &error)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "unable to create capture: " + path.string();
        return false;
    }
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (std::size_t index = 0; index + 3u < rgba.size(); index += 4u) {
        output.write(
            reinterpret_cast<const char *>(rgba.data() + index), 3);
    }
    return static_cast<bool>(output);
}

std::string metadataJson(
    const Options &options, double initializationMs)
{
    const std::string contractVersion = environmentValue(
        "WHATSCANVAS_CROSS_LIBRARY_CONTRACT_VERSION");
    const char *renderer = reinterpret_cast<const char *>(
        glGetString(GL_RENDERER));
    const char *vendor = reinterpret_cast<const char *>(
        glGetString(GL_VENDOR));
    const char *driver = reinterpret_cast<const char *>(
        glGetString(GL_VERSION));
    std::ostringstream json;
    json << std::fixed << std::setprecision(6)
         << "{\"type\":\"metadata\",\"schema\":1"
         << ",\"suite\":\"WhatsCanvasCrossLibraryBenchmark\""
         << ",\"library\":\"NanoVG\""
         << ",\"library_version\":\""
         << jsonEscape(WHATSCANVAS_NANOVG_VERSION) << "\""
         << ",\"synchronization\":\"gpu_complete\""
         << ",\"cross_library_contract\":\""
         << jsonEscape(
                contractVersion.empty() ? "1.2.0" : contractVersion)
         << "\""
         << ",\"clear_semantics\":\"draw_full_frame_src_over\""
         << ",\"font_sha256\":\""
         << jsonEscape(environmentValue(
                "WHATSCANVAS_CROSS_LIBRARY_FONT_SHA256"))
         << "\""
         << ",\"text_shaping_mode\":\"adapter_native_latin_kerning\""
         << ",\"text_raster_mode\":\"grayscale_no_lcd\""
         << ",\"backend\":\"opengl\""
         << ",\"device\":\"" << jsonEscape(renderer ? renderer : "unknown")
         << "\",\"device_vendor\":\""
         << jsonEscape(vendor ? vendor : "unknown")
         << "\",\"driver\":\""
         << jsonEscape(driver ? driver : "unknown")
         << "\",\"os\":\"" << osName()
         << "\",\"architecture\":\"" << architectureName()
         << "\",\"cpu\":\""
         << jsonEscape(environmentValue("PROCESSOR_IDENTIFIER"))
         << "\",\"compiler\":\"" << compilerName()
         << "\",\"compiler_version\":\""
         << jsonEscape(compilerVersion())
         << "\",\"build_type\":\""
         << jsonEscape(WHATSCANVAS_NANOVG_BUILD_TYPE)
         << "\",\"profile\":\"" << jsonEscape(options.profile)
         << "\",\"width\":" << options.width
         << ",\"height\":" << options.height
         << ",\"frames\":" << options.frames
         << ",\"warmup\":" << options.warmup
         << ",\"workload_mode\":\""
         << workloadModeName(options.workloadMode) << "\""
         << ",\"workload_seed\":" << options.seed
         << ",\"workload_operations\":" << options.operations
         << ",\"workload_texture_count\":" << options.textureCount
         << ",\"workload_rounded_ratio\":" << options.roundedRatio
         << ",\"workload_state_change_rate\":"
         << options.stateChangeRate
         << ",\"workload_text_length\":" << options.textLength
         << ",\"initialization_ms\":" << initializationMs
         << "}";
    return json.str();
}

std::string resultJson(
    const Options &options, const FrameTiming &cold,
    const Distribution &record, const Distribution &submit,
    const Distribution &total, const std::vector<unsigned char> &pixels,
    double readbackMs, const std::vector<double> &recordSamples,
    const std::vector<double> &submitSamples,
    const std::vector<double> &totalSamples)
{
    const std::size_t defaultOperations =
        options.scene == "image_grid" ? 96u
        : options.scene == "contract_text_latin" ? 576u : 2304u;
    const std::size_t operations =
        options.workloadCustomized && options.operations > 0
        ? static_cast<std::size_t>(options.operations)
        : defaultOperations;
    std::ostringstream json;
    json << std::fixed << std::setprecision(6)
         << "{\"type\":\"result\",\"schema\":1"
         << ",\"backend\":\"opengl\""
         << ",\"scene\":\"" << jsonEscape(options.scene)
         << "\",\"width\":" << options.width
         << ",\"height\":" << options.height
         << ",\"frames\":" << options.frames
         << ",\"warmup\":" << options.warmup
         << ",\"workload_mode\":\""
         << workloadModeName(options.workloadMode) << "\""
         << ",\"workload_seed\":" << options.seed
         << ",\"texture_count\":"
         << (options.scene == "image_grid"
                 && options.workloadCustomized
             ? options.textureCount : 1)
         << ",\"rounded_ratio\":"
         << (options.scene == "image_grid"
                 && options.workloadCustomized
             ? options.roundedRatio : 0.0)
         << ",\"state_change_rate\":"
         << (options.workloadCustomized
             ? options.stateChangeRate : 0.0)
         << ",\"text_length\":"
         << (options.scene == "contract_text_latin"
                 && options.workloadCustomized
             ? options.textLength : 0)
         << ",\"operations_per_frame\":" << operations
         << ",\"cold_total_ms\":" << cold.totalMs;
    appendDistribution(json, "record", record);
    appendDistribution(json, "submit", submit);
    appendDistribution(json, "total", total);
    appendSamples(json, "record_samples_ms", recordSamples);
    appendSamples(json, "submit_samples_ms", submitSamples);
    appendSamples(json, "total_samples_ms", totalSamples);
    json << ",\"fps\":"
         << (total.median > 0.0 ? 1000.0 / total.median : 0.0)
         << ",\"operations_per_second\":"
         << (total.median > 0.0
                 ? static_cast<double>(operations) * 1000.0 / total.median
                 : 0.0)
         << ",\"readback_ms\":" << readbackMs
         << ",\"readback_bytes\":" << pixels.size()
         << ",\"pixel_hash\":\"" << formatHash(hashPixels(pixels)) << "\""
         << "}";
    return json.str();
}

} // namespace

int main(int argc, char **argv)
{
    Options options;
    std::string error;
    if (!parseOptions(argc, argv, options, error)) {
        std::cerr << "NANOVG_BENCHMARK_ERROR: " << error << '\n';
        return 2;
    }
    if (options.listScenes) {
        for (const std::string_view scene : kScenes) {
            std::cout << scene << '\n';
        }
        return 0;
    }

    const Clock::time_point initializationStart = Clock::now();
    if (!glfwInit()) {
        std::cerr << "NANOVG_BENCHMARK_ERROR: glfwInit failed\n";
        return 3;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    GLFWwindow *window = glfwCreateWindow(
        options.width, options.height,
        "NanoVG cross-library benchmark", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "NANOVG_BENCHMARK_ERROR: context creation failed\n";
        glfwTerminate();
        return 3;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
    if (!gladLoadGLLoader(
            reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "NANOVG_BENCHMARK_ERROR: GLAD initialization failed\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 3;
    }
    glViewport(0, 0, options.width, options.height);
    NVGcontext *context =
        nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (context == nullptr) {
        std::cerr << "NANOVG_BENCHMARK_ERROR: nvgCreateGL3 failed\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 3;
    }

    Resources resources;
    if (!createResources(context, options, resources, error)) {
        std::cerr << "NANOVG_BENCHMARK_ERROR: " << error << '\n';
        nvgDeleteGL3(context);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 4;
    }
    const double initializationMs =
        std::chrono::duration<double, std::milli>(
            Clock::now() - initializationStart).count();

    const FrameTiming cold =
        renderFrame(context, resources, options, 0);
    for (int frame = 0; frame < options.warmup; ++frame) {
        renderFrame(context, resources, options, frame);
    }

    std::vector<double> recordSamples;
    std::vector<double> submitSamples;
    std::vector<double> totalSamples;
    recordSamples.reserve(static_cast<std::size_t>(options.frames));
    submitSamples.reserve(static_cast<std::size_t>(options.frames));
    totalSamples.reserve(static_cast<std::size_t>(options.frames));
    for (int frame = 0; frame < options.frames; ++frame) {
        const FrameTiming timing =
            renderFrame(context, resources, options, frame);
        recordSamples.push_back(timing.recordMs);
        submitSamples.push_back(timing.submitMs);
        totalSamples.push_back(timing.totalMs);
    }

    renderFrame(context, resources, options, 0);
    const Clock::time_point readbackStart = Clock::now();
    const std::vector<unsigned char> pixels =
        readPixels(options.width, options.height);
    const double readbackMs =
        std::chrono::duration<double, std::milli>(
            Clock::now() - readbackStart).count();

    std::error_code filesystemError;
    std::filesystem::create_directories(
        options.captureDirectory, filesystemError);
    if (filesystemError) {
        error = "unable to create capture directory: "
            + filesystemError.message();
    }
    const std::filesystem::path capture =
        std::filesystem::path(options.captureDirectory)
        / ("opengl_" + options.scene + ".ppm");
    if (error.empty()
        && !savePpm(
            capture, pixels, options.width, options.height, error)) {
        error = "failed to write capture: " + error;
    }

    const std::string metadata =
        metadataJson(options, initializationMs);
    const std::string result = resultJson(
        options, cold, summarize(recordSamples),
        summarize(submitSamples),
        summarize(totalSamples), pixels, readbackMs,
        recordSamples, submitSamples, totalSamples);
    if (error.empty()) {
        std::ofstream output(options.outputPath);
        if (!output) {
            error = "unable to create output: " + options.outputPath;
        } else {
            output << metadata << '\n' << result << '\n';
        }
    }

    nvgDeleteGL3(context);
    glfwDestroyWindow(window);
    glfwTerminate();
    if (!error.empty()) {
        std::cerr << "NANOVG_BENCHMARK_ERROR: " << error << '\n';
        return 5;
    }
    std::cout << "PERF_METADATA " << metadata << '\n'
              << "PERF_RESULT " << result << '\n';
    return 0;
}
