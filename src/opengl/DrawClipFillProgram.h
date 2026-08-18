#pragma once

#include <vector>

#include "opengl/StreamBuffer.h"
#include "render/RenderTypes.h"

class GLProgram;
class RenderContext;

namespace wsc::opengl {

/// Data plumbed into DrawClipFillProgram::draw().
///
/// Positions are in NDC (already transformed by the encoder). UVs are
/// screen-space in [0,1] mapping into the mask texture's red channel.
/// If `positions` is empty (or size < 6), the program falls back to a
/// full-target -1..1 quad with 0..1 UVs and a single uniform color -
/// matches VulkanRenderDevice's ClipFill fullscreen-quad convention used
/// by the shared encoder for canvas-covering clipped paths.
struct DrawClipFillData
{
    const std::vector<float> *positions = nullptr; ///< NDC xy pairs (nullptr => fullscreen quad)
    const std::vector<float> *uvs = nullptr;       ///< 0..1 xy pairs (nullptr with explicit positions => derive from NDC)
    const std::vector<float> *perVertexColors = nullptr; ///< optional rgba per vertex (nullptr => use `color`)
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};     ///< fallback tint applied when perVertexColors is absent
    SharedImageResource mask;                       ///< R8 (or RGBA with .r == coverage) mask texture
};

/// Draws a clip-fill primitive: a colored fragment modulated by a mask
/// texture's red channel. Mirrors VulkanRenderDevice's clip pipeline
/// so shared-encoder DrawList playback produces the same result on OpenGL.
class DrawClipFillProgram
{
public:
    static DrawClipFillProgram *getInstance()
    {
        if (instance_ == nullptr) {
            instance_ = new DrawClipFillProgram();
        }
        return instance_;
    }

    DrawClipFillProgram(const DrawClipFillProgram &) = delete;
    DrawClipFillProgram &operator=(const DrawClipFillProgram &) = delete;

    ~DrawClipFillProgram();

    void initialize();
    void release(bool abandon = false);
    void draw(const RenderContext &context, const DrawClipFillData &data);

private:
    DrawClipFillProgram() = default;

    static DrawClipFillProgram *instance_;

    GLProgram *program_ = nullptr;
    unsigned int VAO_ = static_cast<unsigned int>(-1);
    StreamBuffer vertexBuffer_;
    bool initialized_ = false;
};

} // namespace wsc::opengl
