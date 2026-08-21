#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

#include "SceneCatalog.h"
#include "hosts/glfw/GlfwHost.h"
#include "hosts/software/SoftwareRuntime.h"

namespace {

enum class BackendChoice { OpenGL, Software };

struct Args
{
    std::string scene = whatscanvas::desktop::SceneCatalog::defaultName();
    BackendChoice backend = BackendChoice::OpenGL;
    whatscanvas::scenes::ViewportStandard viewportStandard =
        whatscanvas::scenes::ViewportStandard::Phone2To1;
    int width = 1280;
    int height = 720;
    bool disableMsaa = false;
    bool noVsync = false;
    bool listScenes = false;
    bool showHelp = false;
    // Dump mode
    std::string dumpPath;
    int dumpFrames = 1;
    float dumpTimeSeconds = -1.0f;
    float dumpDevicePixelRatio = 1.0f;
    // Benchmark mode
    bool benchmark = false;
    int benchmarkWarmup = 30;
    int benchmarkMeasured = 300;
};

bool startsWith(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool parseInt(std::string_view text, int& out)
{
    if (text.empty()) return false;
    const std::string valueText(text);
    char* end = nullptr;
    const long value = std::strtol(valueText.c_str(), &end, 10);
    if (end == valueText.c_str() || *end != '\0'
        || value < std::numeric_limits<int>::min()
        || value > std::numeric_limits<int>::max()) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

bool parseFloat(std::string_view text, float& out)
{
    if (text.empty()) return false;
    const std::string valueText(text);
    char* end = nullptr;
    const float value = std::strtof(valueText.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(value) || value < 0.0f) {
        return false;
    }
    out = value;
    return true;
}

void printUsage()
{
    std::fprintf(stdout,
        "WhatsCanvas Desktop host\n"
        "\n"
        "Usage:\n"
        "  WhatsCanvasDesktopHost [--scene=<name>] [--w=<px>] [--h=<px>]\n"
        "                        [--no-msaa] [--no-vsync]\n"
        "                        [--dump-png=<path.ppm>] [--frames=<n>] [--dpr=<scale>]\n"
        "                        [--list-scenes] [--help]\n"
        "\n"
        "Options:\n"
        "  --scene=<name>     Scene to run (default: %s).\n"
        "  --backend=<name>   'gl' (default) uses GLFW + OpenGL 3.3;\n"
        "                     'software' uses the dependency-free CPU backend\n"
        "                     (headless, no display, dump-png only).\n"
        "  --viewport-standard=<id>  phone_2_1 (default), phone_16_9,\n"
        "                     tablet_4_3, desktop_16_10, or legacy_android.\n"
        "  --w=<px>           Window / dump width in pixels (default: 1280).\n"
        "  --h=<px>           Window / dump height in pixels (default: 720).\n"
        "  --no-msaa          Disable MSAA context hint.\n"
        "  --no-vsync         Disable swap interval.\n"
        "  --dump-png=<path>  Headless run: render N frames and write PPM, then exit.\n"
        "  --frames=<n>       Number of frames to advance before dumping (default: 1).\n"
        "  --time=<seconds>   Deterministic elapsed time used by every dump frame.\n"
        "  --dpr=<scale>      Dump at this device-pixel ratio (default: 1).\n"
        "  --benchmark        Headless benchmark: measure frame time + render stats.\n"
        "  --warmup=<n>       Warm-up frames before measurement (default: 30).\n"
        "  --measured=<n>     Measured frames (default: 300).\n"
        "  --list-scenes      Print available scene names and exit.\n"
        "  --help             Print this message.\n",
        whatscanvas::desktop::SceneCatalog::defaultName());
}

bool parseArgs(int argc, char** argv, Args& args)
{
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--help" || a == "-h") { args.showHelp = true; }
        else if (a == "--list-scenes") { args.listScenes = true; }
        else if (a == "--no-msaa") { args.disableMsaa = true; }
        else if (a == "--no-vsync") { args.noVsync = true; }
        else if (startsWith(a, "--scene=")) { args.scene = std::string(a.substr(8)); }
        else if (startsWith(a, "--backend=")) {
            std::string_view name = a.substr(10);
            if (name == "gl" || name == "opengl") { args.backend = BackendChoice::OpenGL; }
            else if (name == "software" || name == "sw") { args.backend = BackendChoice::Software; }
            else {
                std::fprintf(stderr, "Unknown backend: %.*s (expected 'gl' or 'software')\n",
                             static_cast<int>(name.size()), name.data());
                return false;
            }
        }
        else if (startsWith(a, "--viewport-standard=")) {
            const std::string_view id = a.substr(20);
            if (!whatscanvas::scenes::parseViewportStandard(
                    id, args.viewportStandard)) {
                std::fprintf(stderr, "Unknown viewport standard: %.*s\n",
                             static_cast<int>(id.size()), id.data());
                return false;
            }
        }
        else if (startsWith(a, "--w=")) {
            if (!parseInt(a.substr(4), args.width)) return false;
        }
        else if (startsWith(a, "--h=")) {
            if (!parseInt(a.substr(4), args.height)) return false;
        }
        else if (startsWith(a, "--dump-png=")) { args.dumpPath = std::string(a.substr(11)); }
        else if (startsWith(a, "--frames=")) {
            if (!parseInt(a.substr(9), args.dumpFrames)) return false;
        }
        else if (startsWith(a, "--time=")) {
            if (!parseFloat(a.substr(7), args.dumpTimeSeconds)) return false;
        }
        else if (startsWith(a, "--dpr=")) {
            if (!parseFloat(a.substr(6), args.dumpDevicePixelRatio)
                || args.dumpDevicePixelRatio <= 0.0f) return false;
        }
        else if (a == "--benchmark") { args.benchmark = true; }
        else if (startsWith(a, "--warmup=")) {
            if (!parseInt(a.substr(9), args.benchmarkWarmup)) return false;
        }
        else if (startsWith(a, "--measured=")) {
            if (!parseInt(a.substr(11), args.benchmarkMeasured)) return false;
        }
        else {
            std::fprintf(stderr, "Unknown argument: %.*s\n",
                         static_cast<int>(a.size()), a.data());
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    Args args;
    if (!parseArgs(argc, argv, args)) {
        printUsage();
        return 1;
    }
    if (args.showHelp) {
        printUsage();
        return 0;
    }
    if (args.listScenes) {
        for (const auto& name : whatscanvas::desktop::SceneCatalog::listNames()) {
            std::fprintf(stdout, "%s\n", name.c_str());
        }
        return 0;
    }

    auto scene = whatscanvas::desktop::SceneCatalog::create(
        args.scene, args.viewportStandard);
    if (!scene) {
        std::fprintf(stderr, "Unknown scene: %s\n", args.scene.c_str());
        std::fprintf(stderr, "Available scenes:\n");
        for (const auto& name : whatscanvas::desktop::SceneCatalog::listNames()) {
            std::fprintf(stderr, "  %s\n", name.c_str());
        }
        return 2;
    }

    if (!args.dumpPath.empty()) {
        if (args.backend == BackendChoice::Software) {
            whatscanvas::desktop::SoftwareDumpConfig dump;
            dump.outputPath = args.dumpPath;
            dump.width = args.width;
            dump.height = args.height;
            dump.frames = args.dumpFrames;
            dump.captureTimeSeconds = args.dumpTimeSeconds;
            dump.devicePixelRatio = args.dumpDevicePixelRatio;
            return whatscanvas::desktop::SoftwareRuntime::runDump(*scene, dump);
        }
        whatscanvas::desktop::GlfwDumpConfig dump;
        dump.outputPath = args.dumpPath;
        dump.width = args.width;
        dump.height = args.height;
        dump.frames = args.dumpFrames;
        dump.captureTimeSeconds = args.dumpTimeSeconds;
        dump.devicePixelRatio = args.dumpDevicePixelRatio;
        return whatscanvas::desktop::GlfwHost::runDump(*scene, dump);
    }

    if (args.backend == BackendChoice::Software) {
        std::fprintf(stderr,
            "Software backend only supports --dump-png=<path>. Provide it or use --backend=gl.\n");
        return 3;
    }

    if (args.benchmark) {
        whatscanvas::desktop::GlfwBenchmarkConfig bench;
        bench.width = args.width;
        bench.height = args.height;
        bench.warmupFrames = args.benchmarkWarmup;
        bench.measuredFrames = args.benchmarkMeasured;
        return whatscanvas::desktop::GlfwHost::runBenchmark(*scene, bench);
    }

    whatscanvas::desktop::GlfwHostConfig cfg;
    cfg.title = std::string("WhatsCanvas Desktop - ") + args.scene;
    cfg.width = args.width;
    cfg.height = args.height;
    cfg.disableMsaa = args.disableMsaa;
    cfg.vsync = !args.noVsync;
    return whatscanvas::desktop::GlfwHost::runInteractive(*scene, cfg);
}
