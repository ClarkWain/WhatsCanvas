// Hidden OpenGL 3.3 context for tests that require real rendered pixels.
#pragma once
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <wsc/Canvas.h>

class GLContextUnavailable : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    int report() const {
        const char *required = std::getenv("WHATSCANVAS_REQUIRE_GL_CONTEXT");
        const bool fail = required && std::string(required) != "0";
        std::cerr << (fail ? "FAIL: " : "SKIP: ") << "OpenGL context: " << what() << '\n';
        return fail ? 1 : 0;
    }
};

class HiddenGLContext {
public:
    HiddenGLContext() {
        if (!glfwInit()) throw GLContextUnavailable("GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
        window_ = glfwCreateWindow(256, 96, "Pixel regression", nullptr, nullptr);
        if (!window_) {
            glfwTerminate();
            throw GLContextUnavailable("OpenGL 3.3 context unavailable");
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
