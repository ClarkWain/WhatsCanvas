// Vulkan on-screen presentation example.
//
// Renders an animated WhatsCanvas frame on the Vulkan backend (off-screen) and
// presents it to a GLFW window via Canvas::setOutputTarget(ToWindow) + present(),
// blits the rendered image into the swapchain. Set WHATSCANVAS_MAX_FRAMES to run
// a fixed number of frames and exit (used for automated verification).

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdlib>
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
	if (!Canvas::isBackendAvailable(Canvas::Backend::Vulkan)) {
		std::cout << "[VulkanPresent] Vulkan not available on this machine; skipping." << std::endl;
		return 0;
	}

	if (!glfwInit()) {
		std::cerr << "[VulkanPresent] FAIL: glfwInit failed." << std::endl;
		return 1;
	}
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Vulkan owns rendering; no GL context

	const int width = 800;
	const int height = 600;
	GLFWwindow *window = glfwCreateWindow(width, height, "WhatsCanvas - Vulkan Present", nullptr, nullptr);
	if (window == nullptr) {
		std::cerr << "[VulkanPresent] FAIL: could not create window." << std::endl;
		glfwTerminate();
		return 1;
	}

	std::unique_ptr<Canvas> canvas = Canvas::create(Canvas::Backend::Vulkan, width, height);
	if (!canvas) {
		std::cerr << "[VulkanPresent] FAIL: create(Vulkan) returned null." << std::endl;
		glfwTerminate();
		return 1;
	}
	canvas->initializeContext();

	NativeSurface surface;
#if defined(_WIN32)
	surface.platform = NativeSurface::Platform::Win32;
	surface.window = glfwGetWin32Window(window);
#endif
	if (!canvas->setOutputTarget(OutputTarget::ToWindow(surface))) {
		std::cerr << "[VulkanPresent] FAIL: setOutputTarget(Window) failed." << std::endl;
		glfwTerminate();
		return 1;
	}

	int maxFrames = 0;
	if (const char *env = std::getenv("WHATSCANVAS_MAX_FRAMES")) {
		maxFrames = std::atoi(env);
	}

	const double start = glfwGetTime();
	int frame = 0;
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		const float t = static_cast<float>(glfwGetTime() - start);

		canvas->beginFrame();
		canvas->drawColor(Color(24, 26, 34));

		Paint background;
		background.setAntiAlias(true);
		background.setLinearGradient(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
		                             Color(40, 44, 60), Color(18, 20, 28));
		canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)), background);

		const float cx = width * 0.5f + std::cos(t) * 180.0f;
		const float cy = height * 0.5f + std::sin(t * 1.3f) * 120.0f;
		Paint circle;
		circle.setAntiAlias(true);
		circle.setRadialGradient(cx, cy, 90.0f, Color(120, 200, 255), Color(40, 90, 160));
		canvas->drawCircle(cx, cy, 90.0f, circle);

		canvas->save();
		canvas->translate(width * 0.5f, height * 0.5f);
		canvas->rotate(t * 0.7f);
		Paint box;
		box.setAntiAlias(true);
		box.setColor(Color(255, 180, 80));
		canvas->drawRoundRect(RectF(-60.0f, -40.0f, 120.0f, 80.0f), 18.0f, box);
		canvas->restore();

		Paint labelPaint;
		labelPaint.setColor(Color::WHITE);
		labelPaint.setTextSize(26.0f);
		canvas->drawText("WhatsCanvas - Vulkan Present", 24.0f, 40.0f, labelPaint);

		canvas->endFrame();
		canvas->present();

		if (maxFrames > 0 && ++frame >= maxFrames) {
			break;
		}
	}

	glfwDestroyWindow(window);
	glfwTerminate();
	std::cout << "[VulkanPresent] OK" << std::endl;
	return 0;
}
