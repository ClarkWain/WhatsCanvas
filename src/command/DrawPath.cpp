
#include "DrawPath.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <glad/glad.h>

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
    std::string vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec4 aColor;
        uniform mat4 uProjection;
        uniform mat4 uTransform;
        out vec4 vColor;
        void main()
        {
            vColor = aColor;
            gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
        }
    )";

    std::string fragmentSrc = R"(
        #version 330 core
        out vec4 FragColor;
        uniform vec4 uColor;
        uniform int uUseVertexColor;
        in vec4 vColor;
        void main()
        {
            FragColor = uUseVertexColor != 0 ? vColor : uColor;
        }
    )";

    program_ = new GLProgram(vertexSrc, fragmentSrc);

    // Create the VAO and VBO
    glGenVertexArrays(1, &VAO_);
    glGenBuffers(1, &VBO_);
    glGenBuffers(1, &CBO_);

    // Bind the VAO and VBO
    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);

    // Preallocate the buffer
    glBufferData(GL_ARRAY_BUFFER, maxVertices_ * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    // Configure vertex attributes
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, CBO_);
    glBufferData(GL_ARRAY_BUFFER, maxVertices_ * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    // Unbind the current objects
    glBindVertexArray(0);

    vertexCache_.reserve(maxVertices_);

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

    if (VBO_ != -1)
        glDeleteBuffers(1, &VBO_);

    if (CBO_ != -1)
        glDeleteBuffers(1, &CBO_);

    initialized_ = false;
}

void DrawPathProgram::draw(const RenderContext &context, const DrawPathData &data)
{
    if (!initialized_)
    {
        std::cerr << "DrawPathProgram has not been initialized." << std::endl;
        return;
    }

    if (data.points.empty())
        return;

    // Resize the buffer when needed
    size_t requiredSize = data.points.size();
    if (requiredSize > maxVertices_)
    {
        while (requiredSize > maxVertices_)
            maxVertices_ *= BUFFER_GROW_FACTOR;
        glBindBuffer(GL_ARRAY_BUFFER, VBO_);
        glBufferData(GL_ARRAY_BUFFER, maxVertices_ * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, CBO_);
        glBufferData(GL_ARRAY_BUFFER, maxVertices_ * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        vertexCache_.reserve(maxVertices_);
    }

    // Set the projection matrix
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(context.getWidth()), static_cast<float>(context.getHeight()), 0.0f);
    program_->use();
    program_->setMat4("uProjection", projection);
    program_->setMat4("uTransform", data.transform);
    program_->setVec4("uColor", glm::make_vec4(data.color));
    program_->setInt("uUseVertexColor", data.hasVertexColors() ? 1 : 0);

    // Upload vertex data
    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, requiredSize * sizeof(float), data.points.data());

    if (data.hasVertexColors()) {
        glBindBuffer(GL_ARRAY_BUFFER, CBO_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, data.colors.size() * sizeof(float), data.colors.data());
    }

    // Draw according to the selected draw mode
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
}
