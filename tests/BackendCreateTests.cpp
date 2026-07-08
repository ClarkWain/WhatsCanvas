// Unit tests for the unified backend-creation API:
//   Canvas::create(Backend, w, h), the preference-list overload,
//   Canvas::isBackendAvailable(Backend), and Canvas::backend().
// These run headless: they only construct canvases (no initializeContext),
// so no GL context or window is required.

#include <iostream>
#include <string>

#include "wsc/wsc.h"

using namespace wsc;

namespace {

using Backend = Canvas::Backend;

int g_failures = 0;

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        ++g_failures;
    }
    return condition;
}

// Software is always available; it is the universal fallback.
bool testSoftwareAlwaysAvailable()
{
    bool ok = expect(Canvas::isBackendAvailable(Backend::Software),
                     "Software backend must always be available");

    auto canvas = Canvas::create(Backend::Software, 32, 24);
    ok = expect(canvas != nullptr, "create(Software) should return a canvas") && ok;
    if (!canvas) {
        return false;
    }
    ok = expect(canvas->backend() == Backend::Software,
                "backend() should report Software") && ok;
    ok = expect(canvas->getWidth() == 32 && canvas->getHeight() == 24,
                "created canvas should keep its requested size") && ok;
    return ok;
}

// Backends with no implementation must be unavailable and yield nullptr.
bool testUnavailableBackends()
{
    bool ok = expect(!Canvas::isBackendAvailable(Backend::Metal),
                     "Metal backend should be unavailable");
    ok = expect(!Canvas::isBackendAvailable(Backend::Direct3D),
                "Direct3D backend should be unavailable") && ok;
    // Auto is a selector, not a concrete backend: it is never "available".
    ok = expect(!Canvas::isBackendAvailable(Backend::Auto),
                "Auto should not report as an available backend") && ok;

    ok = expect(Canvas::create(Backend::Metal, 16, 16) == nullptr,
                "create(Metal) should return nullptr") && ok;
    ok = expect(Canvas::create(Backend::Direct3D, 16, 16) == nullptr,
                "create(Direct3D) should return nullptr") && ok;
    return ok;
}

// isBackendAvailable() must be consistent with what create() returns.
bool testAvailabilityMatchesCreate()
{
    bool ok = true;
    const Backend concrete[] = {Backend::OpenGL, Backend::OpenGLES,
                                Backend::Software, Backend::Vulkan,
                                Backend::Metal, Backend::Direct3D};
    for (Backend b : concrete) {
        auto canvas = Canvas::create(b, 8, 8);
        const bool available = Canvas::isBackendAvailable(b);
        ok = expect((canvas != nullptr) == available,
                    "create() success must match isBackendAvailable()") && ok;
        if (canvas) {
            ok = expect(canvas->backend() == b,
                        "backend() should match the requested backend") && ok;
        }
    }
    return ok;
}

// Auto resolves to a concrete, available backend (Software at minimum).
bool testAutoResolves()
{
    auto canvas = Canvas::create(Backend::Auto, 40, 20);
    bool ok = expect(canvas != nullptr,
                     "create(Auto) should resolve to some available backend");
    if (!canvas) {
        return false;
    }
    ok = expect(canvas->backend() != Backend::Auto,
                "resolved backend should be concrete, not Auto") && ok;
    ok = expect(Canvas::isBackendAvailable(canvas->backend()),
                "resolved backend should report as available") && ok;
    ok = expect(canvas->getWidth() == 40 && canvas->getHeight() == 20,
                "Auto-created canvas should keep its size") && ok;
    return ok;
}

// The preference-list overload picks the first available backend.
bool testPreferenceList()
{
    // Metal is unavailable, Software is available -> Software wins.
    auto canvas = Canvas::create({Backend::Metal, Backend::Software}, 12, 12);
    bool ok = expect(canvas != nullptr,
                     "preference list should skip unavailable and pick Software");
    if (canvas) {
        ok = expect(canvas->backend() == Backend::Software,
                    "preference list should resolve to Software") && ok;
    }

    // None available -> nullptr.
    auto none = Canvas::create({Backend::Metal, Backend::Direct3D}, 12, 12);
    ok = expect(none == nullptr,
                "preference list with no available backend should return nullptr") && ok;
    return ok;
}

// If OpenGL is available in this build, create() must honor it.
bool testOpenGLWhenAvailable()
{
    if (!Canvas::isBackendAvailable(Backend::OpenGL)) {
        return true; // Not applicable to this build configuration.
    }
    auto canvas = Canvas::create(Backend::OpenGL, 64, 48);
    bool ok = expect(canvas != nullptr, "create(OpenGL) should return a canvas");
    if (canvas) {
        ok = expect(canvas->backend() == Backend::OpenGL,
                    "backend() should report OpenGL") && ok;
        ok = expect(canvas->getWidth() == 64 && canvas->getHeight() == 48,
                    "OpenGL canvas should keep its size") && ok;
    }
    return ok;
}

} // namespace

int main()
{
    testSoftwareAlwaysAvailable();
    testUnavailableBackends();
    testAvailabilityMatchesCreate();
    testAutoResolves();
    testPreferenceList();
    testOpenGLWhenAvailable();

    if (g_failures == 0) {
        std::cout << "BackendCreateTests: all checks passed." << std::endl;
        return 0;
    }
    std::cerr << "BackendCreateTests: " << g_failures << " check(s) failed." << std::endl;
    return 1;
}
