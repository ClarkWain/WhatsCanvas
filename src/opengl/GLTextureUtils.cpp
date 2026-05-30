#include "opengl/GLTextureUtils.h"

#include <cstddef>
#include <cstdint>

namespace {

TextureHandle createTexture(int width, int height, GLenum internalFormat, GLenum dataFormat,
                            const void *pixels, bool generateMipmaps)
{
    if (width <= 0 || height <= 0) {
        return {};
    }

    GLint previousTexture = 0;
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, dataFormat, GL_UNSIGNED_BYTE, pixels);
    if (generateMipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    return TextureHandle{static_cast<std::uint32_t>(texture)};
}

} // namespace

namespace prismcanvas::opengl {

TextureHandle createTextureRGBA(int width, int height, const std::vector<unsigned char> &pixels)
{
    if (pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
        return {};
    }

    return createTexture(width, height, GL_RGBA, GL_RGBA, pixels.data(), false);
}

TextureHandle createTextureFromImageData(int width, int height, int channels, const unsigned char *pixels,
                                         bool generateMipmaps)
{
    if (pixels == nullptr || (channels != 3 && channels != 4)) {
        return {};
    }

    const GLenum format = channels == 4 ? GL_RGBA : GL_RGB;
    return createTexture(width, height, format, format, pixels, generateMipmaps);
}

bool createRenderTargetTexture(int width, int height, GLuint &framebuffer, GLuint &stencilRenderbuffer,
                               TextureHandle &texture)
{
    framebuffer = 0;
    stencilRenderbuffer = 0;
    texture = {};
    if (width <= 0 || height <= 0) {
        return false;
    }

    const TextureHandle createdTexture = createTexture(width, height, GL_RGBA, GL_RGBA, nullptr, false);
    if (!createdTexture.isValid()) {
        return false;
    }

    const GLuint nativeTexture = static_cast<GLuint>(createdTexture.value);
    GLint previousRenderbuffer = 0;
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, nativeTexture, 0);

    glGenRenderbuffers(1, &stencilRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, stencilRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencilRenderbuffer);

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previousRenderbuffer));
    if (!complete) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteRenderbuffers(1, &stencilRenderbuffer);
        glDeleteFramebuffers(1, &framebuffer);
        destroyTexture(createdTexture);
        framebuffer = 0;
        stencilRenderbuffer = 0;
        return false;
    }

    texture = createdTexture;
    return true;
}

void destroyTexture(TextureHandle handle)
{
    if (!handle.isValid()) {
        return;
    }

    const GLuint nativeTexture = static_cast<GLuint>(handle.value);
    glDeleteTextures(1, &nativeTexture);
}

} // namespace prismcanvas::opengl