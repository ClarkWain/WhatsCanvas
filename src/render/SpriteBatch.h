#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

class RenderContext;
class ImageResource;
class GLProgram;
enum class DrawBlendMode;

/// A batch renderer for drawing many sprites with a small ordered texture set
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
    static constexpr std::size_t kMaxTextures = 8u;

    SpriteBatch();
    ~SpriteBatch();

    SpriteBatch(const SpriteBatch &) = delete;
    SpriteBatch &operator=(const SpriteBatch &) = delete;

    /// Reset transient binding state for a new frame.
    void beginFrame();

    /// End a consecutive sprite sequence and restore sampler/VAO bindings.
    void endBatch();

    /// Set the shared texture for all sprites in this batch.
    void setTexture(std::shared_ptr<ImageResource> texture);

    /// Get the current texture.
    const std::shared_ptr<ImageResource> &texture() const { return texture_; }

    /// Return the texture slot for an expanded sprite, or -1 when this batch
    /// has no remaining slot or the resource has no OpenGL-native handle.
    int addTexture(const std::shared_ptr<ImageResource> &texture);

    /// Add a sprite (quad) to the batch.
    /// Each sprite is 4 indexed vertices. Per-vertex data also carries
    /// normalized quad coordinates and uniform rounded-clip parameters.
    void add(float x, float y, float width, float height,
             float u0, float v0, float u1, float v1,
             float r, float g, float b, float a,
             const glm::mat4 &transform = glm::mat4(1.0f),
             float roundedRadius = 0.0f,
             int textureSlot = 0);

    /// Add an atlas quad using one compact instance instead of four expanded
    /// vertices. Intended for alpha-only glyph textures.
    void addInstance(float x, float y, float width, float height,
                     float u0, float v0, float u1, float v1,
                     float r, float g, float b, float a,
                     const glm::mat4 &transform = glm::mat4(1.0f));

    /// Submit all accumulated sprites as a single draw call.
    void flush(RenderContext &context, DrawBlendMode blendMode);

    /// Clear the accumulated vertex data without drawing.
    void clear();

    /// Forget all GL names after involuntary context loss without deleting
    /// objects from the lost context.
    void abandonGLResources();

    /// Get the number of sprites in the current batch.
    std::size_t spriteCount() const
    {
        return !instanceData_.empty()
            ? instanceData_.size() / 12u
            : vertexData_.size() / 56u;
    }

    /// Whether the batch has any sprites.
    bool empty() const
    {
        return vertexData_.empty() && instanceData_.empty();
    }

private:
    std::shared_ptr<ImageResource> texture_;
    std::vector<std::shared_ptr<ImageResource>> textures_;
    std::vector<float> vertexData_;
    std::vector<float> instanceData_;

    // GL resources (lazily initialized).
    unsigned int VAO_ = static_cast<unsigned int>(-1);
    unsigned int VBO_ = static_cast<unsigned int>(-1);
    unsigned int EBO_ = static_cast<unsigned int>(-1);
    unsigned int sampler_ = static_cast<unsigned int>(-1);
    unsigned int instanceVAO_ = static_cast<unsigned int>(-1);
    unsigned int instanceVBO_ = static_cast<unsigned int>(-1);
    std::size_t indexSpriteCapacity_ = 0;
    GLProgram *program_ = nullptr;
    GLProgram *instanceProgram_ = nullptr;
    bool glInitialized_ = false;
    GLProgram *boundProgram_ = nullptr;
    std::array<unsigned int, kMaxTextures> boundTextures_ = {};
    std::size_t boundSamplerCount_ = 0;
    bool samplerUniformsInitialized_ = false;
    bool instanceSamplerInitialized_ = false;

    void ensureGLInitialized();
    void ensureIndexCapacity(std::size_t spriteCount);
    void releaseGLResources(bool abandon = false);
};
