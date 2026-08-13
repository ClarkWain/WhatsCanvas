// Metal device lifecycle test. Boots and shuts down the render device via
// the factory, exercising the initialization / finalization contract
// (double-init idempotency, resource stats before + after render, no crash
// on repeated finalize) without going through the higher-level Canvas API.

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include "wsc/wsc.h"

#include "render/RenderDeviceFactory.h"
#include "render/IRenderDevice.h"
#include "render/IRenderTarget.h"
#include "render/RenderTypes.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
    }
    return condition;
}

bool testMetalDeviceLifecycle()
{
    if (!RenderDeviceFactory::isBackendSupported(RenderBackendType::Metal)) {
        std::cout << "Metal unavailable in this build: skipping device lifecycle test.\n";
        return true;
    }
    auto device = RenderDeviceFactory::create(RenderBackendType::Metal);
    if (!expect(device != nullptr, "factory should return a Metal device")) {
        return false;
    }

    // Baseline resource stats before initializeBackend should be all zero.
    RenderResourceStats before = device->resourceStats();
    bool ok = expect(before.imageTextureCount == 0 && before.renderTargetCount == 0,
                     "resource stats should start at zero");

    device->initializeBackend();
    device->initializeBackend(); // idempotent

    auto target = device->createRenderTarget(32, 32);
    ok = expect(target != nullptr, "createRenderTarget(32, 32) should succeed") && ok;
    if (target != nullptr) {
        ok = expect(target->isValid(), "render target should report valid") && ok;
        ok = expect(target->width() == 32 && target->height() == 32,
                    "render target should keep the requested size") && ok;
    }
    RenderResourceStats after = device->resourceStats();
    ok = expect(after.imageTextureCount > before.imageTextureCount
                && after.renderTargetCount > before.renderTargetCount,
                "creating a render target should bump resource stats") && ok;

    device->finalizeBackend();
    device->finalizeBackend(); // idempotent
    return ok;
}

} // namespace

int main()
{
    return testMetalDeviceLifecycle() ? 0 : 1;
}
