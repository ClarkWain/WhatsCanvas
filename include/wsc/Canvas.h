#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "Color.h"
#include "Export.h"
#include "ImageFilter.h"
#include "Paint.h"
#include "Surface.h"
#include "TextureSource.h"
#include "base.h"

class IRenderer;

namespace wsc {
class FontFace;
class FontFallbackChain;
class FontProvider;
class Image;
class CanvasLifecycleTestAccess;
class Matrix4;
class Paint;
class Path;
class Picture;

/// Stateful drawing surface and command recorder exposed by WhatsCanvas.
///
/// A Canvas owns one renderer backend and its backend resources. It is confined
/// to the application's rendering thread; concurrent access to one Canvas, or
/// sharing its Images/external textures across unrelated Canvas instances, is
/// not part of the public contract. OpenGL/OpenGL ES calls require the host
/// context to be current on that thread.
///
/// Coordinates use a top-left origin with X increasing right and Y increasing
/// down. Rect/RectF values are `(x, y, width, height)`, angles are radians, and
/// drawing uses physical pixels unless setDevicePixelRatio() establishes a
/// logical-coordinate scale. Drawing state (matrix, DPR, clip, color and blend
/// mode) is scoped by save()/restore().
///
/// Typical drawing flow:
/// @code{.cpp}
/// auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 256, 256);
/// if (!canvas) return;
///
/// canvas->beginFrame();
/// canvas->drawColor(wsc::Color::WHITE);
///
/// wsc::Paint paint;
/// paint.setColor(wsc::Color(40, 120, 240));
/// canvas->drawRoundRect(wsc::RectF(32, 32, 192, 128), 16.0f, paint);
///
/// canvas->endFrame();
/// @endcode
///
/// A normal frame is `beginFrame()` -> draw calls -> `endFrame()`. Pixel
/// readback and Canvas-as-texture sampling require the frame to have ended;
/// call present() afterwards only when an output target needs presentation.
///
/// `OutputTarget` is a design choice that separates drawing from presentation.
/// A Canvas records commands and renders to an internal backend resource; the
/// output target decides where the finished frame is sent: an offscreen buffer
/// for readback/texture reuse, a native OS window for presentation, or a
/// host-owned framebuffer/swapchain for embedding in an existing graphics
/// pipeline. This lets the same drawing code target different destinations
/// without reworking the drawing commands themselves. The flow is typically:
/// `create -> setOutputTarget -> beginFrame -> draw -> endFrame -> present()`.
///
/// Minimal software frame:
/// @code{.cpp}
/// auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 64, 64);
/// canvas->beginFrame();
/// canvas->drawColor(wsc::Color::WHITE);
/// canvas->endFrame();
/// @endcode
///
/// Minimal OpenGL frame (host setup is abbreviated):
/// @code{.cpp}
/// makeContextCurrent(); // Provided by the window system.
/// wsc::Canvas::loadOpenGL(getProcAddress); // Host GL address lookup.
/// auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, 64, 64);
/// canvas->beginFrame();
/// canvas->drawColor(wsc::Color::WHITE);
/// canvas->endFrame();
/// swapBuffers(); // Provided by the window system.
/// @endcode
///
/// Common lifecycle checklist:
/// - Create a Canvas once for a backend/device and keep it on the same render
///   thread.
/// - Call `beginFrame()` before issuing draw commands.
/// - Use `Paint` to set fill/stroke/text state and `RectF`/`PointF`/`Path` for
///   geometry. `save()`/`restore()` apply state changes within a frame.
/// - End the frame with `endFrame()`. Only present to a window-backed target when
///   the target needs a display update.
/// - Call `finalizeContext()` or destroy the canvas during teardown; do not use a
///   GL-backed canvas after the owning context is lost unless the backend is
///   reinitialized.
class WSC_API Canvas : public ITextureSource
{
public:
	/// Aggregated text metrics returned by measurement helpers.
	/// All values use the current logical coordinate space. `top`/`bottom` and
	/// `bounds` are relative to the text origin selected by Paint.
	struct TextMetrics
	{
		float width = 0.0f;
		float height = 0.0f;
		float top = 0.0f;
		float bottom = 0.0f;
		float ascent = 0.0f;
		float descent = 0.0f;
		float lineGap = 0.0f;
		float lineHeight = 0.0f;
		RectF bounds;
	};

	/// One laid-out UTF-8 line produced by layoutTextBox(). Source offsets and
	/// lengths are byte offsets into the original UTF-8 string.
	struct TextLine
	{
		std::string text;
		std::size_t sourceStart = 0;
		std::size_t sourceLength = 0;
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float lineHeight = 0.0f;
		bool ellipsized = false;
	};
	/// Opaque frame-diagnostic snapshot. Include <wsc/CanvasStats.h> before
	/// accessing its fields; ordinary drawing code does not need that header.
	struct RenderStats;

	using ReadPixelsCallback = std::function<void(std::vector<unsigned char> pixels, int width, int height)>;

	/// How an image is mapped into a destination rectangle.
	enum class ImageFit
	{
		FILL,    ///< Scale independently on each axis to fill the destination.
		CONTAIN, ///< Preserve aspect ratio and keep the whole image visible.
		COVER    ///< Preserve aspect ratio and crop to cover the destination.
	};

	/// Preset alignment used when CONTAIN leaves space or COVER crops content.
	enum class ImageAnchor
	{
		TOP_LEFT,
		TOP,
		TOP_RIGHT,
		LEFT,
		CENTER,
		RIGHT,
		BOTTOM_LEFT,
		BOTTOM,
		BOTTOM_RIGHT
	};

	/// Controls how arcs are closed (open stroke, chord, or pie slice).
	enum class ArcMode
	{
		OPEN,   ///< Arc only, no closing segment.
		CHORD,  ///< Arc with a straight line between start and end points.
		PIE     ///< Arc with lines from center to start and end points.
	};

	/// Initialize every currently registered Canvas. Usually unnecessary because
	/// beginFrame() initializes lazily. GL contexts for all GL canvases must be
	/// current when this process-wide helper is used.
	static void initialize();

	/// Finalize every currently registered Canvas. Prefer finalizeContext() when
	/// different canvases belong to different contexts/threads.
	static void finalize();

	using OpenGLProcAddress = void *(*)(const char *name);
	/// Load the process GL/GLES entry-point table. Call after the host context is
	/// current and before creating/initializing an OpenGL or OpenGLES Canvas.
	/// Returns false for a null loader, an incompatible context, or a build with
	/// no GL-family renderer. Other backends do not call this function.
	static bool loadOpenGL(OpenGLProcAddress loadProcAddress);

	/// Return the active GL version string, or an empty string when GL is not loaded.
	static std::string getOpenGLVersionString();

	/// Enable or disable gamma-correct rendering.
	/// When enabled, all color operations are performed in linear color space,
	/// producing visually correct alpha blending. The GPU framebuffer uses
	/// sRGB format for automatic linear→sRGB conversion on output.
	static void setGammaCorrect(bool enabled);

	/// Check whether gamma-correct rendering is currently enabled.
	static bool isGammaCorrect();

public:
	/// Runtime renderer selection for create(). Availability depends on the
	/// linked package target, build options, platform and host capabilities.
	/// `Auto` picks the first available of Vulkan -> Metal -> OpenGL/OpenGLES ->
	/// Software. Direct3D is reserved and currently unavailable.
	enum class Backend
	{
		Auto,      ///< Select the first available backend.
		OpenGL,    ///< Host-owned desktop OpenGL context.
		OpenGLES,  ///< Host-owned OpenGL ES context (mobile/Web).
		Software,  ///< CPU renderer; no window or graphics API required.
		Vulkan,    ///< Vulkan renderer when included in the OpenGL package build.
		Metal,     ///< Metal renderer on supported Apple package builds.
		Direct3D,  ///< Reserved; create() currently returns nullptr.
	};

	/// Release the Canvas and owned resources. A GL context must still be current
	/// unless finalizeContext() or abandonContext() was called first.
	~Canvas();

	Canvas(const Canvas &) = delete;

	Canvas &operator=(const Canvas &) = delete;

	/// Create a canvas on `backend`, sized to physical `width` x `height` pixels.
	/// Dimensions must be positive before rendering. Returns nullptr when
	/// the backend is unavailable in this build/host. The canvas is sized but
	/// not initialized. Call `beginFrame()` (which initializes lazily) or
	/// `initializeContext()` before drawing. For the OpenGL backends, make your
	/// GL context current and call `loadOpenGL()` before initialization or the
	/// first frame; Software/Vulkan/Metal need no external GL context.
	static std::unique_ptr<Canvas> create(Backend backend, int width, int height);

	/// Try backends in caller-defined order. `Auto` entries are skipped; returns
	/// nullptr if no listed backend can be created.
	static std::unique_ptr<Canvas> create(std::initializer_list<Backend> preferred, int width, int height);

	/// Whether `backend` can be created in this build/host.
	static bool isBackendAvailable(Backend backend);

	/// The backend this canvas was created with.
	Backend backend() const;

	// ITextureSource interface
	int getTextureWidth() const override { return getWidth(); }

	int getTextureHeight() const override { return getHeight(); }

	bool isTextureValid() const override;

	bool isRenderTarget() const override;

	/// Return the current diagnostic counters without synchronizing extra GPU
	/// work. GPU time is valid only when gpuTimeAvailable is true.
	RenderStats getRenderStats() const;

	/// Enable optional backend GPU frame timers. Disabled by default so normal
	/// rendering and comparative benchmarks pay no query overhead.
	void setGpuTimingEnabled(bool enabled);

	/// Initialize backend resources now. Returns false on unavailable/misconfigured
	/// backends, invalid size, or missing/current-context failures. beginFrame()
	/// also initializes lazily but cannot return the failure to the caller.
	bool initializeContext();

	/// Orderly backend teardown. For GL-family backends the owning context must
	/// be current. The Canvas may be initialized again later.
	void finalizeContext();

	/// Forget all GPU objects after an involuntary backend-context loss.
	/// Unlike finalizeContext(), this never attempts to delete objects from the
	/// lost context. The Canvas keeps its CPU-side state and can be initialized
	/// again when a replacement context is current.
	void abandonContext();

	/// Whether backend initialization completed successfully.
	bool isContextInitialized() const;

	/// Purge Canvas-owned caches and derived GPU resources while keeping the
	/// backend initialized. Later draws recreate resources on demand.
	void releaseResources();

protected:
	void *getTextureHandleOpaque() const override;

public:
	// Canvas lifetime and state.
	/// Resize the physical drawing surface and invalidate size-dependent caches.
	/// Pass positive physical-pixel dimensions. Hosts should also call
	/// resizeOutput() for a configured window swapchain.
	void setSize(int width, int height);

	/// Physical drawing-surface width and height in pixels.
	int getWidth() const;

	int getHeight() const;

	/// Convenience current color used by the parameterless draw* helpers.
	void setColor(Color color);

	void setColor(float r, float g, float b, float a = 1.0f);

	Color getColor() const;

	/// Canvas-level blend state for subsequent draws. A non-SRC_OVER value
	/// overrides the Paint blend mode and is scoped by save()/restore().
	void setBlendMode(Paint::BlendMode blendMode);

	Paint::BlendMode getBlendMode() const;

	/// Fill/clear the entire target with a color. Unlike drawPaint(), this
	/// intentionally ignores the current clip.
	void drawColor(const Color &color);

	void drawColor(float r, float g, float b, float a = 1.0f);

	/// Fill the current clip with `paint`, respecting its shader, alpha and blend mode.
	void drawPaint(const Paint &paint);

	// Primitive drawing.
	/// Draw independent points using Paint stroke color/width and the current transform/clip.
	void drawPoint(int x, int y, const Paint &paint);

	void drawPoint(float x, float y, const Paint &paint);

	void drawPoint(const Point &point, const Paint &paint);

	void drawPoint(const PointF &point, const Paint &paint);

	/// Draw every vector element as an independent point. Non-finite PointF values are skipped.
	void drawPoints(const std::vector<Point> &points, const Paint &paint);

	void drawPoints(const std::vector<PointF> &points, const Paint &paint);

	/// Draw one stroked segment. Paint style is treated as stroke semantics.
	void drawLine(int x1, int y1, int x2, int y2, const Paint &paint);

	void drawLine(float x1, float y1, float x2, float y2, const Paint &paint);

	void drawLine(const Point &start, const Point &end, const Paint &paint);

	void drawLine(const PointF &start, const PointF &end, const Paint &paint);

	/// Draw independent segments from pairs `[p0,p1]`, `[p2,p3]`, ...; an odd
	/// final point is ignored.
	void drawLines(const std::vector<Point> &points, const Paint &paint);

	void drawLines(const std::vector<PointF> &points, const Paint &paint);

	/// Stroke one connected open contour. Fewer than two valid points draw nothing.
	void drawPolyline(const std::vector<Point> &points, const Paint &paint);

	void drawPolyline(const std::vector<PointF> &points, const Paint &paint);

	/// Draw one closed contour using Paint fill/stroke style. Fewer than three
	/// valid points draw nothing; repeating the first point is optional.
	void drawPolygon(const std::vector<Point> &points, const Paint &paint);

	void drawPolygon(const std::vector<PointF> &points, const Paint &paint);

	/// Draw `(x,y,width,height)`; negative dimensions are normalized and a zero
	/// dimension draws nothing.
	void drawRect(const RectF &rect, const Paint &paint);

	void drawRect(const Rect &rect, const Paint &paint);

	/// Draw a rounded rectangle. Negative radii become zero and oversized corner
	/// radii are proportionally reduced so adjacent corners fit.
	void drawRoundRect(const RectF &rect, float radius, const Paint &paint);

	void drawRoundRect(const Rect &rect, float radius, const Paint &paint);

	void drawRoundRect(const RectF &rect, float topLeftRadius, float topRightRadius,
					   float bottomRightRadius, float bottomLeftRadius, const Paint &paint);

	void drawRoundRect(const Rect &rect, float topLeftRadius, float topRightRadius,
					   float bottomRightRadius, float bottomLeftRadius, const Paint &paint);

	/// Draw only an outer rounded-rectangle shadow. Spread expands the source
	/// rectangle; negative blur radii become zero.
	void drawBoxShadow(const RectF &rect, float radius, float spread, float blurRadius,
	                   float dx, float dy, const Color &color);

	void drawBoxShadow(const RectF &rect, float topLeftRadius, float topRightRadius,
	                   float bottomRightRadius, float bottomLeftRadius, float spread,
	                   float blurRadius, float dx, float dy, const Color &color);

	/// Draw a circle; non-positive/non-finite radii or coordinates draw nothing.
	void drawCircle(float centerX, float centerY, float radius, const Paint &paint);

	void drawCircle(const PointF &center, float radius, const Paint &paint);

	void drawCircle(const Point &center, float radius, const Paint &paint);

	/// Draw an oval inscribed in normalized `(x,y,width,height)` bounds.
	void drawOval(const RectF &bounds, const Paint &paint);

	void drawOval(const Rect &bounds, const Paint &paint);

	/// Draw an ellipse arc. Zero radians starts at the rightmost point; positive
	/// sweep follows the positive-angle direction in the Canvas' Y-down space
	/// (clockwise on screen) and is clamped to one revolution. Prefer ArcMode;
	/// the boolean overload is retained for compatibility.
	void drawArc(const RectF &bounds, float startRadians, float sweepRadians, bool useCenter, const Paint &paint);

	void drawArc(const Rect &bounds, float startRadians, float sweepRadians, bool useCenter, const Paint &paint);

	void drawArc(const RectF &bounds, float startRadians, float sweepRadians, ArcMode mode, const Paint &paint);

	void drawArc(const Rect &bounds, float startRadians, float sweepRadians, ArcMode mode, const Paint &paint);

	/// Draw all Path contours using the active transform, clip and Paint style.
	/// Empty paths are ignored; input geometry is copied when command recording requires it.
	void drawPath(const Path &path, const Paint &paint);

	/// Return local-space bounds of the actual stroke mesh, including cap, join,
	/// dash and corner effects. Map with mapRect() for device-space bounds.
	RectF measureStrokeBounds(const Path &path, const Paint &paint) const;

	// Image drawing.
	/// Draw a valid Image. The `(x,y)` overload uses its intrinsic pixel size;
	/// destination rectangles scale it; source rectangles use image-pixel
	/// coordinates. Invalid/backend-incompatible images draw nothing. Paint
	/// color tints the image, so use Color::WHITE for original colors.
	void drawImage(const Image &image, float x, float y, const Paint &paint);

	void drawImage(const Image &image, const RectF &dst, const Paint &paint);

	void drawImage(const Image &image, const RectF &src, const RectF &dst, const Paint &paint);

	/// Fit an image into `dst`; custom alignment values are clamped to [0,1].
	void drawImageFit(const Image &image, const RectF &dst, ImageFit fit, const Paint &paint);

	void drawImageFit(const Image &image, const RectF &dst, ImageFit fit, ImageAnchor anchor, const Paint &paint);

	void drawImageFit(const Image &image, const RectF &dst, ImageFit fit, float alignX, float alignY, const Paint &paint);

	/// Draw a nine-patch. `centerSrc` is the stretchable source region in image pixels.
	void drawImageNinePatch(const Image &image, const RectF &centerSrc, const RectF &dst, const Paint &paint);

	/// Draw an image clipped to a rounded destination rectangle.
	void drawImageRounded(const Image &image, const RectF &dst, float radius, const Paint &paint);

	void drawImageRounded(const Image &image, const RectF &dst, float topLeftRadius, float topRightRadius,
	                      float bottomRightRadius, float bottomLeftRadius, const Paint &paint);

	/// Draw an image center-cropped into a circle.
	void drawImageCircle(const Image &image, const PointF &center, float radius, const Paint &paint);

	/// Tile an image across `dst`; explicit tile sizes must be positive.
	void drawImageTiled(const Image &image, const RectF &dst, const Paint &paint);

	void drawImageTiled(const Image &image, const RectF &dst, float tileWidth, float tileHeight, const Paint &paint);

	// Texture-source drawing (supports Canvas-as-texture when render-target mode is active).
	/// NOTE: The source Canvas must have been ended (`endFrame()`) before
	/// passing it to drawImage. Drawing an unfinished Canvas is a silent no-op.
	void drawImage(const ITextureSource &source, float x, float y, const Paint &paint);

	/// NOTE: The source Canvas must have been ended (`endFrame()`) before
	/// passing it to drawImage. Drawing an unfinished Canvas is a silent no-op.
	void drawImage(const ITextureSource &source, const RectF &dst, const Paint &paint);

	/// Decode a file into `image`. Returns false for a null/unreadable/unsupported
	/// file or resource creation failure. Prefer Image::loadFromEncodedMemory
	/// when the host owns asset I/O.
	bool loadImage(Image &image, const char *imagePath);

	/// Decode encoded image bytes. The input is consumed before return and need
	/// not outlive the call.
	bool loadImageFromEncodedMemory(Image &image, const unsigned char *data, int size, bool generateMipmaps = true);

	/// Upload tightly packed RGBA8 bytes (`width*height*4`). Returns false for
	/// invalid dimensions/size, null data, or backend resource failure.
	bool loadImageFromRGBA(Image &image, const unsigned char *pixels, int width, int height,
	                       bool generateMipmaps = false);

	bool loadImageFromRGBA(Image &image, const std::vector<unsigned char> &pixels, int width, int height,
	                       bool generateMipmaps = false);

	/// Replace the complete image snapshot; may change dimensions.
	bool replaceImageRGBA(Image &image, const unsigned char *pixels, int width, int height,
	                      bool generateMipmaps = false);

	bool replaceImageRGBA(Image &image, const std::vector<unsigned char> &pixels, int width, int height,
	                      bool generateMipmaps = false);

	/// Replace a tightly packed RGBA8 sub-rectangle. The region must lie inside
	/// the existing image; mipmaps are regenerated when requested.
	bool updateImageRGBA(Image &image, const unsigned char *pixels, int x, int y, int width, int height,
	                     bool regenerateMipmaps = true);

	bool updateImageRGBA(Image &image, const std::vector<unsigned char> &pixels, int x, int y, int width,
	                     int height, bool regenerateMipmaps = true);

	/// Wrap a caller-owned texture from this Canvas' current GL context. No
	/// ownership transfers; keep it alive until the Image is reset/destroyed.
	bool wrapExternalTexture(Image &image, std::uint32_t textureId, int width, int height,
	                         bool mipmapsGenerated = false);

	/// Wrap an externally-owned id<MTLTexture> as an Image on a Metal canvas.
	/// `texture` must be a valid MTLTexture from this canvas's MTLDevice.
	bool wrapExternalMetalTexture(Image &image, void *texture, int width, int height,
	                              bool mipmapsGenerated = false);

	// Text drawing and measurement.
	/// Draw one normalized UTF-8 run. Invalid sequences are replaced
	/// predictably. `(x,y)` uses Paint alignment/baseline; text does not wrap.
	void drawText(const std::string &text, float x, float y, const Paint &paint);

	/// Draw wrapped text inside a rectangle, optionally limiting lines and
	/// ellipsizing overflow.
	void drawTextBox(const std::string &text, const RectF &bounds, const Paint &paint);

	void drawTextBox(const std::string &text, const RectF &bounds, float lineHeight, const Paint &paint);

	void drawTextBox(const std::string &text, const RectF &bounds, float lineHeight, int maxLines, bool ellipsize, const Paint &paint);

	/// Layout normalized UTF-8 without drawing. Returned source ranges are UTF-8
	/// byte offsets and remain valid only for the exact input string.
	std::vector<TextLine> layoutTextBox(const std::string &text, const RectF &bounds, const Paint &paint) const;

	std::vector<TextLine> layoutTextBox(const std::string &text, const RectF &bounds, float lineHeight, const Paint &paint) const;

	std::vector<TextLine> layoutTextBox(const std::string &text, const RectF &bounds, float lineHeight,
	                                    int maxLines, bool ellipsize, const Paint &paint) const;

	/// Draw normalized UTF-8 along the Path baseline. `hOffset` advances along
	/// the path and `vOffset` moves perpendicular to it.
	void drawTextOnPath(const std::string &text, const Path &path, const Paint &paint);

	void drawTextOnPath(const std::string &text, const Path &path, float hOffset, float vOffset, const Paint &paint);

	/// Measure using the same shaping/fallback state as drawing, without emitting
	/// draw commands. Returned values are in logical coordinates.
	float measureText(const std::string &text, const Paint &paint) const;

	/// Return logical bounds relative to the Paint-selected text origin.
	RectF measureTextBounds(const std::string &text, const Paint &paint) const;

	/// Return width, font vertical metrics, line gap/height and logical bounds.
	TextMetrics measureTextMetrics(const std::string &text, const Paint &paint) const;

	/// Register a font face so its family can be selected via Paint::setFontFamily.
	bool registerFontFace(const FontFace &face);

	/// Add an application font provider. The Canvas retains shared ownership;
	/// portable text supports lazy asset/dynamic providers.
	bool addFontProvider(std::shared_ptr<FontProvider> provider);

	/// Re-enumerate installed fonts and update this Canvas while preserving
	/// explicitly registered fonts and fallback chains.
	bool refreshSystemFonts();

	/// Set the fallback chain used to resolve glyphs missing from the primary font.
	bool setFontFallbackChain(const FontFallbackChain &chain);

	/// Text backend selection. `Auto`/`Portable` use the portable FreeType/stb
	/// glyph-atlas backend; `DirectWrite` and `CoreText` use the native Windows
	/// and Apple backends when available (falling back to portable otherwise).
	enum class TextBackend
	{
		Auto,
		Portable,
		DirectWrite,
		CoreText,
	};
	/// Anti-aliasing mode for the native text backend.
	enum class TextRenderMode
	{
		Grayscale,
		ClearType,
	};
	/// Select the text backend (and, for the native backend, its render mode).
	/// Resets text state (registered fonts / fallback chains), so call it before
	/// registering fonts. Returns true if the requested backend is now active.
	bool setTextBackend(TextBackend backend, TextRenderMode renderMode = TextRenderMode::Grayscale);

	/// The text backend currently in effect.
	TextBackend textBackend() const;

	// Save stack and offscreen layering.
	/// Push the current graphics state and return the previous save count. Pass
	/// that value to restoreToCount() to balance this call, even if nested code
	/// performs additional saves.
	int save();

	/// Begin a bounds-clipped offscreen layer composited with `paint` on restore.
	/// This allocates/intermediates rendering work and is more expensive than
	/// save(); keep bounds tight and use it only when group compositing is needed.
	int saveLayer(const RectF &bounds, const Paint &paint);

	int saveLayer(const Rect &bounds, const Paint &paint);

	/// Begin an offscreen layer with optional filters for its content and backdrop.
	///
	/// A backdrop filter sees only commands recorded before this call. The
	/// filtered backdrop and subsequently drawn layer content are composited
	/// together when restore() closes the layer.
	int saveLayer(const RectF &bounds, const Paint &paint, const LayerOptions &options);

	int saveLayer(const Rect &bounds, const Paint &paint, const LayerOptions &options);

	/// Pop the most recent state. An unmatched restore is a safe no-op.
	void restore();

	/// Current state-stack depth; the base state has count 1.
	int getSaveCount() const;

	/// Pop to `saveCount`; values below 1 are clamped to the base state.
	void restoreToCount(int saveCount);

	// Transform and hit-test helpers.
	/// Current total transform matrix (local space to device space).
	Matrix4 getMatrix() const;

	/// Map local coordinates through the current matrix and DPR to physical
	/// device coordinates. A non-invertible/non-finite transform makes inverse
	/// operations return false.
	PointF mapPoint(const PointF &point) const;

	/// Map four corners and return their physical device-space axis-aligned bounds.
	RectF mapRect(const RectF &rect) const;

	RectF mapRect(const Rect &rect) const;

	/// Inverse-map physical device coordinates; false leaves output unspecified
	/// when the current transform is singular or non-finite.
	bool inverseMapPoint(const PointF &devicePoint, PointF &localPoint) const;

	bool inverseMapRect(const RectF &deviceRect, RectF &localRect) const;

	/// Clip and hit-test query points are expressed in physical device coordinates.
	bool isPointInClip(const PointF &devicePoint) const;

	/// Test a physical device point against the current clip and inverse-mapped Path.
	bool hitTestPathFill(const Path &path, const PointF &devicePoint) const;

	bool hitTestPathStroke(const Path &path, const PointF &devicePoint, float strokeWidth) const;

	/// Whether an explicit clip is active. getClipBounds() returns device-space
	/// conservative bounds and false for an empty effective clip.
	bool hasClip() const;

	bool getClipBounds(RectF &bounds) const;

	/// Conservative visibility test in local coordinates. False means "possibly
	/// visible", not guaranteed visible; true means drawing can be skipped.
	bool quickReject(const RectF &rect) const;

	bool quickReject(const Rect &rect) const;

	bool quickReject(const Path &path, const Paint &paint) const;

	/// Intersect the current clip with a transformed anti-aliased Path. Clip
	/// operations only shrink the clip and are scoped by save()/restore().
	void clipPath(const Path &path);

	/// Intersect the current clip with a rectangle.
	void clipRect(const RectF &rect);

	void clipRect(const Rect &rect);

	/// Replace the logical transform while preserving the current DPR base scale.
	void setMatrix(const Matrix4 &matrix);

	/// Reset to identity in logical space (the DPR base scale remains active).
	void resetMatrix();

	/// Set the device pixel ratio (HiDPI / content scale). Folded into the root
	/// transform so `resetMatrix()` restores a `ratio`x base scale: draw in
	/// logical coordinates and content — including crisp, device-resolution
	/// text — renders at `ratio`x physical pixels. The canvas size is expected to
	/// be the physical framebuffer size. Default 1.0.
	///
	/// Side effect (M2): this resets the CURRENT transform to a dpr-scaled base
	/// (as if `resetMatrix()` were called). The ratio is stored on the current
	/// graphics state, so it is saved/restored in lockstep with the matrix by
	/// save()/restore() — a restore() that crosses this call restores the
	/// matching prior ratio and matrix together.
	void setDevicePixelRatio(float ratio);

	/// The current device pixel ratio (default 1.0). Reflects the value on the
	/// current graphics state, so it tracks save()/restore().
	float devicePixelRatio() const;

	/// Post-multiply the current logical matrix by another column-major transform.
	void concat(const Matrix4 &matrix);

	/// Translate/scale/rotate the current logical transform. Rotation is in radians.
	void translate(float dx, float dy);

	void scale(float sx, float sy);

	void rotate(float radians);

	// Retained drawing.
	/// Record backend-neutral Canvas operations into an immutable Picture.
	/// Recording does not submit GPU work and leaves this Canvas' state exactly
	/// as it was before the callback. Nested recording is rejected.
	std::shared_ptr<const Picture> recordPicture(
		const std::function<void(Canvas &)> &recorder);

	/// Replay a retained Picture inside an implicit save/restore boundary.
	void drawPicture(const Picture &picture);

	/// Replay an isolated static Picture through a context-keyed raster cache.
	/// This is intended for RepaintBoundary-like content whose compositing as one
	/// offscreen layer is semantically correct. GPU resources are derived data
	/// and are purged before context teardown.
	void drawPictureRasterized(const Picture &picture);

	/// Set the soft memory budget shared by rasterized Pictures on this Canvas.
	/// A zero budget disables raster caching and falls back to drawPicture().
	void setRetainedPictureRasterCacheBudgetBytes(std::size_t bytes);

	/// Current soft raster-cache budget; zero disables retained rasterization.
	std::size_t retainedPictureRasterCacheBudgetBytes() const;

	// Frame and pixel readback helpers.
	/// Begin a frame of drawing. Initializes the backend lazily, resets per-frame
	/// state/statistics, and discards any previously queued commands. It does not
	/// clear a host-owned OpenGL framebuffer; the host controls that clear.
	void beginFrame();

	/// End the current frame: submit all recorded drawing to the backend and make
	/// it readable. This is the paired counterpart of `beginFrame()` and is
	/// required before reading pixels or using this Canvas as a texture source.
	///
	/// The offscreen flow is `beginFrame -> draw -> endFrame -> readPixelsRGBA`.
	/// endFrame() submits and consumes the recorded commands. On the normal output
	/// path, Software clears on every submission, Vulkan/Metal clear when a
	/// draw list starts, and OpenGL does not clear implicitly. A render-target
	/// canvas rebuilds its offscreen texture only when commands are queued. Call
	/// endFrame() exactly once per frame, right before reading back or presenting.
	void endFrame();

	/// Clear pending commands and finalize the backend. Equivalent to an orderly
	/// explicit teardown; the Canvas object remains reusable after reinitialization.
	void shutdown();

	/// Read the rendered image back as top-left-origin RGBA bytes.
	/// Read tightly packed, top-left-origin RGBA8 pixels after endFrame(). Returns
	/// false and clears the output when readback is unavailable.
	bool readPixelsRGBA(std::vector<unsigned char> &pixels) const;

	std::vector<unsigned char> readPixelsRGBA() const;

	/// Submit one asynchronous readback where supported. Poll once per later
	/// frame until the callback runs; Software and Metal currently return false.
	bool readPixelsRGBAAsync(ReadPixelsCallback callback);

	bool pollReadPixelsRGBAAsync();

	bool hasPendingReadPixelsRGBAAsync() const;

	/// Save a binary PPM snapshot after endFrame(); returns false on read/I/O failure.
	bool savePixelsPPM(const std::string &path) const;

	/// Stable byte hash for exact regression checks. Hardware/backend AA
	/// differences make it unsuitable as a cross-platform perceptual metric.
	static std::uint64_t hashPixelsRGBA(const std::vector<unsigned char> &pixels);

	std::uint64_t computePixelsHashRGBA() const;

	// Output target — where this canvas delivers rendered frames.
	// See doc/windowed-presentation-design.md.
	/// `OutputTarget` describes the destination of each rendered frame:
	/// - `OutputTarget::Offscreen()` for CPU readback or texture reuse;
	/// - `OutputTarget::ToWindow()` for window presentation;
	/// - `OutputTarget::GLFramebuffer()` / `OutputTarget::VulkanImageTarget()` for
	///   embedding in a host-owned render target.
	///
	/// Set the target once for the Canvas's presentation mode. A window target
	/// usually requires the host to provide a valid native surface and a current
	/// graphics context. Returns false when the target is unsupported for this
	/// backend/platform. Default is `OutputTarget::Offscreen()`.
	bool setOutputTarget(const OutputTarget &target);

	/// Deliver the current frame to the output target. Call after `endFrame()`.
	/// For a Window target this swaps/blits to the window; for off-screen and
	/// wrap-external targets it is a no-op returning true.
	bool present();

	/// Notify a Window output target of a new physical drawable size. Also call
	/// setSize() so Canvas coordinates and backend viewport match.
	void resizeOutput(int width, int height);

	/// Whether the current backend/platform can present to a window.
	bool isPresentable() const;

	// Advanced Vulkan interop. These return the raw handles of the Vulkan
	// backend (as opaque pointers), or null / 0 for non-Vulkan canvases. Useful
	// e.g. to allocate a VulkanImage output target on this canvas's device.
	/// Borrow raw handles owned by this Vulkan Canvas. Values are null/zero for
	/// other backends and become invalid at context finalization/destruction.
	void *vulkanInstance() const;

	void *vulkanPhysicalDevice() const;

	void *vulkanDevice() const;

	void *vulkanQueue() const;

	unsigned int vulkanQueueFamily() const;

	// Advanced Metal interop. Return the raw handles of the Metal backend as
	// opaque pointers (id<MTLDevice>, id<MTLCommandQueue>, id<MTLTexture>), or
	// null for non-Metal canvases. `metalLastRenderedTexture()` is the offscreen
	// MTLTexture the Canvas most recently drew into; consumers can blit it into
	// a CAMetalLayer drawable to present the frame on screen.
	/// Borrow Objective-C Metal objects as opaque pointers. They remain owned by
	/// the Canvas and become invalid at context finalization/destruction.
	void *metalDevice() const;

	void *metalCommandQueue() const;

	void *metalLastRenderedTexture() const;

private:
	friend class CanvasLifecycleTestAccess;
	explicit Canvas(std::unique_ptr<::IRenderer> renderer);

	void drawImageSnapshot(
		std::shared_ptr<const std::vector<unsigned char>> pixels,
		int imageWidth, int imageHeight, bool mipmapsReady,
		const RectF &src, const RectF &dst, const Paint &paint);

	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace wsc
