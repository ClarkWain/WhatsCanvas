#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "command/DrawCommand.h"
#include "render/FrameCompiler.h"

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

bool testGaussianShadowSemanticPreserved()
{
    DrawShadowData shadow;
    shadow.blurRadius = 8.0f;
    shadow.color[0] = 0.1f;
    shadow.color[1] = 0.2f;
    shadow.color[2] = 0.9f;
    shadow.color[3] = 0.75f;
    shadow.silhouette.points = {
        4.0f, 4.0f, 20.0f, 4.0f, 20.0f, 20.0f,
        4.0f, 4.0f, 20.0f, 20.0f, 4.0f, 20.0f,
    };
    shadow.silhouette.coverage.assign(6, 1.0f);

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<DrawShadowCommand>(shadow));
    CommandDrawListEncodeRequest request;
    request.canvasWidth = 32;
    request.canvasHeight = 32;
    request.targetHeight = 32;
    CompiledFrame frame;
    std::string error;
    FrameCompiler compiler;
    if (!expect(compiler.compile(commands, request, frame, &error),
                "Gaussian shadow command should encode")) {
        std::cerr << error << std::endl;
        return false;
    }
    if (!expect(frame.packets.size() == 1,
                "Gaussian shadow should remain one ordered packet")) {
        return false;
    }
    const wsc::DrawPrimitive &packet = frame.packets.front();
    return expect(packet.kind == wsc::DrawPrimitiveKind::GaussianShadow,
                  "encoder must preserve Gaussian shadow semantics")
        && expect(near(packet.shadowBlurRadius, 8.0f),
                  "encoder must preserve the shadow blur radius")
        && expect(near(packet.tint[2], 0.9f) && near(packet.tint[3], 0.75f),
                  "encoder must preserve the shadow tint")
        && expect(packet.shadowSilhouette.size() == 1,
                  "path shadow should carry one offscreen silhouette")
        && expect(packet.shadowSilhouette.front().kind
                      == wsc::DrawPrimitiveKind::SolidTriangles,
                  "path shadow silhouette should remain solid geometry")
        && expect(packet.shadowSilhouette.front().coverage.size() == 6,
                  "path shadow silhouette should retain analytic AA coverage");
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

    CompiledFrame frame;
    std::string error;
    FrameCompiler compiler;
    if (!expect(
            compiler.compile(commands, request, frame, &error),
            "packed path command should encode")) {
        std::cerr << error << std::endl;
        return 1;
    }
    if (!expect(frame.packets.size() == 1,
                "compiler should emit one primitive")) {
        return 1;
    }

    const wsc::DrawPrimitive &primitive = frame.packets.front();
    return expect(
               frame.stats.commandCount == 1
                   && frame.stats.packetCount == 1,
               "compiler should report command and packet counts")
            && expect(
               frame.stats.vertexBytes > 0
                   && frame.stats.indexBytes == 6 * sizeof(std::uint32_t),
               "compiler should report packet payload bytes")
            && expect(
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
            && testGaussianShadowSemanticPreserved()
        ? 0
        : 1;
}
