#pragma once

#include <cstddef>
#include <vector>

#include <glad/glad.h>

#include "render/IVolatile.h"

/// A GPU-backed buffer that can be sampled as a texture via samplerBuffer
/// in shaders. Useful for storing glyph atlas indices, lookup tables,
/// or any data that needs random access from a shader.
///
/// Note: Requires GL 3.0+ (texture buffer objects).
class TexelBuffer : public IVolatile
{
public:
    TexelBuffer() = default;
    ~TexelBuffer();

    TexelBuffer(const TexelBuffer &) = delete;
    TexelBuffer &operator=(const TexelBuffer &) = delete;

    /// Initialize with float data.
    bool initialize(const float *data, std::size_t count);

    /// Update the buffer data.
    bool update(const float *data, std::size_t count);

    /// Release GPU resources.
    void release();

    bool loadVolatile() override;
    void unloadVolatile() override;

    /// Get the texture unit handle for binding in shaders.
    GLuint textureHandle() const { return texture_; }

    /// Get the number of elements in the buffer.
    std::size_t count() const { return count_; }

    /// Whether the buffer is initialized and valid.
    bool isValid() const { return buffer_ != 0 && texture_ != 0; }

private:
    GLuint buffer_ = 0;
    GLuint texture_ = 0;
    std::size_t count_ = 0;
    std::size_t capacity_ = 0;
    std::vector<float> cachedData_;

    bool uploadGpuData(const float *data, std::size_t count);
    void releaseGpuResources();
};
