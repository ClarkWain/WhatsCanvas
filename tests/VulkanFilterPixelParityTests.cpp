#include <iostream>
#include <vector>

#include "render/vulkan/VulkanRenderDevice.h"
#include "support/CompositeFilterParityScene.h"
#include "support/PixelParity.h"

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "FILTER_PARITY backend=vulkan status=FAIL "
                     "reason=backend_unavailable\n";
        return 1;
    }

    std::vector<unsigned char> actual;
    std::vector<unsigned char> reference;
    wsc::Canvas::RenderStats actualStats;
    wsc::Canvas::RenderStats referenceStats;
    const bool actualRendered = whatscanvas::test::renderCompositeFilterParityScene(
        wsc::Canvas::Backend::Vulkan, actual, &actualStats);
    const bool referenceRendered =
        whatscanvas::test::renderCompositeFilterParityScene(
            wsc::Canvas::Backend::Software, reference, &referenceStats);
    if (!actualRendered || !referenceRendered) {
        std::cerr << "FILTER_PARITY backend=vulkan status=FAIL "
                     "reason=render_or_readback\n";
        return 1;
    }

    const bool statsPassed = actualStats.filterCount == 3
        && actualStats.filterPassCount == 7
        && referenceStats.filterCount == 3
        && referenceStats.filterPassCount == 9;
    const auto diff = whatscanvas::test::comparePremultipliedRGBA(
        actual, reference, whatscanvas::test::kCompositeParityWidth,
        whatscanvas::test::kCompositeParityHeight);
    const bool passed = whatscanvas::test::reportPixelParity(
        "vulkan", diff, whatscanvas::test::hashRGBA(actual),
        whatscanvas::test::hashRGBA(reference), 4, 0.75, 0.005,
        statsPassed, statsPassed ? nullptr : "unexpected_filter_stats");
    if (!statsPassed) {
        std::cerr << "[VulkanFilterPixelParityTests] unexpected stats:"
                  << " actual_filters=" << actualStats.filterCount
                  << " actual_passes=" << actualStats.filterPassCount
                  << " reference_filters=" << referenceStats.filterCount
                  << " reference_passes=" << referenceStats.filterPassCount
                  << '\n';
    }
    return passed ? 0 : 1;
}
