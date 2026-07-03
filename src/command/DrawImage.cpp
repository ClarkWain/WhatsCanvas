#include "DrawImage.h"

#include <iostream>
#include <string>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "DrawValidation.h"
#include "opengl/GlobalIndexBuffers.h"
#include "opengl/GLShaderSource.h"
#include "opengl/ClipMaskUniforms.h"
#include "render/GammaCorrect.h"

DrawImageProgram *DrawImageProgram::instance_ = nullptr;

DrawImageProgram::DrawImageProgram()
{
}

DrawImageProgram::~DrawImageProgram()
{
    release();
}

void DrawImageProgram::initialize()
{
    if (initialized_) {
        return;
    }

    const std::string vertexSrc = std::string(wsc::opengl::shaderVersionDirective()) + R"(
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aUv;

        uniform mat4 uProjection;
        uniform mat4 uTransform;

        out vec2 vUv;
        out vec2 vLocalPos;

        void main()
        {
            gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
            vUv = aUv;
            vLocalPos = aPos;
        }
    )";

    const std::string fragmentSrc = std::string(wsc::opengl::shaderVersionDirective())
        + wsc::opengl::clipMaskFragmentUniforms() + R"(
        in vec2 vUv;
        in vec2 vLocalPos;

        uniform sampler2D uTexture;
        uniform vec4 uTintColor;
        uniform float uAlpha;
        uniform bool uUseColorMatrix;
        uniform mat4 uColorMatrix;
        uniform vec4 uColorMatrixOffset;
        uniform int uTileMode;
        uniform int uGradientType;
        uniform int uGradientTileMode;
        uniform vec2 uLinearStart;
        uniform vec2 uLinearEnd;
        uniform vec2 uRadialCenter;
        uniform float uRadialRadius;
        uniform int uGradientStopCount;
        uniform float uGradientStopPositions[8];
        uniform vec4 uGradientStopColors[8];

        out vec4 FragColor;

        float applyGradientTile(float t, out float visibility)
        {
            visibility = 1.0;
            if (uGradientTileMode == 1) {
                return fract(t);
            }
            if (uGradientTileMode == 2) {
                float period = floor(t);
                float localT = t - period;
                if (mod(abs(period), 2.0) > 0.5) {
                    localT = 1.0 - localT;
                }
                return localT;
            }
            if (uGradientTileMode == 3) {
                visibility = (t >= 0.0 && t <= 1.0) ? 1.0 : 0.0;
                return clamp(t, 0.0, 1.0);
            }
            return clamp(t, 0.0, 1.0);
        }

        vec4 sampleGradient(float t)
        {
            float visibility = 1.0;
            t = applyGradientTile(t, visibility);
            if (visibility <= 0.0 || uGradientStopCount <= 0) {
                return vec4(0.0);
            }
            if (uGradientStopCount == 1 || t <= uGradientStopPositions[0]) {
                return uGradientStopColors[0] * visibility;
            }
            for (int i = 1; i < 8; ++i) {
                if (i >= uGradientStopCount) {
                    break;
                }
                if (t <= uGradientStopPositions[i]) {
                    float startPos = uGradientStopPositions[i - 1];
                    float endPos = uGradientStopPositions[i];
                    float span = max(endPos - startPos, 0.0001);
                    float localT = clamp((t - startPos) / span, 0.0, 1.0);
                    return mix(uGradientStopColors[i - 1], uGradientStopColors[i], localT) * visibility;
                }
            }
            return uGradientStopColors[uGradientStopCount - 1] * visibility;
        }

        void main()
        {
            if (uTileMode == 3 && (vUv.x < 0.0 || vUv.x > 1.0 || vUv.y < 0.0 || vUv.y > 1.0)) {
                FragColor = vec4(0.0);
                return;
            }

            vec4 texColor = texture(uTexture, vUv);
            vec4 paintColor = uTintColor;
            if (uGradientType == 1) {
                vec2 direction = uLinearEnd - uLinearStart;
                float lengthSq = max(dot(direction, direction), 0.0001);
                float t = dot(vLocalPos - uLinearStart, direction) / lengthSq;
                paintColor = sampleGradient(t);
            } else if (uGradientType == 2) {
                float t = length(vLocalPos - uRadialCenter) / max(uRadialRadius, 0.0001);
                paintColor = sampleGradient(t);
            }
            vec4 color = vec4(texColor.rgb * paintColor.rgb, texColor.a * paintColor.a * uAlpha);
            if (uUseColorMatrix) {
                color = clamp(uColorMatrix * color + uColorMatrixOffset, 0.0, 1.0);
            }
            if (uClipEnabled != 0) {
                color.a *= texture(uClipMask, gl_FragCoord.xy / uClipViewport).r;
            }
            FragColor = color;
        }
    )";

    program_ = new GLProgram(vertexSrc, fragmentSrc);

    glGenVertexArrays(1, &VAO_);
    vertexBuffer_.initialize(16);

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_.handle());

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    initialized_ = true;
}

void DrawImageProgram::release()
{
    if (!initialized_) {
        return;
    }

    if (program_ != nullptr) {
        delete program_;
        program_ = nullptr;
    }

    if (VAO_ != static_cast<unsigned int>(-1)) {
        glDeleteVertexArrays(1, &VAO_);
        VAO_ = static_cast<unsigned int>(-1);
    }

    vertexBuffer_.release();

    initialized_ = false;
}

void DrawImageProgram::draw(const RenderContext &context, const DrawImageData &data)
{
    if (!DrawValidation::validateProgram(initialized_, "DrawImageProgram::draw")) {
        return;
    }

    if (!DrawValidation::validateResource(data.imageResource, "imageResource", "DrawImageProgram::draw")) {
        return;
    }

    if (!DrawValidation::validateImageDimensions(data.width, data.height, "DrawImageProgram::draw")) {
        return;
    }

    const float left = data.x;
    const float top = data.y;
    const float right = data.x + data.width;
    const float bottom = data.y + data.height;

    const float vertices[] = {
        left,  top,    data.u0, data.v0,
        right, top,    data.u1, data.v0,
        right, bottom, data.u1, data.v1,
        left,  bottom, data.u0, data.v1
    };

    program_->use();
    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(context.getWidth()),
                                            static_cast<float>(context.getHeight()), 0.0f);
    program_->setMat4("uProjection", projection);
    program_->setMat4("uTransform", data.transform);
    float tintColor[4] = {data.tintColor[0], data.tintColor[1], data.tintColor[2], data.tintColor[3]};
    GammaCorrect::srgbToLinear4(tintColor);
    program_->setVec4("uTintColor", glm::vec4(tintColor[0], tintColor[1], tintColor[2], tintColor[3]));
    program_->setFloat("uAlpha", data.alpha);
    const glm::mat4 colorMatrix(
        data.colorMatrix[0], data.colorMatrix[1], data.colorMatrix[2], data.colorMatrix[3],
        data.colorMatrix[4], data.colorMatrix[5], data.colorMatrix[6], data.colorMatrix[7],
        data.colorMatrix[8], data.colorMatrix[9], data.colorMatrix[10], data.colorMatrix[11],
        data.colorMatrix[12], data.colorMatrix[13], data.colorMatrix[14], data.colorMatrix[15]);
    program_->setInt("uUseColorMatrix", data.hasColorMatrix ? 1 : 0);
    program_->setMat4("uColorMatrix", colorMatrix);
    program_->setVec4("uColorMatrixOffset", glm::vec4(data.colorMatrixOffset[0], data.colorMatrixOffset[1],
                                                       data.colorMatrixOffset[2], data.colorMatrixOffset[3]));
    int tileMode = 0;
    if (data.tileMode == DrawImageTileMode::Repeat) {
        tileMode = 1;
    } else if (data.tileMode == DrawImageTileMode::Mirror) {
        tileMode = 2;
    } else if (data.tileMode == DrawImageTileMode::Decal) {
        tileMode = 3;
    }
    program_->setInt("uTileMode", tileMode);
    program_->setInt("uGradientType", static_cast<int>(data.gradientType));
    program_->setInt("uGradientTileMode", static_cast<int>(data.gradientTileMode));
    program_->setVec2("uLinearStart", glm::vec2(data.gradientStart[0], data.gradientStart[1]));
    program_->setVec2("uLinearEnd", glm::vec2(data.gradientEnd[0], data.gradientEnd[1]));
    program_->setVec2("uRadialCenter", glm::vec2(data.radialCenter[0], data.radialCenter[1]));
    program_->setFloat("uRadialRadius", data.radialRadius);
    program_->setInt("uGradientStopCount", data.gradientStopCount);
    for (std::size_t i = 0; i < DrawImageData::kMaxGradientStops; ++i) {
        program_->setFloat("uGradientStopPositions[" + std::to_string(i) + "]", data.gradientStopPositions[i]);
        float stopColor[4] = {
            data.gradientStopColors[i * 4 + 0],
            data.gradientStopColors[i * 4 + 1],
            data.gradientStopColors[i * 4 + 2],
            data.gradientStopColors[i * 4 + 3]
        };
        GammaCorrect::srgbToLinear4(stopColor);
        program_->setVec4("uGradientStopColors[" + std::to_string(i) + "]", glm::vec4(stopColor[0], stopColor[1], stopColor[2], stopColor[3]));
    }
    program_->setInt("uTexture", 0);
    wsc::opengl::applyClipMaskUniforms(program_, context);

    context.bindImageResource(data.imageResource, data.sampling, data.tileMode, data.mipmapsReady);

    glBindVertexArray(VAO_);
    vertexBuffer_.upload(vertices, 16);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, GlobalIndexBuffers::quadBuffer());
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
