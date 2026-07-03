#include "text/GlyphAtlas.h"

#include <algorithm>

namespace wsc::text {

namespace {
constexpr int kMaxGlyphAtlasDimension = 4096;
constexpr std::size_t kMaxGlyphAtlasDirtyRects = 64;
}

bool GlyphKey::operator==(const GlyphKey &other) const
{
    return fontFamily == other.fontFamily
        && codepoint == other.codepoint
        && glyphIndex == other.glyphIndex
        && pixelSize == other.pixelSize
        && format == other.format;
}

std::size_t GlyphKeyHasher::operator()(const GlyphKey &key) const
{
    std::size_t seed = std::hash<std::string>{}(key.fontFamily);
    const auto combine = [&seed](std::size_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    };
    combine(std::hash<std::uint32_t>{}(key.codepoint));
    combine(std::hash<int>{}(key.glyphIndex));
    combine(std::hash<float>{}(key.pixelSize));
    combine(std::hash<int>{}(static_cast<int>(key.format)));
    return seed;
}

GlyphAtlas::GlyphAtlas(int width, int height, int padding)
    : width_(std::max(0, width)),
      height_(std::max(0, height)),
      padding_(std::max(0, padding)),
      pixels_(static_cast<std::size_t>(std::max(0, width)) * static_cast<std::size_t>(std::max(0, height)), 0),
      rgbaPixels_(static_cast<std::size_t>(std::max(0, width)) * static_cast<std::size_t>(std::max(0, height)) * 4u, 0)
{
    resetPacking();
}

const GlyphAtlasEntry *GlyphAtlas::find(const GlyphKey &key) const
{
    const auto found = entryIndex_.find(key);
    if (found == entryIndex_.end() || found->second >= entries_.size()) {
        return nullptr;
    }
    return &entries_[found->second];
}

std::optional<GlyphAtlasEntry> GlyphAtlas::uploadGlyph(const GlyphKey &key, const GlyphBitmap &bitmap)
{
    if (const GlyphAtlasEntry *existing = find(key)) {
        return *existing;
    }

    if (!hasValidPixels(bitmap)) {
        return std::nullopt;
    }
    if (!canStoreDimensions(bitmap.width, bitmap.height) && !growToFit(bitmap.width, bitmap.height)) {
        return std::nullopt;
    }

    int x = 0;
    int y = 0;
    if (!allocateRect(bitmap.width, bitmap.height, x, y)) {
        if (!growToFit(bitmap.width, bitmap.height)) {
            rememberRebuildKeys();
            clear();
            ++evictionCount_;
            ++generation_;
        }
        if (!allocateRect(bitmap.width, bitmap.height, x, y)) {
            return std::nullopt;
        }
    }

    GlyphAtlasEntry entry;
    entry.key = key;
    entry.x = x;
    entry.y = y;
    entry.width = bitmap.width;
    entry.height = bitmap.height;
    entry.u0 = width_ > 0 ? static_cast<float>(x) / static_cast<float>(width_) : 0.0f;
    entry.v0 = height_ > 0 ? static_cast<float>(y) / static_cast<float>(height_) : 0.0f;
    entry.u1 = width_ > 0 ? static_cast<float>(x + bitmap.width) / static_cast<float>(width_) : 0.0f;
    entry.v1 = height_ > 0 ? static_cast<float>(y + bitmap.height) / static_cast<float>(height_) : 0.0f;
    entry.bearingX = bitmap.bearingX;
    entry.bearingY = bitmap.bearingY;
    entry.advanceX = bitmap.advanceX;
    entry.generation = generation_;

    entries_.push_back(entry);
    entryIndex_[entry.key] = entries_.size() - 1u;
    writeGlyphPixels(entries_.back(), bitmap);
    ++uploadCount_;
    textureValid_ = true;
    return entry;
}

void GlyphAtlas::clear()
{
    entries_.clear();
    entryIndex_.clear();
    std::fill(pixels_.begin(), pixels_.end(), 0);
    std::fill(rgbaPixels_.begin(), rgbaPixels_.end(), 0);
    hasColorPixels_ = false;
    resetPacking();
    markFullDirty();
}

void GlyphAtlas::onContextLost()
{
    rememberRebuildKeys();
    clear();
    textureValid_ = false;
    ++generation_;
}

void GlyphAtlas::onContextRestored()
{
    textureValid_ = true;
}

GlyphAtlasStats GlyphAtlas::stats() const
{
    GlyphAtlasStats result;
    result.width = width_;
    result.height = height_;
    result.glyphCount = entries_.size();
    result.usedBytes = pixels_.size();
    result.uploadCount = uploadCount_;
    result.evictionCount = evictionCount_;
    result.resizeCount = resizeCount_;
    result.dirtyRectCollapseCount = dirtyRectCollapseCount_;
    result.generation = generation_;
    result.textureValid = textureValid_;
    return result;
}

std::vector<GlyphAtlasDirtyRect> GlyphAtlas::consumeDirtyRects()
{
    std::vector<GlyphAtlasDirtyRect> result = std::move(dirtyRects_);
    dirtyRects_.clear();
    return result;
}

bool GlyphAtlas::hasValidPixels(const GlyphBitmap &bitmap) const
{
    const std::size_t expectedSize = static_cast<std::size_t>(std::max(0, bitmap.width))
        * static_cast<std::size_t>(std::max(0, bitmap.height));
    const bool hasAlphaPixels = bitmap.alphaPixels.size() >= expectedSize;
    const bool hasRgbaPixels = bitmap.rgbaPixels.size() >= expectedSize * 4u;
    return bitmap.width > 0 && bitmap.height > 0
        && ((bitmap.format == GlyphBitmapFormat::Alpha && hasAlphaPixels)
            || (bitmap.format == GlyphBitmapFormat::RGBA && hasRgbaPixels));
}

bool GlyphAtlas::canStoreDimensions(int width, int height) const
{
    return width_ > 0 && height_ > 0
        && width > 0 && height > 0
        && width + padding_ * 2 <= width_
        && height + padding_ * 2 <= height_;
}

bool GlyphAtlas::allocateRect(int width, int height, int &x, int &y)
{
    const int paddedWidth = width + padding_ * 2;
    const int paddedHeight = height + padding_ * 2;
    if (cursorX_ + paddedWidth > width_) {
        cursorX_ = 0;
        cursorY_ += rowHeight_;
        rowHeight_ = 0;
    }

    if (cursorY_ + paddedHeight > height_) {
        return false;
    }

    x = cursorX_ + padding_;
    y = cursorY_ + padding_;
    cursorX_ += paddedWidth;
    rowHeight_ = std::max(rowHeight_, paddedHeight);
    return true;
}

bool GlyphAtlas::growToFit(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    int nextWidth = std::max(1, width_);
    int nextHeight = std::max(1, height_);
    const int requiredWidth = width + padding_ * 2;
    const int requiredHeight = height + padding_ * 2;
    while ((nextWidth < requiredWidth || nextHeight < requiredHeight
            || (nextWidth == width_ && nextHeight == height_))
           && (nextWidth < kMaxGlyphAtlasDimension || nextHeight < kMaxGlyphAtlasDimension)) {
        if (nextWidth <= nextHeight && nextWidth < kMaxGlyphAtlasDimension) {
            nextWidth = std::min(kMaxGlyphAtlasDimension, nextWidth * 2);
        } else if (nextHeight < kMaxGlyphAtlasDimension) {
            nextHeight = std::min(kMaxGlyphAtlasDimension, nextHeight * 2);
        } else if (nextWidth < kMaxGlyphAtlasDimension) {
            nextWidth = std::min(kMaxGlyphAtlasDimension, nextWidth * 2);
        }
    }

    if (nextWidth == width_ && nextHeight == height_) {
        return false;
    }
    if (requiredWidth > nextWidth || requiredHeight > nextHeight) {
        return false;
    }

    rememberRebuildKeys();
    width_ = nextWidth;
    height_ = nextHeight;
    pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0);
    rgbaPixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u, 0);
    entries_.clear();
    entryIndex_.clear();
    hasColorPixels_ = false;
    resetPacking();
    markFullDirty();
    ++resizeCount_;
    ++generation_;
    textureValid_ = true;
    return true;
}

void GlyphAtlas::resetPacking()
{
    cursorX_ = 0;
    cursorY_ = 0;
    rowHeight_ = 0;
}

void GlyphAtlas::rememberRebuildKeys()
{
    pendingRebuildKeys_.clear();
    pendingRebuildKeys_.reserve(entries_.size());
    for (const GlyphAtlasEntry &entry : entries_) {
        pendingRebuildKeys_.push_back(entry.key);
    }
}

void GlyphAtlas::markDirtyRect(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    if (hasFullDirtyRect()) {
        return;
    }

    const int left = std::clamp(x, 0, width_);
    const int top = std::clamp(y, 0, height_);
    const int right = std::clamp(x + width, 0, width_);
    const int bottom = std::clamp(y + height, 0, height_);
    if (right <= left || bottom <= top) {
        return;
    }

    if (dirtyRects_.size() >= kMaxGlyphAtlasDirtyRects) {
        ++dirtyRectCollapseCount_;
        markFullDirty();
        return;
    }

    dirtyRects_.push_back({left, top, right - left, bottom - top});
}

void GlyphAtlas::markFullDirty()
{
    if (width_ <= 0 || height_ <= 0) {
        return;
    }

    dirtyRects_.clear();
    dirtyRects_.push_back({0, 0, width_, height_});
}

bool GlyphAtlas::hasFullDirtyRect() const
{
    return dirtyRects_.size() == 1
        && dirtyRects_[0].x == 0
        && dirtyRects_[0].y == 0
        && dirtyRects_[0].width == width_
        && dirtyRects_[0].height == height_;
}

void GlyphAtlas::writeGlyphPixels(const GlyphAtlasEntry &entry, const GlyphBitmap &bitmap)
{
    if (bitmap.format == GlyphBitmapFormat::RGBA) {
        hasColorPixels_ = true;
    }

    for (int row = 0; row < bitmap.height; ++row) {
        const std::size_t src = static_cast<std::size_t>(row) * static_cast<std::size_t>(bitmap.width);
        const std::size_t dst = static_cast<std::size_t>(entry.y + row) * static_cast<std::size_t>(width_)
            + static_cast<std::size_t>(entry.x);
        if (bitmap.format == GlyphBitmapFormat::RGBA) {
            for (int col = 0; col < bitmap.width; ++col) {
                const std::size_t srcPixel = (src + static_cast<std::size_t>(col)) * 4u;
                const std::size_t dstPixel = dst + static_cast<std::size_t>(col);
                const std::size_t dstRgba = dstPixel * 4u;
                rgbaPixels_[dstRgba + 0] = bitmap.rgbaPixels[srcPixel + 0];
                rgbaPixels_[dstRgba + 1] = bitmap.rgbaPixels[srcPixel + 1];
                rgbaPixels_[dstRgba + 2] = bitmap.rgbaPixels[srcPixel + 2];
                rgbaPixels_[dstRgba + 3] = bitmap.rgbaPixels[srcPixel + 3];
                pixels_[dstPixel] = bitmap.rgbaPixels[srcPixel + 3];
            }
        } else {
            std::copy(bitmap.alphaPixels.begin() + static_cast<std::ptrdiff_t>(src),
                      bitmap.alphaPixels.begin() + static_cast<std::ptrdiff_t>(src + bitmap.width),
                      pixels_.begin() + static_cast<std::ptrdiff_t>(dst));
            for (int col = 0; col < bitmap.width; ++col) {
                const std::size_t dstPixel = dst + static_cast<std::size_t>(col);
                const std::size_t dstRgba = dstPixel * 4u;
                rgbaPixels_[dstRgba + 0] = 255;
                rgbaPixels_[dstRgba + 1] = 255;
                rgbaPixels_[dstRgba + 2] = 255;
                rgbaPixels_[dstRgba + 3] = bitmap.alphaPixels[src + static_cast<std::size_t>(col)];
            }
        }
    }
    markDirtyRect(entry.x, entry.y, entry.width, entry.height);
}

} // namespace wsc::text
