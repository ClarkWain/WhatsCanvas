#include <iostream>
#include <string>

#include "wsc/wsc.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }

    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

bool testDefaultStatsAreReadable()
{
    wsc::Canvas canvas;
    const wsc::Canvas::RenderStats stats = canvas.getRenderStats();
    return expect(stats.commandCount == 0, "default command count should be zero")
        && expect(stats.drawCallCount == 0, "default draw call count should be zero")
        && expect(stats.mergedBatchCount == 0, "default merged batch count should be zero")
        && expect(stats.renderTargetSwitches == 0, "default render target switch count should be zero")
        && expect(stats.imageTextureCount == 0, "default image texture count should be zero")
        && expect(stats.glyphAtlasTextureCount == 0, "default glyph atlas count should be zero")
        && expect(stats.renderTargetCount == 0, "default render target count should be zero");
}

} // namespace

int main()
{
    return testDefaultStatsAreReadable() ? 0 : 1;
}
