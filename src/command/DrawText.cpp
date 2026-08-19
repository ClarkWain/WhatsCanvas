#include "DrawText.h"

#include <iostream>
#include <string>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "DrawValidation.h"
#include "opengl/GLShaderSource.h"
#include "opengl/ClipMaskUniforms.h"
#include "render/GammaCorrect.h"

DrawTextProgram *DrawTextProgram::instance_ = nullptr;

DrawTextProgram::DrawTextProgram()
{
}

DrawTextProgram::~DrawTextProgram()
{
    release();
}

void DrawTextProgram::initialize()
{
    if (initialized_) {
        return;
    }

    const std::string vertexSrc = std::string(wsc::opengl::shaderVersionDirective()) + R"(
        layout (location = 0) in vec2 aPos;

        uniform mat4 uProjection;
        uniform mat4 uTransform;

        // highp so subtraction against uLinearStart in the fragment shader
        // survives large logical/world coordinates on GLES mediump defaults.
        out highp vec2 vLocalPos;

        void main()
        {
            gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
            vLocalPos = aPos;
        }
    )";

    const std::string fragmentSrc = std::string(wsc::opengl::shaderVersionDirective())
        + wsc::opengl::clipMaskFragmentUniforms() + R"(
        in highp vec2 vLocalPos;

        uniform vec4 uColor;
        uniform int uGradientType;
        uniform int uGradientTileMode;
        uniform highp vec2 uLinearStart;
        uniform highp vec2 uLinearEnd;
        uniform highp vec2 uRadialCenter;
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
            vec4 outColor = uColor;
            if (uGradientType == 1) {
                vec2 direction = uLinearEnd - uLinearStart;
                float lengthSq = max(dot(direction, direction), 0.0001);
                float t = dot(vLocalPos - uLinearStart, direction) / lengthSq;
                outColor = sampleGradient(t);
            } else if (uGradientType == 2) {
                float t = length(vLocalPos - uRadialCenter) / max(uRadialRadius, 0.0001);
                outColor = sampleGradient(t);
            }
            if (uClipEnabled != 0) {
                outColor.a *= texture(uClipMask, gl_FragCoord.xy / uClipViewport).r;
            }
            FragColor = outColor;
        }
    )";

    program_ = new GLProgram("draw_text", vertexSrc, fragmentSrc);

    glGenVertexArrays(1, &VAO_);
    vertexBuffer_.initialize(4096);

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_.handle());

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    initialized_ = true;
}

void DrawTextProgram::release(bool abandon)
{
    if (!initialized_) {
        return;
    }

    if (program_ != nullptr) {
        if (abandon) program_->abandonVolatile();
        delete program_;
        program_ = nullptr;
    }

    if (VAO_ != static_cast<unsigned int>(-1)) {
        if (!abandon) glDeleteVertexArrays(1, &VAO_);
        VAO_ = static_cast<unsigned int>(-1);
    }

    if (abandon) vertexBuffer_.abandon(); else vertexBuffer_.release();

    initialized_ = false;
}

void DrawTextProgram::draw(const RenderContext &context, const DrawTextData &data)
{
    initialize();
    if (!DrawValidation::validateProgram(initialized_, "DrawTextProgram::draw")) {
        return;
    }

    if (!DrawValidation::validateVertexData(data.getVertexCount(), "DrawTextProgram::draw")) {
        return;
    }

    program_->use();

    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(context.getWidth()),
                                            static_cast<float>(context.getHeight()), 0.0f);
    program_->setMat4("uProjection", projection);
    program_->setMat4("uTransform", data.transform);
    float color[4] = {data.color[0], data.color[1], data.color[2], data.color[3]};
    GammaCorrect::srgbToLinear4(color);
    program_->setVec4("uColor", glm::vec4(color[0], color[1], color[2], color[3]));
    program_->setInt("uGradientType", static_cast<int>(data.gradientType));
    program_->setInt("uGradientTileMode", static_cast<int>(data.gradientTileMode));
    program_->setVec2("uLinearStart", glm::vec2(data.gradientStart[0], data.gradientStart[1]));
    program_->setVec2("uLinearEnd", glm::vec2(data.gradientEnd[0], data.gradientEnd[1]));
    program_->setVec2("uRadialCenter", glm::vec2(data.radialCenter[0], data.radialCenter[1]));
    program_->setFloat("uRadialRadius", data.radialRadius);
    program_->setInt("uGradientStopCount", data.gradientStopCount);
    for (std::size_t i = 0; i < DrawTextData::kMaxGradientStops; ++i) {
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
    wsc::opengl::applyClipMaskUniforms(program_, context);

    glBindVertexArray(VAO_);
    vertexBuffer_.upload(data.vertices.data(), data.vertices.size());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(data.getVertexCount()));
    glBindVertexArray(0);
}
