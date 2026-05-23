# PrismCanvas

PrismCanvas is a small C++17 canvas playground that builds a Skia-like 2D drawing surface on top of OpenGL. The current demo focuses on path tessellation, strokes, gradients, clipping, transforms, image sampling, text primitives, readback, and simple regression hooks. The architecture is intentionally renderer-facing, so Metal and Vulkan backends can be added without changing the Canvas-facing API shape.

## Highlights

- Immediate-mode `Canvas` API for points, lines, paths, rectangles, rounded rectangles, circles, ovals, arcs, images, and text.
- `Paint` state for stroke/fill styles, alpha, gradients, blend modes, image sampling, tile modes, dash effects, corner effects, and text alignment.
- Save/restore, transforms, clipping, saveLayer-style offscreen composition, hit testing, pixel readback, PPM capture, and FNV-1a pixel hashes.
- OpenGL 3.3 backend with GLFW windowing, GLAD loading, GLM math, STB image/text helpers, and Polyline2D stroke meshes.
- Third-party source projects are pulled with Git submodules.

## Canvas API

The current public `Canvas` surface is intentionally close to familiar 2D canvas APIs. The list below is a compact map of the available entry points and what each one is for.

```cpp
class Canvas {
	struct TextMetrics;                         // Stores measured text width, height, bounds, ascent, and descent.
	enum class ImageFit { FILL, CONTAIN, COVER }; // Controls how an image fits into a destination rectangle.
	enum class ImageAnchor { ... };             // Controls the anchor point used by fitted images.

	static void initialize();                   // Initializes shared canvas rendering resources.
	static void finalize();                     // Releases shared canvas rendering resources.

	void setSize(int width, int height);        // Sets the drawing surface size.
	int getWidth() const;                       // Returns the current surface width.
	int getHeight() const;                      // Returns the current surface height.
	void setColor(Color color);                 // Sets the default canvas color state.

	void drawColor(const Color& color);         // Clears/fills the whole canvas with a solid color.
	void drawPaint(const Paint& paint);         // Fills the whole canvas using Paint state.
	void drawPoint(...);                        // Draws one point, with int/float/Point/PointF overloads.
	void drawPoints(...);                       // Draws multiple points.
	void drawLine(...);                         // Draws one line segment.
	void drawLines(...);                        // Draws independent line segments from point arrays.
	void drawPolyline(...);                     // Draws a connected open polyline.
	void drawPolygon(...);                      // Draws a connected closed polygon.
	void drawRect(...);                         // Draws an axis-aligned rectangle.
	void drawRoundRect(...);                    // Draws rounded rectangles, including independent corner radii.
	void drawCircle(...);                       // Draws a circle.
	void drawOval(...);                         // Draws an oval inside a bounding rectangle.
	void drawArc(...);                          // Draws an arc, optionally connected to the center.
	void drawPath(const Path& path, ...);       // Draws arbitrary paths with fill/stroke Paint state.
	RectF measureStrokeBounds(...);             // Measures the stroke bounds of a path.

	void drawImage(...);                        // Draws an image at a point, into a dst rect, or from src to dst.
	void drawImageFit(...);                     // Draws an image with fill/contain/cover fitting.
	void drawImageNinePatch(...);               // Draws a nine-patch image region into a destination rect.
	void drawImageTiled(...);                   // Draws a repeated/tiled image region.

	void drawText(...);                         // Draws a single text run.
	void drawTextBox(...);                      // Draws wrapped text inside clipped bounds, with optional max lines.
	void drawTextOnPath(...);                   // Places text along a path.
	float measureText(...);                     // Measures text width.
	RectF measureTextBounds(...);               // Measures text bounds.
	TextMetrics measureTextMetrics(...);        // Measures text metrics and bounds in one call.

	int save();                                 // Saves matrix and clip state.
	int saveLayer(...);                         // Saves a composited offscreen layer.
	void restore();                             // Restores the latest saved state or layer.
	int getSaveCount() const;                   // Returns the current save stack depth.
	void restoreToCount(int saveCount);         // Restores back to a previous save count.

	const glm::mat4& getMatrix() const;         // Returns the current transform matrix.
	PointF mapPoint(...);                       // Maps a local point through the current matrix.
	RectF mapRect(...);                         // Maps a rect through the current matrix.
	bool inverseMapPoint(...);                  // Converts a device-space point back to local space.
	bool inverseMapRect(...);                   // Converts a device-space rect back to local space.
	void setMatrix(const glm::mat4& matrix);    // Replaces the current transform matrix.
	void resetMatrix();                         // Resets the transform to identity.
	void concat(const glm::mat4& matrix);       // Concatenates another transform.
	void translate(float dx, float dy);         // Applies translation.
	void scale(float sx, float sy);             // Applies scale.
	void rotate(float radians);                 // Applies rotation in radians.

	void clipRect(...);                         // Intersects the current clip with a rectangle.
	bool hasClip() const;                       // Reports whether a clip is active.
	bool getClipBounds(RectF& bounds) const;    // Returns the current clip bounds.
	bool isPointInClip(...);                    // Tests whether a device-space point is inside the clip.
	bool quickReject(...);                      // Quickly rejects rect/path draws outside canvas or clip bounds.
	bool hitTestPathFill(...);                  // Hit-tests path fill in device space.
	bool hitTestPathStroke(...);                // Hit-tests path stroke in device space.

	void beginFrame();                          // Starts a frame and prepares command collection.
	void flush();                               // Flushes queued draw commands.
	void endFrame();                            // Flushes and completes the current frame.
	bool readPixelsRGBA(...);                   // Reads the framebuffer as top-left-origin RGBA pixels.
	std::vector<unsigned char> readPixelsRGBA() const; // Convenience pixel readback overload.
	bool savePixelsPPM(const std::string& path) const; // Saves a simple RGB PPM screenshot.
	static std::uint64_t hashPixelsRGBA(...);   // Hashes RGBA pixels with FNV-1a 64-bit.
	std::uint64_t computePixelsHashRGBA() const;// Reads and hashes the current framebuffer.
};
```

## Example

Examples live under `example/` and are meant to show how the Canvas API can be used beyond isolated drawing primitives. The current repository includes gameplay-focused demos such as Tetris and Bubble Shooter, and more game/UI samples will continue to be added here as the project grows.

### Tetris

The Tetris example is under [example/game/tetris](example/game/tetris). It uses PrismCanvas to draw the game board, falling blocks, text, preview/score panels, and simple game UI elements.

![Tetris example built with PrismCanvas](images/tetris.jpg)

Run it from the example directory:

```bat
cd example\game\tetris
build.bat
```

```bash
cd example/game/tetris
./build.sh
```

### Bubble Shooter

The Bubble Shooter example is under [example/game/bubble_shooter](example/game/bubble_shooter). It uses PrismCanvas to render a hex-grid bubble field, the aiming guide, the launcher, and a compact HUD panel for score, level, next bubble, controls, and performance stats.

![Bubble Shooter example built with PrismCanvas](images/bubble_shooter.jpg)

Run it from the example directory:

```bat
cd example\game\bubble_shooter
build.bat
```

```bash
cd example/game/bubble_shooter
./build.sh
```

## Requirements

- CMake 3.16 or newer.
- A C++17 compiler.
- Windows: Visual Studio 2022 with the Desktop C++ workload.
- macOS/Linux: OpenGL development libraries and a toolchain supported by GLFW.

## Clone

```bash
git clone --recurse-submodules https://github.com/ClarkWain/PrismCanvas
cd PrismCanvas
```

If the repository was cloned without submodules:

```bash
git submodule update --init --recursive
```

## Build

Windows:

```bat
build.bat --no-run
build.bat
```

macOS/Linux:

```bash
chmod +x build.sh
./build.sh --no-run
./build.sh
```

The demo target is `PrismCanvasDemo`.

## Third-Party Dependencies

Submodules:

- GLFW: window and OpenGL context creation.
- GLM: matrix and vector math.
- STB: image loading and lightweight demo text generation.
- Polyline2D: stroke mesh generation.

Generated source kept in-tree:

- GLAD: generated OpenGL 3.3 core loader files under `third_party/glad`.

## Debug Hooks

The demo supports a few environment variables for quick rendering checks:

```bash
CPPDEMO_CAPTURE_PPM=build/capture.ppm ./build/PrismCanvasDemo
CPPDEMO_PRINT_PIXEL_HASH=1 ./build/PrismCanvasDemo
CPPDEMO_EXPECT_PIXEL_HASH=<uint64> ./build/PrismCanvasDemo
CPPDEMO_EXIT_AFTER_FIRST_FRAME=1 ./build/PrismCanvasDemo
CPPDEMO_FIXED_TIME_SECONDS=1.25 ./build/PrismCanvasDemo
CPPDEMO_DISABLE_MSAA=1 ./build/PrismCanvasDemo
```

Pixel hashes are exact and can vary by GPU, driver, MSAA behavior, and platform. Treat them as a fast local regression aid rather than a portable golden image format.

Use `smoke_test.bat` on Windows or `sh smoke_test.sh` on macOS/Linux to build the Debug target, run one fixed-time non-MSAA frame with pixel readback/hash enabled, and check the log for rendering failure markers. Pass a local expected hash as the first argument to make it strict.

## Roadmap

- Extract the Canvas core into a reusable library target.
- Add backend abstraction points for Metal and Vulkan.
- Replace the current lightweight text path with real font metrics, shaping, and glyph atlas rendering.
- Add automated render tests and a small UI layer on top of Canvas.