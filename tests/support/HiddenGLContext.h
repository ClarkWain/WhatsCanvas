// Hidden OpenGL 3.3 context for tests that require real rendered pixels.
#pragma once
#include <stdexcept>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <wsc/Canvas.h>

class HiddenGLContext {
public:
    HiddenGLContext() {
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        window_ = glfwCreateWindow(256, 96, "Pixel regression", nullptr, nullptr);
        if (!window_) {
            glfwTerminate();
            throw std::runtime_error("OpenGL 3.3 context unavailable");
        }
        glfwMakeContextCurrent(window_);
        if (!wsc::Canvas::loadOpenGL(
                reinterpret_cast<wsc::Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
            glfwDestroyWindow(window_);
            glfwTerminate();
            throw std::runtime_error("OpenGL loading failed");
        }
    }
    ~HiddenGLContext() { glfwDestroyWindow(window_); glfwTerminate(); }
    HiddenGLContext(const HiddenGLContext &) = delete;
    HiddenGLContext &operator=(const HiddenGLContext &) = delete;
private:
    GLFWwindow *window_ = nullptr;
};
