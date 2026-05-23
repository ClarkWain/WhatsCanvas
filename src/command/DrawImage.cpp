#include "DrawImage.h"

#include <iostream>
#include <string>

#include <glad/glad.h>
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

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

    const std::string vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aUv;

        uniform mat4 uProjection;
        uniform mat4 uTransform;

        out vec2 vUv;

        void main()
        {
            gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
            vUv = aUv;
        }
    )";

    const std::string fragmentSrc = R"(
        #version 330 core
        in vec2 vUv;

        uniform sampler2D uTexture;
        uniform vec4 uTintColor;
        uniform float uAlpha;
        uniform bool uUseColorMatrix;
        uniform mat4 uColorMatrix;
        uniform vec4 uColorMatrixOffset;
        uniform int uTileMode;

        out vec4 FragColor;

        void main()
        {
            if (uTileMode == 3 && (vUv.x < 0.0 || vUv.x > 1.0 || vUv.y < 0.0 || vUv.y > 1.0)) {
                FragColor = vec4(0.0);
                return;
            }

            vec4 texColor = texture(uTexture, vUv);
            vec4 color = vec4(texColor.rgb * uTintColor.rgb, texColor.a * uTintColor.a * uAlpha);
            if (uUseColorMatrix) {
                color = clamp(uColorMatrix * color + uColorMatrixOffset, 0.0, 1.0);
            }
            FragColor = color;
        }
    )";

    program_ = new GLProgram(vertexSrc, fragmentSrc);

    glGenVertexArrays(1, &VAO_);
    glGenBuffers(1, &VBO_);

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

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

    if (VBO_ != static_cast<unsigned int>(-1)) {
        glDeleteBuffers(1, &VBO_);
        VBO_ = static_cast<unsigned int>(-1);
    }

    initialized_ = false;
}

void DrawImageProgram::draw(const RenderContext &context, const DrawImageData &data)
{
    if (!initialized_) {
        std::cerr << "DrawImageProgram has not been initialized." << std::endl;
        return;
    }

    if (data.textureID == 0 || data.width <= 0.0f || data.height <= 0.0f) {
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
        left,  top,    data.u0, data.v0,
        right, bottom, data.u1, data.v1,
        left,  bottom, data.u0, data.v1
    };

    program_->use();
    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(context.getWidth()),
                                            static_cast<float>(context.getHeight()), 0.0f);
    program_->setMat4("uProjection", projection);
    program_->setMat4("uTransform", data.transform);
    program_->setVec4("uTintColor", glm::vec4(data.tintColor[0], data.tintColor[1], data.tintColor[2], data.tintColor[3]));
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
    program_->setInt("uTexture", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, data.textureID);
    const GLint textureWrap = data.tileMode == DrawImageTileMode::Repeat
        ? GL_REPEAT
        : (data.tileMode == DrawImageTileMode::Mirror ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, textureWrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, textureWrap);
    if (data.sampling == DrawImageSampling::Nearest) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else if (data.sampling == DrawImageSampling::MipmapLinear) {
        if (!data.mipmapsReady) {
            glGenerateMipmap(GL_TEXTURE_2D);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}
