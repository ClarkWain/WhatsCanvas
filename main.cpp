#include <iostream>
#include <cmath>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "canvas/Canvas.h"
#include "canvas/Image.h"
#include "canvas/Paint.h"
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

const float PI = 3.14159265359f;

// 回调函数：当窗口大小变化时调整视口
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

int main() {
    std::cout << "Starting application..." << std::endl;

    // 初始化 GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }
    std::cout << "GLFW initialized successfully." << std::endl;

    // 设置 GLFW 上下文版本和 OpenGL 配置
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); // 主版本号
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3); // 次版本号
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // 使用核心模式

    // 在 macOS 上需要启用兼容性视图
    #ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif

    // 创建窗口
    GLFWwindow* window = glfwCreateWindow(800, 600, "OpenGL Window", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    std::cout << "GLFW window created successfully." << std::endl;

    // 设置当前上下文
    glfwMakeContextCurrent(window);
    std::cout << "GLFW context set successfully." << std::endl;

    // 初始化 GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }
    std::cout << "GLAD initialized successfully." << std::endl;

    // 检查 OpenGL 版本
    std::cout << "OpenGL " << glGetString(GL_VERSION) << " loaded." << std::endl;

    // 设置视口
    glViewport(0, 0, 800, 600);

    // 注册窗口大小调整的回调函数
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // 设置清除颜色
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);


        Canvas::initialize();
    
    Canvas canvas;
    canvas.setSize(800, 600);
    
    Paint paint1;
    paint1.setStrokeWidth(20.0f);
    paint1.setStyle(Paint::Style::STROKE);
    
    // 动画参数
    const float centerX = 400.0f;
    const float centerY = 300.0f;
    const float radius = 100.0f;
    const int numPoints = 5;
    const float rotationSpeed = 1.0f;
    const float colorSpeed = 0.5f;  // 颜色变化速度
    
    // 主循环
    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        
        float currentTime = (float)glfwGetTime();
        float rotation = currentTime * rotationSpeed;
        
        // 计算颜色
        float r = (sin(currentTime * colorSpeed) + 1.0f) * 0.5f;
        float g = (sin(currentTime * colorSpeed + 2.0f * PI / 3.0f) + 1.0f) * 0.5f;
        float b = (sin(currentTime * colorSpeed + 4.0f * PI / 3.0f) + 1.0f) * 0.5f;
        paint1.setColor(Color(r * 255, g * 255, b * 255));
        
        canvas.beginFrame();
        
        // 计算并存储顶点
        std::vector<std::pair<float, float>> points;
        for (int i = 0; i < numPoints; i++) {
            float angle = rotation + i * (2 * PI / numPoints);
            float x = centerX + radius * cos(angle);
            float y = centerY + radius * sin(angle);
            points.push_back({x, y});
        }
        
        // 绘制线条
        for (int i = 0; i < numPoints; i++) {
            int next = (i + 2) % numPoints;
            canvas.drawLine(
                points[i].first, points[i].second,
                points[next].first, points[next].second,
                paint1
            );
            canvas.drawPoint(points[i].first, points[i].second, paint1);
        }
        
        canvas.endFrame();
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    canvas.finalize();

    // 终止 GLFW
    glfwTerminate();
    std::cout << "GLFW terminated. Exiting application." << std::endl;

    return 0;
}