#pragma once

#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>

class GLProgram;

namespace wsc::opengl {

/// Builds an anti-aliased clip coverage mask on the GPU.
///
/// Each clip path is rasterised (with its analytic-AA fringe) into a single
/// R8 layer using GL_MAX blending so overlapping interior/fringe triangles keep
/// the highest coverage. Successive clip layers are multiplied into an
/// accumulator, so the final mask holds the smooth intersection coverage of all
/// active clips (0 outside, 1 fully inside, a soft ramp along every edge).
///
/// Draw programs sample the accumulator texture and multiply their output alpha
/// by the coverage, giving anti-aliased arbitrary-path clipping instead of the
/// old 1-bit stencil test. The targets live for the process lifetime (singleton)
/// and resize on demand.
class ClipCoverageProgram
{
public:
    static ClipCoverageProgram *getInstance()
    {
        if (instance_ == nullptr) {
            instance_ = new ClipCoverageProgram();
        }
        return instance_;
    }

    ClipCoverageProgram(const ClipCoverageProgram &) = delete;
    ClipCoverageProgram &operator=(const ClipCoverageProgram &) = delete;

    ~ClipCoverageProgram();

    void initialize();
    void release();

    /// Ensures the accumulator and temp R8 targets exist at the requested size.
    bool ensureTargets(int width, int height);

    /// Binds the accumulator and clears it to full coverage (white) so the first
    /// multiplied layer becomes the accumulator's value.
    void beginAccumulator(int width, int height);

    /// Binds the temp layer, clears it to zero coverage and configures GL_MAX
    /// blending so coverage triangles accumulate their maximum value.
    void beginClipLayer(int width, int height);

    /// Rasterises one clip path's coverage triangles into the currently bound
    /// (temp) layer. `points` is interleaved x,y in path-local space, `coverage`
    /// is one value per vertex, `transform` maps local to device space.
    void drawCoverage(const std::vector<float> &points, const std::vector<float> &coverage,
                      const glm::mat4 &transform, int width, int height);

    /// Multiplies the temp layer into the accumulator (accumulator *= temp).
    void multiplyLayerIntoAccumulator(int width, int height);

    GLuint accumulatorTexture() const { return accumulatorTexture_; }

private:
    ClipCoverageProgram() = default;

    void destroyTargets();
    void ensureCoverageBuffer(std::size_t floatCount);

    static ClipCoverageProgram *instance_;

    GLProgram *coverageProgram_ = nullptr;
    GLProgram *multiplyProgram_ = nullptr;

    GLuint coverageVao_ = 0;
    GLuint coverageVbo_ = 0;
    std::size_t coverageVboCapacity_ = 0;

    GLuint quadVao_ = 0;
    GLuint quadVbo_ = 0;

    GLuint accumulatorFbo_ = 0;
    GLuint accumulatorTexture_ = 0;
    GLuint tempFbo_ = 0;
    GLuint tempTexture_ = 0;
    int targetWidth_ = 0;
    int targetHeight_ = 0;

    bool initialized_ = false;
};

} // namespace wsc::opengl
