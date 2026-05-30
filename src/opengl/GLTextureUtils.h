#pragma once

#include <vector>

#include <glad/glad.h>

#include "render/RenderTypes.h"

namespace prismcanvas::opengl {

TextureHandle createTextureRGBA(int width, int height, const std::vector<unsigned char> &pixels);
TextureHandle createTextureFromImageData(int width, int height, int channels, const unsigned char *pixels,
                                         bool generateMipmaps);
bool createRenderTargetTexture(int width, int height, GLuint &framebuffer, GLuint &stencilRenderbuffer,
                               TextureHandle &texture);
void destroyTexture(TextureHandle handle);

} // namespace prismcanvas::opengl