#include <iostream>
#include <cmath>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "canvas/Canvas.h"
#include "canvas/Image.h"
#include "canvas/Paint.h"
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

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


    Canvas::initialize(); // 初始化 Canvas

    Canvas canvas;
    canvas.setSize(800, 600); // 设置窗口尺寸

    Image image;
    bool load_success = image.load("C:\\Users\\bassy\\Desktop\\CppDemo\\images\\hello.png");
    std::cout << "Image loaded successfully: " << load_success << std::endl;

        // 创建 Paint 对象
    Paint paint1;
    paint1.setColor(Color::RED); 
    paint1.setStrokeWidth(3.0f);  // 调整点的大小
    paint1.setStyle(Paint::Style::FILL);

    // 时间相关变量
    double lastTime = glfwGetTime();
    const double period = 3.0;  // 3秒周期
    const double amplitude = 100.0;  // sin曲线振幅
    const double yOffset = 300.0;   // y轴偏移量
    const double PI = 3.1415926f;
    
    // 主循环
    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        
        double currentTime = glfwGetTime();
        double phase = (currentTime / period) * 2.0 * PI;
        
        canvas.beginFrame();
        
        // 绘制sin曲线
        std::vector<Point> points;
        std::vector<Point> points2;
        std::vector<Point> points3;
        std::vector<Point> points4;
        std::vector<Point> points5;
        for(int x = 0; x < 800; x += 1) {  // x步进5个像素
            float normalizedX = (x / 800.0) * 4 * PI;  // 在窗口宽度内显示两个完整周期
            float y = amplitude * sin(normalizedX + phase) + yOffset;
            points.push_back(Point(x, y));
            points2.push_back(Point(x, y + 50));
            points3.push_back(Point(x, y - 50));
            points4.push_back(Point(x, y + 100));
            points5.push_back(Point(x, y - 100));
        }
        paint1.setColor(Color::RED);
        canvas.drawPoints(points, paint1);

        paint1.setColor(Color::GREEN);
        canvas.drawPoints(points2, paint1);

        paint1.setColor(Color::BLUE);
        canvas.drawPoints(points3, paint1);

        paint1.setColor(Color::YELLOW);
        canvas.drawPoints(points4, paint1);

        paint1.setColor(Color::CYAN);
        canvas.drawPoints(points5, paint1);
        
        canvas.endFrame();
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 释放资源
    canvas.finalize(); // 释放 Canvas 资源

    // 终止 GLFW
    glfwTerminate();
    std::cout << "GLFW terminated. Exiting application." << std::endl;

    return 0;
}