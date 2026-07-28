#pragma once

#include <vector>

#include <glad/glad.h>

#include "render/RenderTypes.h"

namespace wsc::opengl {

TextureHandle createTextureRGBA(int width, int height, const std::vector<unsigned char> &pixels);
TextureHandle createTextureAlpha8(
    int width, int height,
    const std::vector<unsigned char> &pixels);
TextureHandle createTextureFromImageData(int width, int height, int channels, const unsigned char *pixels,
                                         bool generateMipmaps);
bool updateTextureRGBA(TextureHandle handle, int x, int y, int width, int height, const unsigned char *pixels,
                       bool regenerateMipmaps);
bool updateTextureAlpha8(
    TextureHandle handle, int x, int y, int width, int height,
    const unsigned char *pixels);
bool createRenderTargetTexture(int width, int height, GLuint &framebuffer, GLuint &stencilRenderbuffer,
                               TextureHandle &texture);
void destroyTexture(TextureHandle handle);

} // namespace wsc::opengl
