#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace wsc::text {

enum class GlyphBitmapFormat
{
    Alpha,
    RGBA
};

struct GlyphKey
{
    std::string fontFamily;
    std::uint32_t codepoint = 0;
    int glyphIndex = 0;
    float pixelSize = 0.0f;
    GlyphBitmapFormat format = GlyphBitmapFormat::Alpha;

    bool operator==(const GlyphKey &other) const;
};

struct GlyphKeyHasher
{
    std::size_t operator()(const GlyphKey &key) const;
};

struct GlyphBitmap
{
    GlyphBitmapFormat format = GlyphBitmapFormat::Alpha;
    int width = 0;
    int height = 0;
    float bearingX = 0.0f;
    float bearingY = 0.0f;
    float advanceX = 0.0f;
    std::vector<unsigned char> alphaPixels;
    std::vector<unsigned char> rgbaPixels;
};

struct GlyphAtlasEntry
{
    GlyphKey key;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float bearingX = 0.0f;
    float bearingY = 0.0f;
    float advanceX = 0.0f;
    std::uint64_t generation = 0;
};

struct GlyphAtlasStats
{
    int width = 0;
    int height = 0;
    std::size_t glyphCount = 0;
    std::size_t usedBytes = 0;
    std::size_t uploadCount = 0;
    std::size_t evictionCount = 0;
    std::size_t resizeCount = 0;
    std::uint64_t generation = 0;
    bool textureValid = false;
};

struct GlyphAtlasDirtyRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

class GlyphAtlas
{
public:
    GlyphAtlas(int width, int height, int padding = 1);

    const GlyphAtlasEntry *find(const GlyphKey &key) const;
    std::optional<GlyphAtlasEntry> uploadGlyph(const GlyphKey &key, const GlyphBitmap &bitmap);
    void clear();
    void onContextLost();
    void onContextRestored();

    const std::vector<GlyphKey> &pendingRebuildKeys() const { return pendingRebuildKeys_; }
    const std::vector<GlyphAtlasDirtyRect> &dirtyRects() const { return dirtyRects_; }
    std::vector<GlyphAtlasDirtyRect> consumeDirtyRects();
    const std::vector<unsigned char> &pixels() const { return pixels_; }
    const std::vector<unsigned char> &rgbaPixels() const { return rgbaPixels_; }
    bool hasColorPixels() const { return hasColorPixels_; }
    GlyphAtlasStats stats() const;

private:
    bool hasValidPixels(const GlyphBitmap &bitmap) const;
    bool canStoreDimensions(int width, int height) const;
    bool allocateRect(int width, int height, int &x, int &y);
    bool growToFit(int width, int height);
    void resetPacking();
    void rememberRebuildKeys();
    void markDirtyRect(int x, int y, int width, int height);
    void markFullDirty();
    bool hasFullDirtyRect() const;
    void writeGlyphPixels(const GlyphAtlasEntry &entry, const GlyphBitmap &bitmap);

    int width_ = 0;
    int height_ = 0;
    int padding_ = 1;
    int cursorX_ = 0;
    int cursorY_ = 0;
    int rowHeight_ = 0;
    std::vector<GlyphAtlasEntry> entries_;
    std::unordered_map<GlyphKey, std::size_t, GlyphKeyHasher> entryIndex_;
    std::vector<GlyphKey> pendingRebuildKeys_;
    std::vector<GlyphAtlasDirtyRect> dirtyRects_;
    std::vector<unsigned char> pixels_;
    std::vector<unsigned char> rgbaPixels_;
    std::size_t uploadCount_ = 0;
    std::size_t evictionCount_ = 0;
    std::size_t resizeCount_ = 0;
    std::uint64_t generation_ = 1;
    bool textureValid_ = true;
    bool hasColorPixels_ = false;
};

} // namespace wsc::text
