# Window presentation examples

This directory groups the minimal on-screen presentation hosts for every
supported native backend. They share one CMake entry point while preserving
the existing executable target names:

- `WhatsCanvasGLPresent` — host-owned GLFW OpenGL context.
- `WhatsCanvasSoftwarePresent` — Windows GDI software swapchain.
- `WhatsCanvasVulkanCanvasPresent` — Win32 Vulkan `OutputTarget::ToWindow`.
- `WhatsCanvasMetalPresent` — macOS `CAMetalLayer` presentation.

Configure the repository with `WHATSCANVAS_BUILD_DEMO=ON`, then build the
target appropriate for the enabled backend. Platform-specific targets are
created only where their presentation adapter is available.
