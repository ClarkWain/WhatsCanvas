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
class Image;
class CanvasLifecycleTestAccess;
class Matrix4;
class Paint;
class Path;

/// Main drawing surface exposed by WhatsCanvas.
class WSC_API Canvas : public ITextureSource
{
public:
	/// Aggregated text metrics returned by measurement helpers.
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

	struct RenderStats
	{
		std::uint64_t flushCpuTimeNs = 0;
		std::uint64_t frameCompileCpuTimeNs = 0;
		std::uint64_t deviceExecutionCpuTimeNs = 0;
		std::uint64_t gpuTimeNs = 0;
		bool gpuTimeAvailable = false;
		std::size_t commandCount = 0;
		std::size_t drawCallCount = 0;
		std::size_t mergedBatchCount = 0;
		std::size_t compiledPacketCount = 0;
		std::size_t compiledVertexBytes = 0;
		std::size_t compiledIndexBytes = 0;
		std::size_t renderTargetSwitches = 0;
		std::size_t filterCount = 0;
		std::size_t filterPassCount = 0;
		std::size_t downsampledFilterCount = 0;
		std::size_t filterInputPixelCount = 0;
		std::size_t filterPixelPassCount = 0;
		std::size_t pathVertexCount = 0;
		std::size_t pathIndexCount = 0;
		std::size_t pathIndexBytes = 0;
		std::size_t pathUploadCount = 0;
		std::size_t pathUploadBytes = 0;
		std::size_t pathTopologyCacheHits = 0;
		std::size_t pathTopologyCacheMisses = 0;
		std::size_t imageTextureCount = 0;
		std::size_t glyphAtlasTextureCount = 0;
		std::size_t glyphAtlasTextureBytes = 0;
		std::size_t renderTargetCount = 0;
		std::size_t pooledRenderTargetCount = 0;
		std::size_t pooledRenderTargetBytes = 0;
		std::size_t renderTargetPoolReuseCount = 0;
		std::size_t renderTargetPoolAllocationCount = 0;
		std::size_t renderTargetPoolEvictionCount = 0;
		std::size_t tessellationCacheHits = 0;
		std::size_t tessellationCacheMisses = 0;
		std::size_t tessellationCacheSize = 0;
		std::size_t tessellationCacheBytes = 0;
		std::size_t aaCacheHits = 0;
		std::size_t aaCacheMisses = 0;
		std::size_t aaCacheSize = 0;
		std::size_t aaCacheBytes = 0;
		std::size_t strokeCacheHits = 0;
		std::size_t strokeCacheMisses = 0;
		std::size_t strokeCacheSize = 0;
		std::size_t strokeCacheBytes = 0;
		std::size_t bitmapTextCacheSize = 0;
		std::size_t bitmapTextCacheBytes = 0;
		std::size_t trackedResourceBytes = 0;
	};

	using ReadPixelsCallback = std::function<void(std::vector<unsigned char> pixels, int width, int height)>;

	enum class ImageFit
	{
		FILL,
		CONTAIN,
		COVER
	};

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

	/// Process-wide initialization/teardown of shared WhatsCanvas resources.
	static void initialize();
	static void finalize();
	using OpenGLProcAddress = void *(*)(const char *name);
	/// Provide the platform's GL function loader before using a GL-backed Canvas.
	static bool loadOpenGL(OpenGLProcAddress loadProcAddress);
	static std::string getOpenGLVersionString();

	/// Enable or disable gamma-correct rendering.
	/// When enabled, all color operations are performed in linear color space,
	/// producing visually correct alpha blending. The GPU framebuffer uses
	/// sRGB format for automatic linear→sRGB conversion on output.
	static void setGammaCorrect(bool enabled);

	/// Check whether gamma-correct rendering is currently enabled.
	static bool isGammaCorrect();

public:
	/// Render backend selection for `create`. `Auto` picks the first available
	/// of Vulkan → OpenGL/OpenGLES → Software.
	enum class Backend
	{
		Auto,
		OpenGL,
		OpenGLES,
		Software,
		Vulkan,
		Metal,
		Direct3D,
	};

	~Canvas();

	Canvas(const Canvas &) = delete;
	Canvas &operator=(const Canvas &) = delete;

	/// Create a canvas on `backend`, sized to width×height. Returns nullptr when
	/// the backend is unavailable in this build/host. The canvas is sized but
	/// not initialized. Call `beginFrame()` (which initializes lazily) or
	/// `initializeContext()` before drawing. For the OpenGL backends, make your
	/// GL context current and call `loadOpenGL()` before initialization or the
	/// first frame; software/Vulkan need no external context.
	static std::unique_ptr<Canvas> create(Backend backend, int width, int height);

	/// Create a canvas on the first available backend from `preferred`, or
	/// nullptr if none are available.
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

	RenderStats getRenderStats() const;
	/// Enable optional backend GPU frame timers. Disabled by default so normal
	/// rendering and comparative benchmarks pay no query overhead.
	void setGpuTimingEnabled(bool enabled);
	bool initializeContext();
	void finalizeContext();
	bool isContextInitialized() const;
	void releaseResources();

protected:
	void *getTextureHandleOpaque() const override;

public:
	// Canvas lifetime and state.
	/// Resize the drawing surface. Must be called before initializeContext for
	/// GL-backed canvases; software/Vulkan canvases are pre-sized by their factory.
	void setSize(int width, int height);
	int getWidth() const;
	int getHeight() const;
	/// Convenience current color used by the parameterless draw* helpers.
	void setColor(Color color);
	void setColor(float r, float g, float b, float a = 1.0f);
	Color getColor() const;
	void setBlendMode(Paint::BlendMode blendMode);
	Paint::BlendMode getBlendMode() const;
	/// Fill the entire canvas with a color (ignores clip when clearing).
	void drawColor(const Color &color);
	void drawColor(float r, float g, float b, float a = 1.0f);
	void drawPaint(const Paint &paint);

	// Primitive drawing.
	void drawPoint(int x, int y, const Paint &paint);
	void drawPoint(float x, float y, const Paint &paint);
	void drawPoint(const Point &point, const Paint &paint);
	void drawPoint(const PointF &point, const Paint &paint);
	void drawPoints(const std::vector<Point> &points, const Paint &paint);
	void drawPoints(const std::vector<PointF> &points, const Paint &paint);

	void drawLine(int x1, int y1, int x2, int y2, const Paint &paint);
	void drawLine(float x1, float y1, float x2, float y2, const Paint &paint);
	void drawLine(const Point &start, const Point &end, const Paint &paint);
	void drawLine(const PointF &start, const PointF &end, const Paint &paint);
	void drawLines(const std::vector<Point> &points, const Paint &paint);
	void drawLines(const std::vector<PointF> &points, const Paint &paint);
	void drawPolyline(const std::vector<Point> &points, const Paint &paint);
	void drawPolyline(const std::vector<PointF> &points, const Paint &paint);
	void drawPolygon(const std::vector<Point> &points, const Paint &paint);
	void drawPolygon(const std::vector<PointF> &points, const Paint &paint);
	void drawRect(const RectF &rect, const Paint &paint);
	void drawRect(const Rect &rect, const Paint &paint);
	void drawRoundRect(const RectF &rect, float radius, const Paint &paint);
	void drawRoundRect(const Rect &rect, float radius, const Paint &paint);
	void drawRoundRect(const RectF &rect, float topLeftRadius, float topRightRadius,
					   float bottomRightRadius, float bottomLeftRadius, const Paint &paint);
	void drawRoundRect(const Rect &rect, float topLeftRadius, float topRightRadius,
					   float bottomRightRadius, float bottomLeftRadius, const Paint &paint);
	void drawBoxShadow(const RectF &rect, float radius, float spread, float blurRadius,
	                   float dx, float dy, const Color &color);
	void drawBoxShadow(const RectF &rect, float topLeftRadius, float topRightRadius,
	                   float bottomRightRadius, float bottomLeftRadius, float spread,
	                   float blurRadius, float dx, float dy, const Color &color);
	void drawCircle(float centerX, float centerY, float radius, const Paint &paint);
	void drawCircle(const PointF &center, float radius, const Paint &paint);
	void drawCircle(const Point &center, float radius, const Paint &paint);
	void drawOval(const RectF &bounds, const Paint &paint);
	void drawOval(const Rect &bounds, const Paint &paint);
	void drawArc(const RectF &bounds, float startRadians, float sweepRadians, bool useCenter, const Paint &paint);
	void drawArc(const Rect &bounds, float startRadians, float sweepRadians, bool useCenter, const Paint &paint);
	void drawArc(const RectF &bounds, float startRadians, float sweepRadians, ArcMode mode, const Paint &paint);
	void drawArc(const Rect &bounds, float startRadians, float sweepRadians, ArcMode mode, const Paint &paint);
	void drawPath(const Path &path, const Paint &paint);
	RectF measureStrokeBounds(const Path &path, const Paint &paint) const;

	// Image drawing.
	void drawImage(const Image &image, float x, float y, const Paint &paint);
	void drawImage(const Image &image, const RectF &dst, const Paint &paint);
	void drawImage(const Image &image, const RectF &src, const RectF &dst, const Paint &paint);
	void drawImageFit(const Image &image, const RectF &dst, ImageFit fit, const Paint &paint);
	void drawImageFit(const Image &image, const RectF &dst, ImageFit fit, ImageAnchor anchor, const Paint &paint);
	void drawImageFit(const Image &image, const RectF &dst, ImageFit fit, float alignX, float alignY, const Paint &paint);
	void drawImageNinePatch(const Image &image, const RectF &centerSrc, const RectF &dst, const Paint &paint);
	void drawImageRounded(const Image &image, const RectF &dst, float radius, const Paint &paint);
	void drawImageRounded(const Image &image, const RectF &dst, float topLeftRadius, float topRightRadius,
	                      float bottomRightRadius, float bottomLeftRadius, const Paint &paint);
	void drawImageCircle(const Image &image, const PointF &center, float radius, const Paint &paint);
	void drawImageTiled(const Image &image, const RectF &dst, const Paint &paint);
	void drawImageTiled(const Image &image, const RectF &dst, float tileWidth, float tileHeight, const Paint &paint);

	// Texture-source drawing (supports Canvas-as-texture when render-target mode is active).
	/// NOTE: The source Canvas must have been ended (`endFrame()`) before
	/// passing it to drawImage. Drawing an unfinished Canvas is a silent no-op.
	void drawImage(const ITextureSource &source, float x, float y, const Paint &paint);
	/// NOTE: The source Canvas must have been ended (`endFrame()`) before
	/// passing it to drawImage. Drawing an unfinished Canvas is a silent no-op.
	void drawImage(const ITextureSource &source, const RectF &dst, const Paint &paint);
	bool loadImage(Image &image, const char *imagePath);
	bool loadImageFromEncodedMemory(Image &image, const unsigned char *data, int size, bool generateMipmaps = true);
	bool loadImageFromRGBA(Image &image, const unsigned char *pixels, int width, int height,
	                       bool generateMipmaps = false);
	bool loadImageFromRGBA(Image &image, const std::vector<unsigned char> &pixels, int width, int height,
	                       bool generateMipmaps = false);
	bool replaceImageRGBA(Image &image, const unsigned char *pixels, int width, int height,
	                      bool generateMipmaps = false);
	bool replaceImageRGBA(Image &image, const std::vector<unsigned char> &pixels, int width, int height,
	                      bool generateMipmaps = false);
	bool updateImageRGBA(Image &image, const unsigned char *pixels, int x, int y, int width, int height,
	                     bool regenerateMipmaps = true);
	bool updateImageRGBA(Image &image, const std::vector<unsigned char> &pixels, int x, int y, int width,
	                     int height, bool regenerateMipmaps = true);
	bool wrapExternalTexture(Image &image, std::uint32_t textureId, int width, int height,
	                         bool mipmapsGenerated = false);

	// Text drawing and measurement.
	/// Draw a single line of text with its baseline anchored per the paint.
	void drawText(const std::string &text, float x, float y, const Paint &paint);
	/// Draw wrapped text inside a rectangle, optionally limiting lines and
	/// ellipsizing overflow.
	void drawTextBox(const std::string &text, const RectF &bounds, const Paint &paint);
	void drawTextBox(const std::string &text, const RectF &bounds, float lineHeight, const Paint &paint);
	void drawTextBox(const std::string &text, const RectF &bounds, float lineHeight, int maxLines, bool ellipsize, const Paint &paint);
	std::vector<TextLine> layoutTextBox(const std::string &text, const RectF &bounds, const Paint &paint) const;
	std::vector<TextLine> layoutTextBox(const std::string &text, const RectF &bounds, float lineHeight, const Paint &paint) const;
	std::vector<TextLine> layoutTextBox(const std::string &text, const RectF &bounds, float lineHeight,
	                                    int maxLines, bool ellipsize, const Paint &paint) const;
	void drawTextOnPath(const std::string &text, const Path &path, const Paint &paint);
	void drawTextOnPath(const std::string &text, const Path &path, float hOffset, float vOffset, const Paint &paint);
	float measureText(const std::string &text, const Paint &paint) const;
	RectF measureTextBounds(const std::string &text, const Paint &paint) const;
	TextMetrics measureTextMetrics(const std::string &text, const Paint &paint) const;
	/// Register a font face so its family can be selected via Paint::setFontFamily.
	bool registerFontFace(const FontFace &face);
	/// Set the fallback chain used to resolve glyphs missing from the primary font.
	bool setFontFallbackChain(const FontFallbackChain &chain);

	/// Text backend selection. `Auto`/`Portable` use the portable FreeType/stb
	/// glyph-atlas backend; `DirectWrite` uses the native Windows backend when
	/// available (falls back to portable otherwise).
	enum class TextBackend
	{
		Auto,
		Portable,
		DirectWrite,
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
	/// Push the current matrix/clip state; returns the new save count.
	int save();
	/// Begin an offscreen layer composited back with the given paint on restore.
	int saveLayer(const RectF &bounds, const Paint &paint);
	int saveLayer(const Rect &bounds, const Paint &paint);
	/// Begin an offscreen layer with optional filters for its content and backdrop.
	///
	/// A backdrop filter sees only commands recorded before this call. The
	/// filtered backdrop and subsequently drawn layer content are composited
	/// together when restore() closes the layer.
	int saveLayer(const RectF &bounds, const Paint &paint, const LayerOptions &options);
	int saveLayer(const Rect &bounds, const Paint &paint, const LayerOptions &options);
	/// Pop the most recent saved state.
	void restore();
	int getSaveCount() const;
	void restoreToCount(int saveCount);

	// Transform and hit-test helpers.
	/// Current total transform matrix (local space to device space).
	Matrix4 getMatrix() const;
	PointF mapPoint(const PointF &point) const;
	RectF mapRect(const RectF &rect) const;
	RectF mapRect(const Rect &rect) const;
	bool inverseMapPoint(const PointF &devicePoint, PointF &localPoint) const;
	bool inverseMapRect(const RectF &deviceRect, RectF &localRect) const;
	bool isPointInClip(const PointF &devicePoint) const;
	bool hitTestPathFill(const Path &path, const PointF &devicePoint) const;
	bool hitTestPathStroke(const Path &path, const PointF &devicePoint, float strokeWidth) const;
	bool hasClip() const;
	bool getClipBounds(RectF &bounds) const;
	bool quickReject(const RectF &rect) const;
	bool quickReject(const Rect &rect) const;
	bool quickReject(const Path &path, const Paint &paint) const;
	void clipPath(const Path &path);
	/// Intersect the current clip with a rectangle.
	void clipRect(const RectF &rect);
	void clipRect(const Rect &rect);
	void setMatrix(const Matrix4 &matrix);
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
	/// Post-multiply the current matrix by another transform.
	void concat(const Matrix4 &matrix);
	/// Translate/scale/rotate the current transform.
	void translate(float dx, float dy);
	void scale(float sx, float sy);
	void rotate(float radians);

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
	/// path, Software clears on every submission, Vulkan clears when a non-empty
	/// draw list starts, and OpenGL does not clear implicitly. A render-target
	/// canvas rebuilds its offscreen texture only when commands are queued. Call
	/// endFrame() exactly once per frame, right before reading back or presenting.
	void endFrame();
	void shutdown();
	/// Read the rendered image back as top-left-origin RGBA bytes.
	bool readPixelsRGBA(std::vector<unsigned char> &pixels) const;
	std::vector<unsigned char> readPixelsRGBA() const;
	bool readPixelsRGBAAsync(ReadPixelsCallback callback);
	bool pollReadPixelsRGBAAsync();
	bool hasPendingReadPixelsRGBAAsync() const;
	bool savePixelsPPM(const std::string &path) const;
	static std::uint64_t hashPixelsRGBA(const std::vector<unsigned char> &pixels);
	std::uint64_t computePixelsHashRGBA() const;
	// Output target — where this canvas delivers rendered frames.
	// See doc/windowed-presentation-design.md.
	/// Set where frames go: off-screen, an on-screen window, or a host-owned
	/// GL/Vulkan render target. Returns false when the target is unsupported for
	/// this backend/platform. Default is `OutputTarget::Offscreen()`.
	bool setOutputTarget(const OutputTarget &target);
	/// Deliver the current frame to the output target. Call after `endFrame()`.
	/// For a Window target this swaps/blits to the window; for off-screen and
	/// wrap-external targets it is a no-op returning true.
	bool present();
	/// Notify a Window output target of a new drawable size (window resize).
	void resizeOutput(int width, int height);
	/// Whether the current backend/platform can present to a window.
	bool isPresentable() const;

	// Advanced Vulkan interop. These return the raw handles of the Vulkan
	// backend (as opaque pointers), or null / 0 for non-Vulkan canvases. Useful
	// e.g. to allocate a VulkanImage output target on this canvas's device.
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
	void *metalDevice() const;
	void *metalCommandQueue() const;
	void *metalLastRenderedTexture() const;

private:
	friend class CanvasLifecycleTestAccess;
	explicit Canvas(std::unique_ptr<::IRenderer> renderer);

	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace wsc
