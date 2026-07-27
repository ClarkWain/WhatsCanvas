#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "command/DrawCommand.h"
#include "render/CommandDrawListEncoder.h"

namespace {

bool near(float actual, float expected)
{
    return std::fabs(actual - expected) < 0.0001f;
}

bool expect(bool condition, const char *message)
{
    if (condition) {
        return true;
    }
    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

} // namespace

int main()
{
    DrawPathData data;
    data.points = {
        0.0f, 0.0f,
        16.0f, 0.0f,
        16.0f, 16.0f,
        0.0f, 16.0f
    };
    data.indices = {0, 1, 2, 0, 2, 3};
    data.packedColors = {
        255, 0, 0, 255,
        0, 128, 0, 255,
        0, 0, 255, 64,
        255, 255, 255, 255
    };
    data.packedCoverage = {255, 128, 0, 255};
    data.color[0] = 1.0f;
    data.color[1] = 1.0f;
    data.color[2] = 1.0f;
    data.color[3] = 1.0f;
    data.drawMode = PathDrawMode::Fill;
    data.capStyle = PathCapStyle::Bevel;

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<DrawPathCommand>(std::move(data)));

    CommandDrawListEncodeRequest request;
    request.canvasWidth = 16;
    request.canvasHeight = 16;
    request.targetHeight = 16;

    wsc::DrawList drawList;
    std::string error;
    if (!expect(
            encodeCommandsToDrawList(commands, request, drawList, &error),
            "packed path command should encode")) {
        std::cerr << error << std::endl;
        return 1;
    }
    if (!expect(drawList.size() == 1, "encoder should emit one primitive")) {
        return 1;
    }

    const wsc::DrawPrimitive &primitive = drawList.front();
    return expect(
               primitive.positions.size() == 8,
               "encoder should retain unique indexed vertices")
            && expect(
               primitive.indices
                   == std::vector<std::uint32_t>(
                       {0, 1, 2, 0, 2, 3}),
               "encoder should preserve triangle indices")
            && expect(
               primitive.colors.size() == 16,
               "encoder should emit decoded RGBA values")
            && expect(
               near(primitive.colors[5], 128.0f / 255.0f),
               "encoder should normalize packed color channels")
            && expect(
               near(primitive.colors[11], 64.0f / 255.0f),
               "encoder should preserve packed alpha")
            && expect(
               primitive.coverage.size() == 4,
               "encoder should emit decoded coverage values")
            && expect(
               near(primitive.coverage[1], 128.0f / 255.0f),
               "encoder should normalize packed coverage")
            && expect(
               near(primitive.coverage[2], 0.0f),
               "encoder should preserve zero coverage")
        ? 0
        : 1;
}
