// Software on-screen presentation example (Windows / GDI).
//
// Opens a GLFW window WITHOUT a GL context and displays a CPU-rendered
// WhatsCanvas frame each iteration via the software swapchain (GDI blit). This
// is the smallest end-to-end "draw into a visible window" demo that needs no
// GPU. It is standalone and not part of the CTest gate (windowed, interactive).

#include <cmath>
#include <iostream>
#include <memory>

#include <wsc/wsc.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// Include the Win32 native header (which pulls in <windows.h>) LAST, after the
// wsc/ and standard headers, so windows.h macros (min/max/...) do not pollute
// them.
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
		std::cerr << "[SoftwarePresent] FAIL: glfwInit failed." << std::endl;
		return 1;
	}

	// No GL context: the software backend blits its CPU framebuffer via GDI.
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	const int width = 800;
	const int height = 600;
	GLFWwindow *window = glfwCreateWindow(width, height, "WhatsCanvas - Software Present", nullptr, nullptr);
	if (window == nullptr) {
		std::cerr << "[SoftwarePresent] FAIL: could not create window." << std::endl;
		glfwTerminate();
		return 1;
	}

	std::unique_ptr<Canvas> canvas = Canvas::create(Canvas::Backend::Software, width, height);
	if (!canvas) {
		std::cerr << "[SoftwarePresent] FAIL: create(Software) returned null." << std::endl;
		glfwDestroyWindow(window);
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
		std::cerr << "[SoftwarePresent] FAIL: setOutputTarget(Window) failed "
		             "(software presentation is currently Windows-only)."
		          << std::endl;
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}

	const double start = glfwGetTime();
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
		circle.setShadowLayer(24.0f, 0.0f, 10.0f, Color(0, 0, 0, 120));
		canvas->drawCircle(cx, cy, 90.0f, circle);

		canvas->save();
		canvas->translate(width * 0.5f, height * 0.5f);
		canvas->rotate(t * 0.7f);
		Paint box;
		box.setAntiAlias(true);
		box.setColor(Color(255, 180, 80));
		canvas->drawRoundRect(RectF(-60.0f, -40.0f, 120.0f, 80.0f), 18.0f, box);
		canvas->restore();

		Paint label;
		label.setColor(Color::WHITE);
		label.setTextSize(26.0f);
		canvas->drawText("WhatsCanvas - Software Present", 24.0f, 40.0f, label);

		canvas->endFrame();
		canvas->present();
	}

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
