#include "GaussianBlurProgram.h"

#include <algorithm>
#include <cmath>
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
        #ifdef WHATSCANVAS_OPENGL_ES
        precision highp float;
        precision highp int;
        #endif
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
        #ifdef WHATSCANVAS_OPENGL_ES
        precision highp float;
        precision highp int;
        #endif
        in vec2 vUv;
        uniform sampler2D uTexture;
        uniform int uMode;        // 0 = alpha blur, 1 = composite, 2 = RGBA blur, 3 = inner shadow
        uniform vec2 uDirection;  // texel step along the blur axis
        uniform int uRadius;
        uniform int uDecal;
        uniform int uColorAdjust;
        uniform int uSourcePremultiplied;
        uniform int uOutputStraight;
        uniform int uResampleStraightAlpha;
        uniform vec3 uColorAdjustment; // saturation, brightness, contrast
        uniform float uGrain;
        uniform float uWeights[65];
        uniform vec4 uTint;
        uniform sampler2D uOriginalTexture;
        uniform vec2 uInnerShadowOffset;
        uniform vec4 uInnerShadowColor;
        out vec4 FragColor;

        vec4 fetchStraightPremultiplied(ivec2 coord)
        {
            ivec2 size = textureSize(uTexture, 0);
            if (uDecal != 0
                && (coord.x < 0 || coord.x >= size.x
                    || coord.y < 0 || coord.y >= size.y)) {
                return vec4(0.0);
            }
            ivec2 bounded = clamp(coord, ivec2(0), size - ivec2(1));
            vec4 color = texelFetch(uTexture, bounded, 0);
            return vec4(color.rgb * color.a, color.a);
        }

        vec4 samplePremultiplied(vec2 uv)
        {
            if (uSourcePremultiplied != 0) {
                if (uDecal != 0
                    && (uv.x < 0.0 || uv.x > 1.0
                        || uv.y < 0.0 || uv.y > 1.0)) {
                    return vec4(0.0);
                }
                return texture(uTexture, uv);
            }
            if (uResampleStraightAlpha == 0) {
                if (uDecal != 0
                    && (uv.x < 0.0 || uv.x > 1.0
                        || uv.y < 0.0 || uv.y > 1.0)) {
                    return vec4(0.0);
                }
                vec4 color = texture(uTexture, uv);
                return vec4(color.rgb * color.a, color.a);
            }
            vec2 size = vec2(textureSize(uTexture, 0));
            vec2 position = uv * size - vec2(0.5);
            ivec2 base = ivec2(floor(position));
            vec2 fraction = fract(position);
            vec4 c00 = fetchStraightPremultiplied(base);
            vec4 c10 = fetchStraightPremultiplied(base + ivec2(1, 0));
            vec4 c01 = fetchStraightPremultiplied(base + ivec2(0, 1));
            vec4 c11 = fetchStraightPremultiplied(base + ivec2(1, 1));
            return mix(mix(c00, c10, fraction.x),
                       mix(c01, c11, fraction.x), fraction.y);
        }

        float sampleAlphaDecal(vec2 uv)
        {
            ivec2 size = textureSize(uTexture, 0);
            vec2 position = uv * vec2(size) - vec2(0.5);
            ivec2 base = ivec2(floor(position));
            vec2 fraction = fract(position);
            float samples[4];
            ivec2 taps[4] = ivec2[4](
                base, base + ivec2(1, 0),
                base + ivec2(0, 1), base + ivec2(1, 1));
            for (int i = 0; i < 4; ++i) {
                ivec2 tap = taps[i];
                samples[i] =
                    tap.x < 0 || tap.x >= size.x
                    || tap.y < 0 || tap.y >= size.y
                    ? 0.0 : texelFetch(uTexture, tap, 0).a;
            }
            return mix(mix(samples[0], samples[1], fraction.x),
                       mix(samples[2], samples[3], fraction.x), fraction.y);
        }

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
                vec4 sum = samplePremultiplied(vUv) * uWeights[0];
                for (int i = 1; i <= 64; ++i) {
                    if (i > uRadius) {
                        break;
                    }
                    vec2 offset = uDirection * float(i);
                    vec2 loUv = vUv - offset;
                    vec2 hiUv = vUv + offset;
                    sum += samplePremultiplied(loUv) * uWeights[i];
                    sum += samplePremultiplied(hiUv) * uWeights[i];
                }
                if (uOutputStraight == 0) {
                    FragColor = sum;
                    return;
                }
                vec4 color = sum.a > 0.000001
                    ? vec4(sum.rgb / sum.a, sum.a)
                    : vec4(0.0);
                if (uColorAdjust != 0 && color.a > 0.000001) {
                    float luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
                    color.rgb = vec3(luma) + (color.rgb - vec3(luma)) * uColorAdjustment.x;
                    color.rgb = ((color.rgb - vec3(0.5)) * uColorAdjustment.z + vec3(0.5))
                        * uColorAdjustment.y;
                    color.rgb = clamp(color.rgb, 0.0, 1.0);
                }
                if (uGrain > 0.0 && color.a > 0.000001) {
                    float noise = fract(sin(dot(gl_FragCoord.xy,
                        vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
                    color.rgb = clamp(color.rgb + vec3(noise * uGrain), 0.0, 1.0);
                }
                FragColor = color;
            } else if (uMode == 3) {
                vec4 original = texture(uOriginalTexture, vUv);
                if (uSourcePremultiplied != 0 && original.a > 0.000001) {
                    original.rgb /= original.a;
                }
                if (original.a <= 0.000001) {
                    FragColor = vec4(0.0);
                    return;
                }
                vec2 shadowUv = vUv - uInnerShadowOffset;
                float blurredAlpha = sampleAlphaDecal(shadowUv);
                float coverage =
                    clamp((original.a - blurredAlpha) / original.a, 0.0, 1.0)
                    * uInnerShadowColor.a;
                FragColor = vec4(
                    mix(original.rgb, uInnerShadowColor.rgb, coverage),
                    original.a);
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
    blurPassImpl(srcTexture, dstFramebuffer, width, height, direction, kernel,
                 0, false, 1.0f, 1.0f, 1.0f, 0.0f, false, true, false);
}

void GaussianBlurProgram::blurImagePass(GLuint srcTexture, GLuint dstFramebuffer, int width, int height,
                                        const glm::vec2 &direction,
                                        const wsc::render::GaussianKernel &kernel, bool decal,
                                        float saturation, float brightness, float contrast,
                                        float grain, bool sourcePremultiplied,
                                        bool outputStraight,
                                        bool resampleStraightAlpha)
{
    blurPassImpl(srcTexture, dstFramebuffer, width, height, direction, kernel,
                 2, decal, saturation, brightness, contrast, grain,
                 sourcePremultiplied, outputStraight, resampleStraightAlpha);
}

void GaussianBlurProgram::blurPassImpl(GLuint srcTexture, GLuint dstFramebuffer, int width, int height,
                                       const glm::vec2 &direction,
                                       const wsc::render::GaussianKernel &kernel, int mode, bool decal,
                                       float saturation, float brightness, float contrast, float grain,
                                       bool sourcePremultiplied, bool outputStraight,
                                       bool resampleStraightAlpha)
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
    const bool colorAdjust = std::abs(saturation - 1.0f) > 1e-6f
        || std::abs(brightness - 1.0f) > 1e-6f
        || std::abs(contrast - 1.0f) > 1e-6f;
    program_->setInt("uColorAdjust", colorAdjust ? 1 : 0);
    program_->setInt("uSourcePremultiplied", sourcePremultiplied ? 1 : 0);
    program_->setInt("uOutputStraight", outputStraight ? 1 : 0);
    program_->setInt("uResampleStraightAlpha", resampleStraightAlpha ? 1 : 0);
    program_->setVec3("uColorAdjustment", glm::vec3(saturation, brightness, contrast));
    program_->setFloat("uGrain", grain);
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

void GaussianBlurProgram::innerShadowPass(
    GLuint blurredTexture, GLuint originalTexture,
    GLuint dstFramebuffer, int width, int height,
    const glm::vec2 &offsetUv, const glm::vec4 &color,
    bool sourcePremultiplied)
{
    if (!initialized_) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, dstFramebuffer);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);

    program_->use();
    program_->setInt("uMode", 3);
    program_->setInt("uTexture", 0);
    program_->setInt("uOriginalTexture", 1);
    program_->setInt("uSourcePremultiplied", sourcePremultiplied ? 1 : 0);
    program_->setVec2("uInnerShadowOffset", offsetUv);
    program_->setVec4("uInnerShadowColor", color);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, blurredTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, originalTexture);
    drawQuad();
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace wsc::opengl
