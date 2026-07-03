#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "render/GaussianKernel.h"

class GLProgram;

namespace wsc::opengl {

/// Self-contained separable Gaussian blur on the GPU.
///
/// Owns a full-screen-quad shader and two ping-pong RGBA render targets. The
/// blur passes operate purely on the alpha channel and always write rgb = 1, so
/// straight-alpha color bleeding cannot occur; a separate composite pass tints
/// the blurred coverage and draws it into the currently bound framebuffer.
///
/// All targets and GL objects live for the process lifetime (singleton) and are
/// resized on demand, so a blur runs entirely within a single call chain and
/// never leaves a transient texture to be reused/corrupted by the deferred
/// render-target pool.
class GaussianBlurProgram
{
public:
    static GaussianBlurProgram *getInstance()
    {
        if (instance_ == nullptr) {
            instance_ = new GaussianBlurProgram();
        }
        return instance_;
    }

    GaussianBlurProgram(const GaussianBlurProgram &) = delete;
    GaussianBlurProgram &operator=(const GaussianBlurProgram &) = delete;

    ~GaussianBlurProgram();

    void initialize();
    void release();
    bool isInitialized() const { return initialized_; }

    /// Ensures the two ping-pong targets exist at the requested size.
    /// Returns false if the targets could not be created.
    bool ensureTargets(int width, int height);

    GLuint framebuffer(int index) const { return index == 0 ? fboA_ : fboB_; }
    GLuint texture(int index) const { return index == 0 ? textureA_ : textureB_; }

    /// One separable blur pass: samples `srcTexture` along `direction`
    /// (a texel step such as (1/width, 0)) and writes the weighted alpha into
    /// `dstFramebuffer`. Binds the destination framebuffer and viewport.
    void blurPass(GLuint srcTexture, GLuint dstFramebuffer, int width, int height,
                  const glm::vec2 &direction, const wsc::render::GaussianKernel &kernel);

    /// Composites `srcTexture` (blurred coverage) into the currently bound
    /// framebuffer as a full-screen quad, tinting rgb with `tint` and scaling
    /// alpha by `tint.a`. The caller owns blend/scissor/viewport state.
    void composite(GLuint srcTexture, const glm::vec4 &tint);

private:
    GaussianBlurProgram() = default;

    static constexpr int kMaxRadius = 64;

    void drawQuad();
    void destroyTargets();

    static GaussianBlurProgram *instance_;

    GLProgram *program_ = nullptr;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint fboA_ = 0;
    GLuint fboB_ = 0;
    GLuint textureA_ = 0;
    GLuint textureB_ = 0;
    int targetWidth_ = 0;
    int targetHeight_ = 0;
    bool initialized_ = false;
};

} // namespace wsc::opengl
