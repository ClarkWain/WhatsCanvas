// Metal GPU frame-timing test. The public IRenderDevice contract exposes
// begin/endGpuFrameTiming + lastGpuFrameTimeNs; the Metal backend records
// GPUStartTime / GPUEndTime on each executeDrawList command buffer whenever
// timing is armed. The test drives one frame through the concrete Metal
// device and asserts the reported time is non-zero and small enough to be a
// plausible per-frame delta on the target device.

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "wsc/wsc.h"

#include "render/RenderDeviceFactory.h"
#include "render/RenderTypes.h"
#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/IRenderDevice.h"
#include "render/metal/MetalRenderDevice.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
    }
    return condition;
}

MetalRenderDevice *asMetal(IRenderDevice *device)
{
    return static_cast<MetalRenderDevice *>(device);
}

bool testMetalGpuTiming()
{
    if (!RenderDeviceFactory::isBackendSupported(RenderBackendType::Metal)) {
        std::cout << "Metal unavailable in this build: skipping GPU timing test.\n";
        return true;
    }
    auto device = RenderDeviceFactory::create(RenderBackendType::Metal);
    if (!expect(device != nullptr, "RenderDeviceFactory::create(Metal) should succeed")) {
        return false;
    }
    device->initializeBackend();

    std::uint64_t frameNs = 42;
    expect(!device->lastGpuFrameTimeNs(frameNs) && frameNs == 0,
           "lastGpuFrameTimeNs should return false before any frame is measured");

    device->setGpuFrameTimingEnabled(true);
    if (!expect(device->beginGpuFrameTiming(),
                "beginGpuFrameTiming() should succeed after enabling timing")) {
        device->finalizeBackend();
        return false;
    }

    const int width = 128;
    const int height = 128;
    std::unique_ptr<IRenderTarget> target = device->createRenderTarget(width, height);
    if (!expect(target != nullptr, "createRenderTarget should succeed")) {
        device->finalizeBackend();
        return false;
    }
    OffscreenRenderRequest req;
    req.canvasWidth = width;
    req.canvasHeight = height;
    req.targetWidth = width;
    req.targetHeight = height;
    target->begin(req);

    wsc::DrawList drawList;
    wsc::DrawPrimitive prim;
    prim.kind = wsc::DrawPrimitiveKind::SolidTriangles;
    prim.blendMode = 0;
    prim.positions = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    prim.color[0] = 1.0f;
    prim.color[1] = 0.0f;
    prim.color[2] = 0.0f;
    prim.color[3] = 1.0f;
    drawList.push_back(std::move(prim));

    if (!expect(asMetal(device.get())->executeDrawList(target, drawList),
                "executeDrawList should succeed")) {
        device->finalizeBackend();
        return false;
    }
    device->endGpuFrameTiming();

    frameNs = 0;
    bool ok = expect(device->lastGpuFrameTimeNs(frameNs),
                     "lastGpuFrameTimeNs should return true after a timed frame");
    ok = expect(frameNs > 0, "measured GPU frame time should be non-zero") && ok;
    ok = expect(frameNs < 1'000'000'000ull,
                "measured GPU frame time should stay below 1 second") && ok;

    device->setGpuFrameTimingEnabled(false);
    std::uint64_t after = 42;
    ok = expect(!device->lastGpuFrameTimeNs(after) && after == 0,
                "lastGpuFrameTimeNs should report unavailable after disabling timing") && ok;

    device->finalizeBackend();
    return ok;
}

} // namespace

int main()
{
    return testMetalGpuTiming() ? 0 : 1;
}
