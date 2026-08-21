#pragma once

/**
 * @file wsc.h
 * @brief Convenience umbrella header for the common WhatsCanvas drawing API.
 *
 * WhatsCanvas requires C++17. Installed packages should normally be consumed
 * through one of the imported CMake targets: `WhatsCanvas::Software`,
 * `WhatsCanvas::OpenGL`, or `WhatsCanvas::OpenGLES`.
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
