#include "DrawPoints.h"
#include <glm.hpp>
#include <gtc/type_ptr.hpp>
#include <iostream>
#include <glad/glad.h>
#include <string>
#include "opengl/GLProgram.h"

DrawPointsProgram *DrawPointsProgram::instance_ = nullptr;

DrawPointsProgram::DrawPointsProgram()
{
}

DrawPointsProgram::~DrawPointsProgram()
{
    release();
}

void DrawPointsProgram::initialize()
{
    if (initialized_)
    {
        return;
    }

    std::string vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec4 aColor;
        uniform float uPointSize;

        out vec4 color;

        void main()
        {
            gl_Position = vec4(aPos, 0.0, 1.0);
            gl_PointSize = uPointSize;
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

    // 创建VAO和VBO
    glGenVertexArrays(1, &VAO_);
    glGenBuffers(1, &VBO_);

    // 绑定VAO和VBO
    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);

    // 预分配更大的缓冲区大小
    glBufferData(GL_ARRAY_BUFFER, maxPoints_ * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    // 设置顶点属性（位置和颜色）
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(2 * sizeof(float)));

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    // 解绑
    glBindVertexArray(0);

    initialized_ = true;
}

void DrawPointsProgram::release()
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

void DrawPointsProgram::draw(const RenderContext &context, const DrawPointsData &data)
{
    if (!initialized_)
    {
        std::cerr << "DrawPointProgram has not been initialized." << std::endl;
        return;
    }

    if (data.points.empty()) {
        return;
    }

    glEnable(GL_PROGRAM_POINT_SIZE);
    program_->use();
    program_->setFloat("uPointSize", data.size);

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);

    // 如果点的数量超过了预分配的缓冲区大小，重新分配缓冲区
    if (data.getPointCount() > maxPoints_) {
        maxPoints_ = data.getPointCount();
        glBufferData(GL_ARRAY_BUFFER, maxPoints_ * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    }

    // 计算所有点的顶点数据
    std::vector<float> vertexData;
    vertexData.reserve(data.getPointCount() * 6); // 每个点6个float（2个位置 + 4个颜色）

    for (size_t i = 0; i < data.points.size(); i += 2) {
        // 将点的坐标转换为OpenGL坐标系
        float x = (data.points[i] / context.getWidth()) * 2 - 1;
        float y = 1 - (data.points[i + 1] / context.getHeight()) * 2;
        
        // 位置
        vertexData.push_back(x);
        vertexData.push_back(y);
        // 颜色
        vertexData.push_back(data.color[0]);
        vertexData.push_back(data.color[1]);
        vertexData.push_back(data.color[2]);
        vertexData.push_back(data.color[3]);
    }

    // 更新顶点数据
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexData.size() * sizeof(float), vertexData.data());

    // 批量绘制所有点
    glDrawArrays(GL_POINTS, 0, data.getPointCount());

    glBindVertexArray(0);
}