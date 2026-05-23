#include "DrawText.h"

#include <iostream>
#include <string>

#include <glad/glad.h>
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

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

    const std::string vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;

        uniform mat4 uProjection;
        uniform mat4 uTransform;

        void main()
        {
            gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
        }
    )";

    const std::string fragmentSrc = R"(
        #version 330 core
        uniform vec4 uColor;

        out vec4 FragColor;

        void main()
        {
            FragColor = uColor;
        }
    )";

    program_ = new GLProgram(vertexSrc, fragmentSrc);

    glGenVertexArrays(1, &VAO_);
    glGenBuffers(1, &VBO_);

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    initialized_ = true;
}

void DrawTextProgram::release()
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

void DrawTextProgram::draw(const RenderContext &context, const DrawTextData &data)
{
    if (!initialized_) {
        std::cerr << "DrawTextProgram has not been initialized." << std::endl;
        return;
    }

    if (data.vertices.empty()) {
        return;
    }

    program_->use();

    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(context.getWidth()),
                                            static_cast<float>(context.getHeight()), 0.0f);
    program_->setMat4("uProjection", projection);
    program_->setMat4("uTransform", data.transform);
    program_->setVec4("uColor", glm::vec4(data.color[0], data.color[1], data.color[2], data.color[3]));

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(data.vertices.size() * sizeof(float)),
                 data.vertices.data(),
                 GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(data.getVertexCount()));
    glBindVertexArray(0);
}
