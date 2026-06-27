#include "text/GlyphAtlas.h"

#include <algorithm>

namespace wsc::text {

bool GlyphKey::operator==(const GlyphKey &other) const
{
    return fontFamily == other.fontFamily
        && codepoint == other.codepoint
        && pixelSize == other.pixelSize;
}

GlyphAtlas::GlyphAtlas(int width, int height, int padding)
    : width_(std::max(0, width)),
      height_(std::max(0, height)),
      padding_(std::max(0, padding)),
      pixels_(static_cast<std::size_t>(std::max(0, width)) * static_cast<std::size_t>(std::max(0, height)), 0)
{
    resetPacking();
}

const GlyphAtlasEntry *GlyphAtlas::find(const GlyphKey &key) const
{
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const GlyphAtlasEntry &entry) {
        return entry.key == key;
    });
    return found == entries_.end() ? nullptr : &(*found);
}

std::optional<GlyphAtlasEntry> GlyphAtlas::uploadGlyph(const GlyphKey &key, const GlyphBitmap &bitmap)
{
    if (const GlyphAtlasEntry *existing = find(key)) {
        return *existing;
    }

    if (!canStore(bitmap)) {
        return std::nullopt;
    }

    int x = 0;
    int y = 0;
    if (!allocateRect(bitmap.width, bitmap.height, x, y)) {
        rememberRebuildKeys();
        clear();
        ++evictionCount_;
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

    writeGlyphPixels(entry, bitmap);
    entries_.push_back(entry);
    ++uploadCount_;
    textureValid_ = true;
    return entry;
}

void GlyphAtlas::clear()
{
    entries_.clear();
    std::fill(pixels_.begin(), pixels_.end(), 0);
    resetPacking();
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
    result.generation = generation_;
    result.textureValid = textureValid_;
    return result;
}

bool GlyphAtlas::canStore(const GlyphBitmap &bitmap) const
{
    const std::size_t expectedSize = static_cast<std::size_t>(std::max(0, bitmap.width))
        * static_cast<std::size_t>(std::max(0, bitmap.height));
    return width_ > 0 && height_ > 0
        && bitmap.width > 0 && bitmap.height > 0
        && bitmap.width + padding_ * 2 <= width_
        && bitmap.height + padding_ * 2 <= height_
        && bitmap.alphaPixels.size() >= expectedSize;
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

void GlyphAtlas::writeGlyphPixels(const GlyphAtlasEntry &entry, const GlyphBitmap &bitmap)
{
    for (int row = 0; row < bitmap.height; ++row) {
        const std::size_t src = static_cast<std::size_t>(row) * static_cast<std::size_t>(bitmap.width);
        const std::size_t dst = static_cast<std::size_t>(entry.y + row) * static_cast<std::size_t>(width_)
            + static_cast<std::size_t>(entry.x);
        std::copy(bitmap.alphaPixels.begin() + static_cast<std::ptrdiff_t>(src),
                  bitmap.alphaPixels.begin() + static_cast<std::ptrdiff_t>(src + bitmap.width),
                  pixels_.begin() + static_cast<std::ptrdiff_t>(dst));
    }
}

} // namespace wsc::text
