#include "DrawLines.h"
#include <glm.hpp>
#include <gtc/type_ptr.hpp>
#include <iostream>
#include <glad/glad.h>
#include <string>

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

        out vec4 color;

        void main()
        {
            gl_Position = vec4(aPos, 0.0, 1.0);
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

    glBufferData(GL_ARRAY_BUFFER, maxLines_ * 12 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

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
    if (!initialized_)
    {
        std::cerr << "DrawLinesProgram has not been initialized." << std::endl;
        return;
    }

    if (data.points.empty()) {
        return;
    }

    program_->use();
    glLineWidth(data.width);

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);

    if (data.getLineCount() > maxLines_) {
        maxLines_ = data.getLineCount();
        glBufferData(GL_ARRAY_BUFFER, maxLines_ * 12 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    }

    std::vector<float> vertexData;
    vertexData.reserve(data.getLineCount() * 12); // 每条线12个float（4个位置 + 8个颜色）

    for (size_t i = 0; i < data.points.size(); i += 4) {
        float x1 = (data.points[i] / context.getWidth()) * 2 - 1;
        float y1 = 1 - (data.points[i + 1] / context.getHeight()) * 2;
        float x2 = (data.points[i + 2] / context.getWidth()) * 2 - 1;
        float y2 = 1 - (data.points[i + 3] / context.getHeight()) * 2;
        
        // 第一个点
        vertexData.push_back(x1);
        vertexData.push_back(y1);
        // 颜色
        vertexData.push_back(data.color[0]);
        vertexData.push_back(data.color[1]);
        vertexData.push_back(data.color[2]);
        vertexData.push_back(data.color[3]);

        // 第二个点
        vertexData.push_back(x2);
        vertexData.push_back(y2);
        // 颜色
        vertexData.push_back(data.color[0]);
        vertexData.push_back(data.color[1]);
        vertexData.push_back(data.color[2]);
        vertexData.push_back(data.color[3]);
    }

    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexData.size() * sizeof(float), vertexData.data());
    glDrawArrays(GL_LINES, 0, data.getLineCount() * 2);

    glBindVertexArray(0);
}
