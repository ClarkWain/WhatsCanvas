// OpenGL on-screen presentation example.
//
// GL is host-owned: this example creates the window and GL context (GLFW),
// makes it current, and lets WhatsCanvas render into the default framebuffer.
// It then presents through the unified Canvas::setOutputTarget / present() API
// API (WGL SwapBuffers on Windows). Where the native handle is not wired
// (non-Windows here), it falls back to glfwSwapBuffers, so the demo still runs.

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>
#include <iostream>
#include <memory>

#include <wsc/wsc.h>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <GLFW/glfw3native.h>
#endif

using namespace wsc;

int main()
{
	if (!glfwInit()) {
		std::cerr << "[GLPresent] FAIL: glfwInit failed." << std::endl;
		return 1;
	}

	const int width = 800;
	const int height = 600;
	GLFWwindow *window = glfwCreateWindow(width, height, "WhatsCanvas - GL Present", nullptr, nullptr);
	if (window == nullptr) {
		std::cerr << "[GLPresent] FAIL: could not create window." << std::endl;
		glfwTerminate();
		return 1;
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	if (!Canvas::loadOpenGL(reinterpret_cast<Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
		std::cerr << "[GLPresent] FAIL: loadOpenGL failed." << std::endl;
		glfwTerminate();
		return 1;
	}

	Canvas canvas; // default = OpenGL backend, renders into the window's default FBO
	canvas.setSize(width, height);
	canvas.initializeContext();

	NativeSurface surface;
#if defined(_WIN32)
	surface.platform = NativeSurface::Platform::Win32;
	surface.window = glfwGetWin32Window(window);
#endif
	const bool present = canvas.setOutputTarget(OutputTarget::ToWindow(surface));
	if (!present) {
		std::cout << "[GLPresent] Note: unified present unavailable here; using glfwSwapBuffers." << std::endl;
	}

	const double start = glfwGetTime();
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		int fbw = width;
		int fbh = height;
		glfwGetFramebufferSize(window, &fbw, &fbh);
		if (fbw > 0 && fbh > 0 && (fbw != canvas.getWidth() || fbh != canvas.getHeight())) {
			canvas.setSize(fbw, fbh);
			canvas.resizeOutput(fbw, fbh);
		}

		const float t = static_cast<float>(glfwGetTime() - start);
		const float w = static_cast<float>(canvas.getWidth());
		const float h = static_cast<float>(canvas.getHeight());

		canvas.beginFrame();
		canvas.drawColor(Color(24, 26, 34));

		Paint background;
		background.setAntiAlias(true);
		background.setLinearGradient(0.0f, 0.0f, w, h, Color(40, 44, 60), Color(18, 20, 28));
		canvas.drawRect(RectF(0.0f, 0.0f, w, h), background);

		const float cx = w * 0.5f + std::cos(t) * 180.0f;
		const float cy = h * 0.5f + std::sin(t * 1.3f) * 120.0f;
		Paint circle;
		circle.setAntiAlias(true);
		circle.setRadialGradient(cx, cy, 90.0f, Color(120, 200, 255), Color(40, 90, 160));
		circle.setShadowLayer(24.0f, 0.0f, 10.0f, Color(0, 0, 0, 120));
		canvas.drawCircle(cx, cy, 90.0f, circle);

		canvas.save();
		canvas.translate(w * 0.5f, h * 0.5f);
		canvas.rotate(t * 0.7f);
		Paint box;
		box.setAntiAlias(true);
		box.setColor(Color(255, 180, 80));
		canvas.drawRoundRect(RectF(-60.0f, -40.0f, 120.0f, 80.0f), 18.0f, box);
		canvas.restore();

		Paint labelPaint;
		labelPaint.setColor(Color::WHITE);
		labelPaint.setTextSize(26.0f);
		canvas.drawText("WhatsCanvas - GL Present", 24.0f, 40.0f, labelPaint);

		canvas.flush();
		if (present) {
			canvas.present();
		} else {
			glfwSwapBuffers(window);
		}
	}

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
