#include "PixelFormatCaps.h"

#include <cstring>

namespace {

void clearGLErrors()
{
    while (glGetError() != GL_NO_ERROR) {
    }
}

void textureProbeFormat(PixelFormatCaps::Format format, GLenum &externalFormat, GLenum &type)
{
    switch (format) {
    case PixelFormatCaps::RGBA16F:
    case PixelFormatCaps::RGBA32F:
        externalFormat = GL_RGBA;
        type = GL_FLOAT;
        break;
    case PixelFormatCaps::R32F:
        externalFormat = GL_RED;
        type = GL_FLOAT;
        break;
    case PixelFormatCaps::DEPTH24_STENCIL8:
        externalFormat = GL_DEPTH_STENCIL;
        type = GL_UNSIGNED_INT_24_8;
        break;
    case PixelFormatCaps::DEPTH32F:
        externalFormat = GL_DEPTH_COMPONENT;
        type = GL_FLOAT;
        break;
    case PixelFormatCaps::RGBA8:
    case PixelFormatCaps::SRGB8_ALPHA8:
    default:
        externalFormat = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
        break;
    }
}

} // namespace

bool PixelFormatCaps::initialized_ = false;
bool PixelFormatCaps::caps_[7][4] = {};

GLint PixelFormatCaps::toGLInternalFormat(Format format)
{
    switch (format) {
    case RGBA8:             return GL_RGBA8;
    case SRGB8_ALPHA8:      return GL_SRGB8_ALPHA8;
    case RGBA16F:           return GL_RGBA16F;
    case RGBA32F:           return GL_RGBA32F;
    case R32F:              return GL_R32F;
    case DEPTH24_STENCIL8:  return GL_DEPTH24_STENCIL8;
    case DEPTH32F:          return GL_DEPTH_COMPONENT32F;
    }
    return GL_RGBA8;
}

void PixelFormatCaps::initialize()
{
    if (initialized_) {
        return;
    }

    // Use glTexImage2D probe with width=0 to test format support.
    // This is compatible with GL 3.3 without glGetInternalformativ.
    const Format formats[] = {RGBA8, SRGB8_ALPHA8, RGBA16F, RGBA32F, R32F, DEPTH24_STENCIL8, DEPTH32F};

    for (int f = 0; f < 7; ++f) {
        const GLint internalFormat = toGLInternalFormat(formats[f]);
        GLenum error;
        GLenum externalFormat = GL_RGBA;
        GLenum type = GL_UNSIGNED_BYTE;
        textureProbeFormat(formats[f], externalFormat, type);

        // Test if the format can be used as a texture (sample).
        GLuint testTex = 0;
        glGenTextures(1, &testTex);
        glBindTexture(GL_TEXTURE_2D, testTex);
        clearGLErrors();
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, 1, 1, 0, externalFormat, type, nullptr);
        error = glGetError();
        caps_[f][0] = (error == GL_NO_ERROR);  // SAMPLE

        if (caps_[f][0]) {
            // Test linear filtering.
            clearGLErrors();
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            error = glGetError();
            caps_[f][2] = (error == GL_NO_ERROR);  // LINEAR
        }

        glDeleteTextures(1, &testTex);

        // Test if the format can be used as a renderbuffer (rendertarget).
        GLuint testRbo = 0;
        glGenRenderbuffers(1, &testRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, testRbo);
        clearGLErrors();
        glRenderbufferStorage(GL_RENDERBUFFER, internalFormat, 1, 1);
        error = glGetError();
        caps_[f][1] = (error == GL_NO_ERROR);  // RENDERTARGET
        glDeleteRenderbuffers(1, &testRbo);

        // MSAA support — assume available if rendertarget works.
        caps_[f][3] = caps_[f][1];
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    initialized_ = true;
}

bool PixelFormatCaps::isSupported(Format format, int usageFlags)
{
    if (!initialized_) {
        initialize();
    }

    const int fi = static_cast<int>(format);
    if (fi < 0 || fi >= 7) {
        return false;
    }

    if (usageFlags & SAMPLE && !caps_[fi][0]) return false;
    if (usageFlags & RENDERTARGET && !caps_[fi][1]) return false;
    if (usageFlags & LINEAR && !caps_[fi][2]) return false;
    if (usageFlags & MSAA && !caps_[fi][3]) return false;

    return true;
}
