#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

class RenderContext;
class ImageResource;
class GLProgram;

/// A batch renderer for drawing many sprites with the same texture
/// in a single draw call. Instead of issuing one draw call per sprite,
/// SpriteBatch accumulates vertex data and flushes all at once.
///
/// Usage:
///   SpriteBatch batch;
///   batch.setTexture(imageResource);
///   batch.add(x, y, u0, v0, u1, v1, color);
///   batch.add(x2, y2, u0, v0, u1, v1, color);
///   batch.flush(context);  // One draw call for all sprites
///   batch.clear();
class SpriteBatch
{
public:
    SpriteBatch();
    ~SpriteBatch();

    SpriteBatch(const SpriteBatch &) = delete;
    SpriteBatch &operator=(const SpriteBatch &) = delete;

    /// Set the shared texture for all sprites in this batch.
    void setTexture(std::shared_ptr<ImageResource> texture);

    /// Get the current texture.
    const std::shared_ptr<ImageResource> &texture() const { return texture_; }

    /// Add a sprite (quad) to the batch.
    /// Each sprite is 6 vertices (2 triangles), each vertex = 2 pos + 2 uv + 4 color = 8 floats.
    void add(float x, float y, float width, float height,
             float u0, float v0, float u1, float v1,
             float r, float g, float b, float a,
             const glm::mat4 &transform = glm::mat4(1.0f));

    /// Submit all accumulated sprites as a single draw call.
    void flush(RenderContext &context);

    /// Clear the accumulated vertex data without drawing.
    void clear();

    /// Get the number of sprites in the current batch.
    std::size_t spriteCount() const { return vertexData_.size() / 48; } // 6 verts * 8 floats

    /// Whether the batch has any sprites.
    bool empty() const { return vertexData_.empty(); }

private:
    std::shared_ptr<ImageResource> texture_;
    std::vector<float> vertexData_;

    // GL resources (lazily initialized).
    unsigned int VAO_ = static_cast<unsigned int>(-1);
    unsigned int VBO_ = static_cast<unsigned int>(-1);
    GLProgram *program_ = nullptr;
    bool glInitialized_ = false;

    void ensureGLInitialized();
    void releaseGLResources();
};
