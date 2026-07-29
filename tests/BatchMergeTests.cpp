// Regression tests for DrawPath batch-merge compatibility.
//
// Renderer::flush() merges adjacent path draws into one draw call by keeping
// the first command's per-draw state and appending the following commands'
// geometry. That is only valid when the two commands share every piece of
// per-shape state. These tests exercise both the strict uniform predicate and
// the broader per-vertex renderer batch predicate, guarding against a solid
// fill inheriting gradient state or analytic-AA coverage becoming misaligned.

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "command/DrawData.h"
#include "render/PathMerge.h"
#include "render/SpriteBatch.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }
    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

bool near(float actual, float expected)
{
    return std::fabs(actual - expected) < 0.0001f;
}

class FakeImageResource final : public ImageResource
{
public:
    explicit FakeImageResource(std::uint64_t handle)
        : handle_{handle}
    {
    }

    bool isValid() const override { return handle_.isValid(); }
    ImageResourceHandle nativeHandle() const override { return handle_; }
    void bind(const RenderContext &) const override {}
    bool updateRGBA(
        int, int, int, int, const unsigned char *, bool) override
    {
        return true;
    }

private:
    ImageResourceHandle handle_;
};

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
    DrawPathGradientStops &stops =
        data.writableGradientStops();
    stops.positions[0] = 0.0f;
    stops.positions[1] = 1.0f;
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

bool testBroaderBatchFlattensAffineTransforms()
{
    DrawPathData translated = makeSolidFill();
    translated.transform = glm::translate(
        glm::mat4(1.0f), glm::vec3(40.0f, 20.0f, 0.0f));
    if (!expect(
            wsc::render::canBatchPathData(
                makeSolidFill(), translated),
            "renderer batch should flatten differing affine transforms")) {
        return false;
    }
    if (!expect(
            !wsc::render::canMergePathData(
                makeSolidFill(), translated),
            "strict uniform merge should still require the same transform")) {
        return false;
    }

    DrawPathData perspective = makeSolidFill();
    perspective.transform[0][3] = 0.01f;
    return expect(
        !wsc::render::canBatchPathData(
            makeSolidFill(), perspective),
        "renderer batch must reject perspective transforms");
}

bool testSharedIndexedGeometryAccessors()
{
    auto geometry = std::make_shared<DrawPathGeometry>();
    geometry->points = {
        0.0f, 0.0f,
        10.0f, 0.0f,
        10.0f, 10.0f,
        0.0f, 10.0f
    };
    geometry->coverage = {1.0f, 1.0f, 0.0f, 0.0f};
    geometry->indices = {0, 1, 2, 0, 2, 3};
    geometry->topologyFingerprint = 42u;

    DrawPathData data = makeSolidFill();
    data.points.clear();
    data.sharedGeometry = geometry;
    return expect(
               data.getPointCount() == 4,
               "shared path geometry should provide vertex data")
        && expect(
               data.getElementCount() == 6 && data.hasIndices(),
               "shared path geometry should provide triangle indices")
        && expect(
               data.hasCoverage()
                   && data.coverageData()[2] == 0.0f,
               "shared path geometry should provide AA coverage")
        && expect(
               data.sharedGeometry->topologyFingerprint == 42u,
               "shared path geometry should retain topology identity");
}

bool testShortIndexAccessors()
{
    DrawPathData data;
    data.points = {
        0.0f, 0.0f,
        10.0f, 0.0f,
        10.0f, 10.0f,
        0.0f, 10.0f
    };
    data.shortIndices = {0, 1, 2, 0, 2, 3};
    return expect(
               data.hasShortIndices()
                   && data.hasIndices(),
               "16-bit packet should report indexed geometry")
        && expect(
               data.getElementCount() == 6,
               "16-bit packet should report its element count")
        && expect(
               data.getIndex(4) == 2u,
               "generic index access should decode 16-bit indices");
}

bool testPackedAttributeAccessors()
{
    DrawPathData data = makeSolidFill();
    data.packedColors = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255
    };
    data.packedCoverage = {255, 128, 0};
    return expect(
               data.hasPackedVertexColors()
                   && data.hasVertexColors(),
               "RGBA8 packet should report per-vertex colors")
        && expect(
               !data.hasFloatVertexColors(),
               "RGBA8 packet must not report float colors")
        && expect(
               data.hasPackedCoverage()
                   && data.hasCoverage(),
               "8-bit packet should report analytic coverage")
        && expect(
               !data.hasFloatCoverage(),
               "8-bit packet must not report float coverage")
        && expect(
               near(data.vertexColorAt(1, 1), 1.0f)
                   && near(data.vertexColorAt(1, 0), 0.0f),
               "generic color access should decode RGBA8 channels")
        && expect(
               near(data.coverageAt(1), 128.0f / 255.0f),
               "generic coverage access should decode normalized 8-bit values");
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

bool testSpriteBatchTextureSlots()
{
    SpriteBatch batch;
    std::vector<std::shared_ptr<FakeImageResource>> textures;
    for (std::size_t slot = 0;
         slot < SpriteBatch::kMaxTextures; ++slot) {
        auto texture = std::make_shared<FakeImageResource>(
            static_cast<std::uint64_t>(slot + 1u));
        textures.push_back(texture);
        if (!expect(
                batch.addTexture(texture)
                    == static_cast<int>(slot),
                "each distinct texture should receive one ordered slot")) {
            return false;
        }
    }
    if (!expect(
            batch.addTexture(textures[3]) == 3,
            "an existing texture should reuse its original slot")) {
        return false;
    }
    if (!expect(
            batch.addTexture(
                std::make_shared<FakeImageResource>(99u)) == -1,
            "a ninth distinct texture should close the batch")) {
        return false;
    }
    for (std::size_t slot = 0;
         slot < SpriteBatch::kMaxTextures; ++slot) {
        batch.add(
            static_cast<float>(slot), 0.0f, 1.0f, 1.0f,
            0.0f, 0.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            glm::mat4(1.0f), 0.0f,
            static_cast<int>(slot));
    }
    const bool countOk = expect(
        batch.spriteCount() == SpriteBatch::kMaxTextures,
        "expanded multi-texture vertices should retain sprite count");
    batch.clear();
    return expect(
               batch.empty(),
               "clearing a multi-texture batch should release all sprites")
        && countOk;
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
    ok = testBroaderBatchFlattensAffineTransforms() && ok;
    ok = testSharedIndexedGeometryAccessors() && ok;
    ok = testShortIndexAccessors() && ok;
    ok = testPackedAttributeAccessors() && ok;
    ok = testDifferingStateDoesNotMerge() && ok;
    ok = testSpriteBatchTextureSlots() && ok;
    return ok ? 0 : 1;
}
