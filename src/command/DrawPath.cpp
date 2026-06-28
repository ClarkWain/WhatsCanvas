
#include "DrawPath.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <glad/glad.h>
#include "DrawValidation.h"
#include "render/GammaCorrect.h"
#include "opengl/GLShaderSource.h"

DrawPathProgram* DrawPathProgram::instance_ = nullptr;

DrawPathProgram::DrawPathProgram()
{
}

DrawPathProgram::~DrawPathProgram()
{
    release();
}

void DrawPathProgram::initialize()
{
    if (initialized_)
        return;

    // Create the shader program
    std::string vertexSrc = std::string(wsc::opengl::shaderVersionDirective()) + R"(
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec4 aColor;
        uniform mat4 uProjection;
        uniform mat4 uTransform;
        out vec4 vColor;
        out vec2 vLocalPos;
        void main()
        {
            vColor = aColor;
            vLocalPos = aPos;
            gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
        }
    )";

    std::string fragmentSrc = std::string(wsc::opengl::shaderVersionDirective()) +
#if defined(WHATSCANVAS_OPENGL_ES)
    R"(
        out vec4 FragColor;
        uniform vec4 uColor;
        uniform int uUseVertexColor;
        uniform int uGradientType;
        uniform int uGradientTileMode;
        uniform vec2 uLinearStart;
        uniform vec2 uLinearEnd;
        uniform vec2 uRadialCenter;
        uniform float uRadialRadius;
        uniform int uGradientStopCount;
        uniform float uGradientStopPositions[8];
        uniform vec4 uGradientStopColors[8];
        in vec4 vColor;
        in vec2 vLocalPos;

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
            if (uGradientType == 1) {
                vec2 direction = uLinearEnd - uLinearStart;
                float lengthSq = max(dot(direction, direction), 0.0001);
                float t = dot(vLocalPos - uLinearStart, direction) / lengthSq;
                FragColor = sampleGradient(t);
                return;
            }
            if (uGradientType == 2) {
                float t = length(vLocalPos - uRadialCenter) / max(uRadialRadius, 0.0001);
                FragColor = sampleGradient(t);
                return;
            }
            FragColor = uUseVertexColor != 0 ? vColor : uColor;
        }
    )";
#else
    R"(
        out vec4 FragColor;
        uniform vec4 uColor;
        uniform int uUseVertexColor;
        uniform int uGradientType;
        uniform int uGradientTileMode;
        uniform vec2 uLinearStart;
        uniform vec2 uLinearEnd;
        uniform vec2 uRadialCenter;
        uniform float uRadialRadius;
        uniform int uGradientStopCount;
        uniform int uUseGradientTexelBuffer;
        uniform samplerBuffer uGradientStops;
        uniform float uGradientStopPositions[8];
        uniform vec4 uGradientStopColors[8];
        in vec4 vColor;
        in vec2 vLocalPos;

        float gradientStopPosition(int index)
        {
            if (uUseGradientTexelBuffer != 0) {
                return texelFetch(uGradientStops, index * 5).r;
            }
            return uGradientStopPositions[index];
        }

        vec4 gradientStopColor(int index)
        {
            if (uUseGradientTexelBuffer != 0) {
                return vec4(
                    texelFetch(uGradientStops, index * 5 + 1).r,
                    texelFetch(uGradientStops, index * 5 + 2).r,
                    texelFetch(uGradientStops, index * 5 + 3).r,
                    texelFetch(uGradientStops, index * 5 + 4).r);
            }
            return uGradientStopColors[index];
        }

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
            if (uGradientStopCount == 1 || t <= gradientStopPosition(0)) {
                return gradientStopColor(0) * visibility;
            }
            for (int i = 1; i < 8; ++i) {
                if (i >= uGradientStopCount) {
                    break;
                }
                if (t <= gradientStopPosition(i)) {
                    float startPos = gradientStopPosition(i - 1);
                    float endPos = gradientStopPosition(i);
                    float span = max(endPos - startPos, 0.0001);
                    float localT = clamp((t - startPos) / span, 0.0, 1.0);
                    return mix(gradientStopColor(i - 1), gradientStopColor(i), localT) * visibility;
                }
            }
            return gradientStopColor(uGradientStopCount - 1) * visibility;
        }

        void main()
        {
            if (uGradientType == 1) {
                vec2 direction = uLinearEnd - uLinearStart;
                float lengthSq = max(dot(direction, direction), 0.0001);
                float t = dot(vLocalPos - uLinearStart, direction) / lengthSq;
                FragColor = sampleGradient(t);
                return;
            }
            if (uGradientType == 2) {
                float t = length(vLocalPos - uRadialCenter) / max(uRadialRadius, 0.0001);
                FragColor = sampleGradient(t);
                return;
            }
            FragColor = uUseVertexColor != 0 ? vColor : uColor;
        }
    )";
#endif

    program_ = new GLProgram(vertexSrc, fragmentSrc);

    // Create the VAO
    glGenVertexArrays(1, &VAO_);

    // Initialize stream buffers
    positionBuffer_.initialize(4096);
    colorBuffer_.initialize(8192);

    // Bind the VAO and configure vertex attributes
    glBindVertexArray(VAO_);

    glBindBuffer(GL_ARRAY_BUFFER, positionBuffer_.handle());
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, colorBuffer_.handle());
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    // Unbind
    glBindVertexArray(0);

    initialized_ = true;
}

void DrawPathProgram::release()
{
    if (!initialized_)
        return;

    if (program_ != nullptr)
        delete program_;

    if (VAO_ != -1)
        glDeleteVertexArrays(1, &VAO_);

    positionBuffer_.release();
    colorBuffer_.release();
    gradientStopBuffer_.release();

    initialized_ = false;
}

void DrawPathProgram::draw(const RenderContext &context, const DrawPathData &data)
{
    if (!DrawValidation::validateProgram(initialized_, "DrawPathProgram::draw")) {
        return;
    }

    if (!DrawValidation::validateVertexData(data.getPointCount(), "DrawPathProgram::draw")) {
        return;
    }

    // Upload vertex data via stream buffers
    positionBuffer_.upload(data.points.data(), data.points.size());

    if (data.hasVertexColors()) {
        colorBuffer_.upload(data.colors.data(), data.colors.size());
    }

    // Set the projection matrix
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(context.getWidth()), static_cast<float>(context.getHeight()), 0.0f);
    program_->use();
    program_->setMat4("uProjection", projection);
    program_->setMat4("uTransform", data.transform);

    // Apply gamma correction to uniform color if enabled.
    float color[4] = {data.color[0], data.color[1], data.color[2], data.color[3]};
    GammaCorrect::srgbToLinear4(color);
    program_->setVec4("uColor", glm::make_vec4(color));
    program_->setInt("uUseVertexColor", data.hasVertexColors() ? 1 : 0);
    program_->setInt("uGradientType", static_cast<int>(data.gradientType));
    program_->setInt("uGradientTileMode", static_cast<int>(data.gradientTileMode));
    program_->setVec2("uLinearStart", glm::vec2(data.gradientStart[0], data.gradientStart[1]));
    program_->setVec2("uLinearEnd", glm::vec2(data.gradientEnd[0], data.gradientEnd[1]));
    program_->setVec2("uRadialCenter", glm::vec2(data.radialCenter[0], data.radialCenter[1]));
    program_->setFloat("uRadialRadius", data.radialRadius);
    program_->setInt("uGradientStopCount", data.gradientStopCount);
#if !defined(WHATSCANVAS_OPENGL_ES)
    bool usingGradientTexelBuffer = false;
    if (data.hasShaderGradient()) {
        std::vector<float> gradientStops;
        gradientStops.reserve(static_cast<std::size_t>(data.gradientStopCount) * 5);
        for (int i = 0; i < data.gradientStopCount; ++i) {
            float stopColor[4] = {
                data.gradientStopColors[i * 4 + 0],
                data.gradientStopColors[i * 4 + 1],
                data.gradientStopColors[i * 4 + 2],
                data.gradientStopColors[i * 4 + 3]
            };
            GammaCorrect::srgbToLinear4(stopColor);
            gradientStops.push_back(data.gradientStopPositions[i]);
            gradientStops.push_back(stopColor[0]);
            gradientStops.push_back(stopColor[1]);
            gradientStops.push_back(stopColor[2]);
            gradientStops.push_back(stopColor[3]);
        }

        usingGradientTexelBuffer = gradientStopBuffer_.update(gradientStops.data(), gradientStops.size());
        if (usingGradientTexelBuffer) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_BUFFER, gradientStopBuffer_.textureHandle());
            program_->setInt("uGradientStops", 1);
        }
    }
    program_->setInt("uUseGradientTexelBuffer", usingGradientTexelBuffer ? 1 : 0);
#endif
    for (std::size_t i = 0; i < DrawPathData::kMaxGradientStops; ++i) {
        program_->setFloat("uGradientStopPositions[" + std::to_string(i) + "]", data.gradientStopPositions[i]);
        float stopColor[4] = {
            data.gradientStopColors[i * 4 + 0],
            data.gradientStopColors[i * 4 + 1],
            data.gradientStopColors[i * 4 + 2],
            data.gradientStopColors[i * 4 + 3]
        };
        GammaCorrect::srgbToLinear4(stopColor);
        program_->setVec4("uGradientStopColors[" + std::to_string(i) + "]", glm::make_vec4(stopColor));
    }

    // Bind VAO and draw (StreamBuffer already bound the data via upload)
    glBindVertexArray(VAO_);

    if (data.drawMode == PathDrawMode::Fill)
    {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(data.getPointCount()));
    }
    else if (data.drawMode == PathDrawMode::Stroke)
    {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(data.getPointCount()));
    }
    else if (data.drawMode == PathDrawMode::FillAndStroke)
    {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(data.getPointCount()));
    }

    glBindVertexArray(0);
#if !defined(WHATSCANVAS_OPENGL_ES)
    if (data.hasShaderGradient()) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, 0);
        glActiveTexture(GL_TEXTURE0);
    }
#endif
}
