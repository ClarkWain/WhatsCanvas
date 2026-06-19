#include "DrawLines.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <glad/glad.h>
#include <string>
#include "DrawValidation.h"

DrawLinesProgram* DrawLinesProgram::instance_ = nullptr;

DrawLinesProgram::DrawLinesProgram()
{
}

DrawLinesProgram::~DrawLinesProgram()
{
    release();
}

void DrawLinesProgram::initialize()
{
    if (initialized_)
    {
        return;
    }

    std::string vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec4 aColor;
        uniform mat4 uProjection;
        uniform mat4 uTransform;

        out vec4 color;

        void main()
        {
            gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
            color = aColor;
        }
    )";

    std::string fragmentSrc = R"(
        #version 330 core
        out vec4 FragColor;

        in vec4 color;

        void main()
        {
            FragColor = color;
        }
    )";

    program_ = new GLProgram(vertexSrc, fragmentSrc);

    glGenVertexArrays(1, &VAO_);
    glGenBuffers(1, &VBO_);

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);

    glBufferData(GL_ARRAY_BUFFER, maxLines_ * 6 * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // Preallocate vertexCache
    vertexCache_.reserve(maxLines_ * 12);

    initialized_ = true;
}

void DrawLinesProgram::release()
{
    if (!initialized_)
    {
        return;
    }

    if (program_ != nullptr)
        delete program_;

    if (VAO_ != -1)
        glDeleteVertexArrays(1, &VAO_);

    if (VBO_ != -1)
        glDeleteBuffers(1, &VBO_);

    initialized_ = false;
}

void DrawLinesProgram::draw(const RenderContext &context, const DrawLinesData &data)
{
    if (!DrawValidation::validateProgram(initialized_, "DrawLinesProgram::draw")) {
        return;
    }

    if (!DrawValidation::validateVertexData(data.getLineCount(), "DrawLinesProgram::draw")) {
        return;
    }

    const size_t requiredSize = data.getLineCount() * 6 * 6;
    
    // Reallocate the buffer only when required
    if (requiredSize > maxLines_ * 6 * 6) {
        maxLines_ = static_cast<int>(requiredSize * BUFFER_GROW_FACTOR);  // geometric growth policy
        glBindBuffer(GL_ARRAY_BUFFER, VBO_);
        glBufferData(GL_ARRAY_BUFFER, maxLines_ * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        vertexCache_.reserve(maxLines_);
    }

    // Reuse vertexCache
    vertexCache_.clear();
    vertexCache_.reserve(requiredSize);

    // Process vertex data in batches
    for (size_t i = 0; i < data.points.size(); i += 4) {
        float x1 = data.points[i];
        float y1 = data.points[i + 1];
        float x2 = data.points[i + 2];
        float y2 = data.points[i + 3];

        // Compute direction and normal vectors
        float dx = x2 - x1;
        float dy = y2 - y1;
        float length = sqrt(dx * dx + dy * dy);
        dx /= length;
        dy /= length;
        float nx = -dy * data.width * 0.5f;
        float ny = dx * data.width * 0.5f;

        // Four quad vertices
        float vertices[12] = {
            x1 + nx, y1 + ny,
            x1 - nx, y1 - ny,
            x2 - nx, y2 - ny,
            x2 + nx, y2 + ny,
            x1 + nx, y1 + ny,
            x2 - nx, y2 - ny
        };

        // Keep pixel-space coordinates and let the shader handle projection/model transforms
        for (int j = 0; j < 12; j += 2) {
            vertexCache_.push_back(vertices[j]);
            vertexCache_.push_back(vertices[j + 1]);
            // Append color values
            vertexCache_.push_back(data.color[0]);
            vertexCache_.push_back(data.color[1]);
            vertexCache_.push_back(data.color[2]);
            vertexCache_.push_back(data.color[3]);
        }
    }

    program_->use();
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(context.getWidth()), static_cast<float>(context.getHeight()), 0.0f);
    program_->setMat4("uProjection", projection);
    program_->setMat4("uTransform", data.transform);

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexCache_.size() * sizeof(float), vertexCache_.data());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertexCache_.size() / 6));
    glBindVertexArray(0);
}

