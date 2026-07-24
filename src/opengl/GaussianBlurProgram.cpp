#include "GaussianBlurProgram.h"

#include <algorithm>
#include <string>
#include <vector>

#include "GLShaderSource.h"
#include "GLProgram.h"

namespace wsc::opengl {

GaussianBlurProgram *GaussianBlurProgram::instance_ = nullptr;

GaussianBlurProgram::~GaussianBlurProgram()
{
    release();
}

void GaussianBlurProgram::initialize()
{
    if (initialized_) {
        return;
    }

    const std::string vertexSrc = std::string(shaderVersionDirective()) + R"(
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aUv;
        out vec2 vUv;
        void main()
        {
            vUv = aUv;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    const std::string fragmentSrc = std::string(shaderVersionDirective()) + R"(
        in vec2 vUv;
        uniform sampler2D uTexture;
        uniform int uMode;        // 0 = alpha blur, 1 = composite, 2 = RGBA blur
        uniform vec2 uDirection;  // texel step along the blur axis
        uniform int uRadius;
        uniform int uDecal;
        uniform float uWeights[65];
        uniform vec4 uTint;
        out vec4 FragColor;
        void main()
        {
            if (uMode == 0) {
                float a = texture(uTexture, vUv).a * uWeights[0];
                for (int i = 1; i <= 64; ++i) {
                    if (i > uRadius) {
                        break;
                    }
                    vec2 offset = uDirection * float(i);
                    a += texture(uTexture, vUv + offset).a * uWeights[i];
                    a += texture(uTexture, vUv - offset).a * uWeights[i];
                }
                FragColor = vec4(1.0, 1.0, 1.0, a);
            } else if (uMode == 2) {
                vec4 center = texture(uTexture, vUv);
                vec4 sum = vec4(center.rgb * center.a, center.a) * uWeights[0];
                for (int i = 1; i <= 64; ++i) {
                    if (i > uRadius) {
                        break;
                    }
                    vec2 offset = uDirection * float(i);
                    vec2 loUv = vUv - offset;
                    vec2 hiUv = vUv + offset;
                    vec4 lo = (uDecal != 0 && (loUv.x < 0.0 || loUv.x > 1.0 || loUv.y < 0.0 || loUv.y > 1.0))
                        ? vec4(0.0) : texture(uTexture, loUv);
                    vec4 hi = (uDecal != 0 && (hiUv.x < 0.0 || hiUv.x > 1.0 || hiUv.y < 0.0 || hiUv.y > 1.0))
                        ? vec4(0.0) : texture(uTexture, hiUv);
                    sum += vec4(lo.rgb * lo.a, lo.a) * uWeights[i];
                    sum += vec4(hi.rgb * hi.a, hi.a) * uWeights[i];
                }
                FragColor = sum.a > 0.000001
                    ? vec4(sum.rgb / sum.a, sum.a)
                    : vec4(0.0);
            } else {
                float a = texture(uTexture, vUv).a;
                FragColor = vec4(uTint.rgb, a * uTint.a);
            }
        }
    )";

    program_ = new GLProgram(vertexSrc, fragmentSrc);

    // Full-screen quad in normalized device coordinates with matching UVs.
    const float quad[] = {
        // pos        uv
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    initialized_ = true;
}

void GaussianBlurProgram::destroyTargets()
{
    if (fboA_ != 0) {
        glDeleteFramebuffers(1, &fboA_);
        fboA_ = 0;
    }
    if (fboB_ != 0) {
        glDeleteFramebuffers(1, &fboB_);
        fboB_ = 0;
    }
    if (textureA_ != 0) {
        glDeleteTextures(1, &textureA_);
        textureA_ = 0;
    }
    if (textureB_ != 0) {
        glDeleteTextures(1, &textureB_);
        textureB_ = 0;
    }
    targetWidth_ = 0;
    targetHeight_ = 0;
}

void GaussianBlurProgram::release()
{
    if (!initialized_) {
        return;
    }

    if (program_ != nullptr) {
        delete program_;
        program_ = nullptr;
    }
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    destroyTargets();
    initialized_ = false;
}

namespace {

GLuint createBlurTarget(int width, int height, GLuint &framebuffer)
{
    GLint previousFramebuffer = 0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    if (!complete) {
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &texture);
        framebuffer = 0;
        return 0;
    }
    return texture;
}

} // namespace

bool GaussianBlurProgram::ensureTargets(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (fboA_ != 0 && fboB_ != 0 && targetWidth_ == width && targetHeight_ == height) {
        return true;
    }

    destroyTargets();
    textureA_ = createBlurTarget(width, height, fboA_);
    textureB_ = createBlurTarget(width, height, fboB_);
    if (textureA_ == 0 || textureB_ == 0) {
        destroyTargets();
        return false;
    }
    targetWidth_ = width;
    targetHeight_ = height;
    return true;
}

void GaussianBlurProgram::drawQuad()
{
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void GaussianBlurProgram::blurPass(GLuint srcTexture, GLuint dstFramebuffer, int width, int height,
                                   const glm::vec2 &direction, const wsc::render::GaussianKernel &kernel)
{
    blurPassImpl(srcTexture, dstFramebuffer, width, height, direction, kernel, 0, false);
}

void GaussianBlurProgram::blurImagePass(GLuint srcTexture, GLuint dstFramebuffer, int width, int height,
                                        const glm::vec2 &direction,
                                        const wsc::render::GaussianKernel &kernel, bool decal)
{
    blurPassImpl(srcTexture, dstFramebuffer, width, height, direction, kernel, 2, decal);
}

void GaussianBlurProgram::blurPassImpl(GLuint srcTexture, GLuint dstFramebuffer, int width, int height,
                                       const glm::vec2 &direction,
                                       const wsc::render::GaussianKernel &kernel, int mode, bool decal)
{
    if (!initialized_) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, dstFramebuffer);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);

    program_->use();
    program_->setInt("uMode", mode);
    program_->setInt("uTexture", 0);
    program_->setInt("uDecal", decal ? 1 : 0);
    program_->setVec2("uDirection", direction);

    const int radius = std::min(kernel.radius(), kMaxRadius);
    program_->setInt("uRadius", radius);
    for (int i = 0; i <= radius; ++i) {
        program_->setFloat("uWeights[" + std::to_string(i) + "]", kernel.weights[static_cast<std::size_t>(i)]);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, srcTexture);
    drawQuad();
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GaussianBlurProgram::composite(GLuint srcTexture, const glm::vec4 &tint)
{
    if (!initialized_) {
        return;
    }

    program_->use();
    program_->setInt("uMode", 1);
    program_->setInt("uTexture", 0);
    program_->setVec4("uTint", tint);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, srcTexture);
    drawQuad();
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace wsc::opengl
