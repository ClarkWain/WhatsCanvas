#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Color.h"
#include "Export.h"
#include "TextureSource.h"
#include "base.h"

class IRenderer;

namespace wsc {
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
		std::size_t commandCount = 0;
		std::size_t drawCallCount = 0;
		std::size_t mergedBatchCount = 0;
		std::size_t renderTargetSwitches = 0;
		std::size_t imageTextureCount = 0;
		std::size_t glyphAtlasTextureCount = 0;
		std::size_t renderTargetCount = 0;
	};

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

	static void initialize();
	static void finalize();
	using OpenGLProcAddress = void *(*)(const char *name);
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
	Canvas();
	~Canvas();

	Canvas(const Canvas &) = delete;
	Canvas &operator=(const Canvas &) = delete;

	// ITextureSource interface
	int getTextureWidth() const override { return getWidth(); }
	int getTextureHeight() const override { return getHeight(); }
	bool isTextureValid() const override;
	bool isRenderTarget() const override;

	/// Enable or disable render-target mode. When enabled, all drawing is
	/// redirected to an offscreen FBO. The result can then be used as a
	/// texture via ITextureSource (e.g. passed to drawImage).
	void setRenderTargetMode(bool enabled);

	/// Check whether render-target mode is currently active.
	bool isRenderTargetMode() const;
	RenderStats getRenderStats() const;
	bool initializeContext();
	void finalizeContext();
	bool isContextInitialized() const;
	void releaseResources();

protected:
	void *getTextureHandleOpaque() const override;

public:
	// Canvas lifetime and state.
	void setSize(int width, int height);
	int getWidth() const;
	int getHeight() const;
	void setColor(Color color);
	void setColor(float r, float g, float b, float a = 1.0f);
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
	/// NOTE: The source Canvas must have been flushed (or endFrame'd) before
	/// passing it to drawImage. Drawing an unflushed Canvas is a silent no-op.
	void drawImage(const ITextureSource &source, float x, float y, const Paint &paint);
	/// NOTE: The source Canvas must have been flushed (or endFrame'd) before
	/// passing it to drawImage. Drawing an unflushed Canvas is a silent no-op.
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
	void drawText(const std::string &text, float x, float y, const Paint &paint);
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

	// Save stack and offscreen layering.
	int save();
	int saveLayer(const RectF &bounds, const Paint &paint);
	int saveLayer(const Rect &bounds, const Paint &paint);
	void restore();
	int getSaveCount() const;
	void restoreToCount(int saveCount);

	// Transform and hit-test helpers.
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
	void clipRect(const RectF &rect);
	void clipRect(const Rect &rect);
	void setMatrix(const Matrix4 &matrix);
	void resetMatrix();
	void concat(const Matrix4 &matrix);
	void translate(float dx, float dy);
	void scale(float sx, float sy);
	void rotate(float radians);

	// Frame and pixel readback helpers.
	void beginFrame();
	void flush();
	void endFrame();
	void shutdown();
	bool readPixelsRGBA(std::vector<unsigned char> &pixels) const;
	std::vector<unsigned char> readPixelsRGBA() const;
	bool savePixelsPPM(const std::string &path) const;
	static std::uint64_t hashPixelsRGBA(const std::vector<unsigned char> &pixels);
	std::uint64_t computePixelsHashRGBA() const;

private:
	friend class CanvasLifecycleTestAccess;
	explicit Canvas(std::unique_ptr<::IRenderer> renderer);

	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace wsc
