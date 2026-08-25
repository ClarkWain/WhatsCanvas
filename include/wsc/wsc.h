#pragma once

/**
 * @file wsc.h
 * @brief Convenience umbrella header for the common WhatsCanvas drawing API.
 *
 * WhatsCanvas requires C++17. Installed packages should normally be consumed
 * through one of the imported CMake targets: `WhatsCanvas::Software`,
 * `WhatsCanvas::OpenGL`, or `WhatsCanvas::OpenGLES`.
 *
 * Quick start for a simple off-screen drawing:
 * 1. Pick a backend (`Software` is the simplest starting point).
 * 2. Create a Canvas with the desired size.
 * 3. Call `beginFrame()` before drawing anything.
 * 4. Configure a `Paint` (color, stroke width, fill/stroke style, etc.).
 * 5. Draw geometry or text.
 * 6. Call `endFrame()`. If the canvas is rendering to a window/output target,
 *    call `present()` after the frame ends.
 *
 * The smallest off-screen program is:
 * @code{.cpp}
 * #include <wsc/wsc.h>
 *
 * auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 256, 256);
 * if (!canvas) return 1;
 * canvas->beginFrame();
 * canvas->drawColor(wsc::Color::WHITE);
 * wsc::Paint paint;
 * paint.setColor(wsc::Color(40, 120, 240));
 * canvas->drawCircle(128, 128, 64, paint);
 * canvas->endFrame();
 * return canvas->savePixelsPPM("frame.ppm") ? 0 : 2;
 * @endcode
 *
 * A Canvas is rendering-thread confined. Every frame follows
 * `beginFrame()` -> draw calls -> `endFrame()`; call `present()` afterwards
 * only for a configured output target. OpenGL/OpenGL ES hosts must make their
 * context current and call Canvas::loadOpenGL() before creating the Canvas.
 *
 * Common startup checklist:
 * - For the first app, prefer `Canvas::Backend::Software` to validate drawing
 *   logic and resource lifetime before switching to a GL-backed backend.
 * - Check the return value of `Canvas::create()` and `Canvas::loadOpenGL()`.
 * - Do not call draw methods outside a frame or on a different thread from the
 *   Canvas's owner thread.
 * - When using a window-backed target (`OutputTarget::ToWindow()`), call
 *   `present()` after `endFrame()` only for that output target.
 *
 * @see README.md in this installed directory for package linking, high-DPI,
 * resource ownership, context-loss, image, and font integration guidance.
 * Advanced integrations include their feature header explicitly:
 * - `<wsc/FontResolver.h>` for custom/lazy/remote font providers;
 * - `<wsc/FontSystem.h>` for font registration and installed-font discovery;
 * - `<wsc/CanvasStats.h>` for detailed frame diagnostics;
 * - `<wsc/Log.h>` for the logging configuration API.
 */

#include "Export.h"
#include "Version.h"
#include "Canvas.h"
#include "Color.h"
#include "Font.h"
#include "Image.h"
#include "ImageFilter.h"
#include "Matrix.h"
#include "Paint.h"
#include "Path.h"
#include "Picture.h"
#include "Surface.h"
#include "TextureSource.h"
#include "base.h"
