// Regression tests for DrawPath batch-merge compatibility.
//
// Renderer::flush() merges adjacent path draws into one draw call by keeping
// the first command's per-draw state and appending the following commands'
// geometry. That is only valid when the two commands share every piece of
// per-shape state. These tests exercise both the strict uniform predicate and
// the broader per-vertex renderer batch predicate, guarding against a solid
// fill inheriting gradient state or analytic-AA coverage becoming misaligned.

#include <iostream>
#include <string>
#include <vector>

#include "command/DrawData.h"
#include "render/PathMerge.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }
    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

// A minimal solid triangle fill with all merge-relevant fields set to fixed,
// matching values so two of them are batch-compatible by default.
DrawPathData makeSolidFill()
{
    DrawPathData data;
    data.points = {0.0f, 0.0f, 10.0f, 0.0f, 10.0f, 10.0f};
    data.color[0] = 0.2f;
    data.color[1] = 0.4f;
    data.color[2] = 0.6f;
    data.color[3] = 1.0f;
    data.drawMode = PathDrawMode::Fill;
    data.capStyle = PathCapStyle::Bevel;
    data.width = 1.0f;
    return data;
}

DrawPathData makeGradientFill()
{
    DrawPathData data = makeSolidFill();
    data.gradientType = DrawGradientType::Linear;
    data.gradientStopCount = 2;
    data.gradientStopPositions[0] = 0.0f;
    data.gradientStopPositions[1] = 1.0f;
    return data;
}

DrawPathData makeAntiAliasedFill()
{
    DrawPathData data = makeSolidFill();
    data.coverage = {1.0f, 1.0f, 1.0f}; // one value per vertex -> hasCoverage() == true
    return data;
}

bool testTwoSolidFillsMerge()
{
    return expect(wsc::render::canMergePathData(makeSolidFill(), makeSolidFill()),
                  "two identical solid fills should be merge-compatible");
}

bool testSolidAndGradientDoNotMerge()
{
    return expect(!wsc::render::canMergePathData(makeSolidFill(), makeGradientFill()),
                  "a solid fill must not merge with a gradient fill")
        && expect(!wsc::render::canMergePathData(makeGradientFill(), makeSolidFill()),
                  "a gradient fill must not merge with a solid fill (order independent)");
}

bool testTwoGradientsDoNotMerge()
{
    return expect(!wsc::render::canMergePathData(makeGradientFill(), makeGradientFill()),
                  "gradient fills carry per-shape coordinates and must never merge");
}

bool testAntiAliasedFillsMerge()
{
    return expect(wsc::render::canMergePathData(makeAntiAliasedFill(), makeAntiAliasedFill()),
                  "two coverage-bearing fills should be merge-compatible");
}

bool testCoverageMismatchDoesNotMerge()
{
    return expect(!wsc::render::canMergePathData(makeAntiAliasedFill(), makeSolidFill()),
                  "fills differing in coverage presence must not merge")
        && expect(!wsc::render::canMergePathData(makeSolidFill(), makeAntiAliasedFill()),
                  "coverage presence mismatch must not merge (order independent)");
}

bool testBroaderBatchSupportsPerVertexAttributes()
{
    DrawPathData differentColor = makeSolidFill();
    differentColor.color[0] = 0.9f;
    if (!expect(
            wsc::render::canBatchPathData(
                makeSolidFill(), differentColor),
            "renderer batch should encode differing colors per vertex")) {
        return false;
    }
    if (!expect(
            wsc::render::canBatchPathData(
                makeSolidFill(), makeAntiAliasedFill()),
            "renderer batch should fill missing coverage with one")) {
        return false;
    }
    return expect(
        !wsc::render::canBatchPathData(
            makeSolidFill(), makeGradientFill()),
        "renderer batch must still reject gradients");
}

bool testDifferingStateDoesNotMerge()
{
    DrawPathData other = makeSolidFill();
    other.color[0] = 0.9f;
    if (!expect(!wsc::render::canMergePathData(makeSolidFill(), other), "different colours must not merge")) {
        return false;
    }

    DrawPathData stroke = makeSolidFill();
    stroke.drawMode = PathDrawMode::Stroke;
    if (!expect(!wsc::render::canMergePathData(makeSolidFill(), stroke), "different draw modes must not merge")) {
        return false;
    }

    DrawPathData scissored = makeSolidFill();
    scissored.scissor.enabled = true;
    scissored.scissor.width = 5;
    return expect(!wsc::render::canMergePathData(makeSolidFill(), scissored),
                  "different scissor state must not merge");
}

} // namespace

int main()
{
    bool ok = true;
    ok = testTwoSolidFillsMerge() && ok;
    ok = testSolidAndGradientDoNotMerge() && ok;
    ok = testTwoGradientsDoNotMerge() && ok;
    ok = testAntiAliasedFillsMerge() && ok;
    ok = testCoverageMismatchDoesNotMerge() && ok;
    ok = testBroaderBatchSupportsPerVertexAttributes() && ok;
    ok = testDifferingStateDoesNotMerge() && ok;
    return ok ? 0 : 1;
}
