#include "text/GlyphAtlas.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        return false;
    }
    return true;
}

wsc::text::GlyphKey makeKey(std::uint32_t codepoint, float size = 16.0f)
{
    return {"Inter", codepoint, static_cast<int>(codepoint), size, wsc::text::GlyphBitmapFormat::Alpha};
}

wsc::text::GlyphKey makeColorKey(std::uint32_t codepoint, float size = 16.0f)
{
    return {"InterColor", codepoint, static_cast<int>(codepoint), size, wsc::text::GlyphBitmapFormat::RGBA};
}

wsc::text::GlyphBitmap makeBitmap(int width, int height, unsigned char value)
{
    wsc::text::GlyphBitmap bitmap;
    bitmap.width = width;
    bitmap.height = height;
    bitmap.bearingX = 1.0f;
    bitmap.bearingY = 2.0f;
    bitmap.advanceX = static_cast<float>(width + 1);
    bitmap.alphaPixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), value);
    return bitmap;
}

wsc::text::GlyphBitmap makeColorBitmap(int width, int height, unsigned char r, unsigned char g,
                                       unsigned char b, unsigned char a)
{
    wsc::text::GlyphBitmap bitmap;
    bitmap.format = wsc::text::GlyphBitmapFormat::RGBA;
    bitmap.width = width;
    bitmap.height = height;
    bitmap.bearingX = 1.0f;
    bitmap.bearingY = 2.0f;
    bitmap.advanceX = static_cast<float>(width + 1);
    bitmap.rgbaPixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * static_cast<std::size_t>(height); ++i) {
        bitmap.rgbaPixels[i * 4u + 0] = r;
        bitmap.rgbaPixels[i * 4u + 1] = g;
        bitmap.rgbaPixels[i * 4u + 2] = b;
        bitmap.rgbaPixels[i * 4u + 3] = a;
    }
    return bitmap;
}

bool testUploadAndFind()
{
    wsc::text::GlyphAtlas atlas(32, 16, 1);
    const auto entry = atlas.uploadGlyph(makeKey('A'), makeBitmap(4, 6, 180));

    bool ok = expect(entry.has_value(), "valid glyph should upload");
    ok = expect(atlas.find(makeKey('A')) != nullptr, "uploaded glyph should be findable") && ok;
    ok = expect(atlas.find(makeKey('B')) == nullptr, "different glyph should miss") && ok;
    ok = expect(entry->x == 1 && entry->y == 1, "first glyph should respect padding") && ok;
    ok = expect(entry->u0 > 0.0f && entry->u1 > entry->u0, "entry should expose normalized u coordinates") && ok;
    ok = expect(atlas.pixels()[static_cast<std::size_t>(entry->y) * 32u + static_cast<std::size_t>(entry->x)] == 180,
                "atlas pixels should contain uploaded glyph alpha") && ok;
    ok = expect(atlas.stats().glyphCount == 1, "stats should count uploaded glyph") && ok;
    ok = expect(atlas.stats().uploadCount == 1, "stats should count upload") && ok;
    return ok;
}

bool testDuplicateUploadHitsCache()
{
    wsc::text::GlyphAtlas atlas(32, 16, 1);
    const auto first = atlas.uploadGlyph(makeKey('A'), makeBitmap(4, 6, 90));
    const auto dirtyAfterFirst = atlas.consumeDirtyRects();
    const auto second = atlas.uploadGlyph(makeKey('A'), makeBitmap(4, 6, 220));
    const auto dirtyAfterSecond = atlas.consumeDirtyRects();

    bool ok = expect(first.has_value() && second.has_value(), "duplicate glyph uploads should return entries");
    ok = expect(first->x == second->x && first->y == second->y, "duplicate upload should reuse entry") && ok;
    ok = expect(atlas.stats().glyphCount == 1, "duplicate upload should not add glyph") && ok;
    ok = expect(atlas.stats().uploadCount == 1, "duplicate upload should not count as upload") && ok;
    ok = expect(dirtyAfterFirst.size() == 1, "first upload should dirty one glyph rectangle") && ok;
    ok = expect(dirtyAfterFirst[0].x == first->x && dirtyAfterFirst[0].y == first->y,
                "dirty rectangle should match uploaded glyph position") && ok;
    ok = expect(dirtyAfterFirst[0].width == first->width && dirtyAfterFirst[0].height == first->height,
                "dirty rectangle should match uploaded glyph size") && ok;
    ok = expect(dirtyAfterSecond.empty(), "duplicate upload should not dirty atlas pixels") && ok;
    return ok;
}

bool testResizeAndOversizedGlyph()
{
    wsc::text::GlyphAtlas atlas(12, 8, 1);
    bool ok = expect(atlas.uploadGlyph(makeKey('A'), makeBitmap(3, 3, 20)).has_value(), "first glyph should fit");
    ok = expect(atlas.uploadGlyph(makeKey('B'), makeBitmap(3, 3, 40)).has_value(), "second glyph should fit");
    ok = expect(atlas.uploadGlyph(makeKey('C'), makeBitmap(3, 3, 60)).has_value(),
                "third glyph should resize and fit") && ok;
    ok = expect(atlas.stats().resizeCount == 1, "atlas should count resize before eviction") && ok;
    ok = expect(atlas.stats().evictionCount == 0, "atlas should not evict when resize can make room") && ok;
    ok = expect(atlas.pendingRebuildKeys().size() == 2, "resize should remember replaced glyphs") && ok;
    ok = expect(atlas.stats().width > 12 || atlas.stats().height > 8, "resize should grow atlas dimensions") && ok;
    const auto dirtyRects = atlas.consumeDirtyRects();
    ok = expect(!dirtyRects.empty(), "resize should mark atlas dirty") && ok;
    ok = expect(dirtyRects.front().x == 0 && dirtyRects.front().y == 0
                    && dirtyRects.front().width == atlas.stats().width && dirtyRects.front().height == atlas.stats().height,
                "resize should dirty the full atlas because old glyph pixels were cleared") && ok;
    ok = expect(!atlas.uploadGlyph(makeKey('Z'), makeBitmap(5000, 16, 255)).has_value(),
                "oversized glyph should be rejected") && ok;
    return ok;
}

bool testLookupIndexClearsAfterReset()
{
    wsc::text::GlyphAtlas atlas(12, 8, 1);
    bool ok = expect(atlas.uploadGlyph(makeKey('A'), makeBitmap(3, 3, 20)).has_value(), "first indexed glyph should fit");
    ok = expect(atlas.uploadGlyph(makeKey('B'), makeBitmap(3, 3, 40)).has_value(), "second indexed glyph should fit") && ok;
    ok = expect(atlas.find(makeKey('A')) != nullptr, "indexed glyph lookup should hit before reset") && ok;

    ok = expect(atlas.uploadGlyph(makeKey('C'), makeBitmap(3, 3, 60)).has_value(),
                "third glyph should force resize") && ok;
    ok = expect(atlas.find(makeKey('A')) == nullptr,
                "resize should clear lookup index for entries that need rebuild") && ok;
    ok = expect(atlas.find(makeKey('C')) != nullptr,
                "lookup index should include glyph uploaded after resize") && ok;

    atlas.onContextLost();
    ok = expect(atlas.find(makeKey('C')) == nullptr,
                "context loss should clear lookup index for evicted entries") && ok;
    return ok;
}

bool testDirtyRectsCollapseToFullAtlas()
{
    wsc::text::GlyphAtlas atlas(512, 16, 0);
    bool ok = true;
    for (std::uint32_t codepoint = 0; codepoint < 80; ++codepoint) {
        ok = expect(atlas.uploadGlyph(makeKey('A' + codepoint), makeBitmap(2, 2, 80)).has_value(),
                    "many small glyphs should upload without resizing") && ok;
    }

    const auto dirtyRects = atlas.consumeDirtyRects();
    ok = expect(dirtyRects.size() == 1, "many dirty glyph rects should collapse to one full atlas rect") && ok;
    ok = expect(atlas.stats().dirtyRectCollapseCount == 1,
                "dirty rect collapse should be counted in atlas stats") && ok;
    ok = expect(dirtyRects.front().x == 0 && dirtyRects.front().y == 0
                    && dirtyRects.front().width == atlas.stats().width
                    && dirtyRects.front().height == atlas.stats().height,
                "collapsed dirty rect should cover the full atlas") && ok;
    return ok;
}

bool testDirtyRectsCollapseByArea()
{
    wsc::text::GlyphAtlas atlas(128, 64, 0);
    bool ok = true;
    for (std::uint32_t codepoint = 0; codepoint < 3; ++codepoint) {
        ok = expect(atlas.uploadGlyph(makeKey('A' + codepoint), makeBitmap(40, 40, 90)).has_value(),
                    "large glyphs should upload without resizing") && ok;
    }

    const auto dirtyRects = atlas.consumeDirtyRects();
    ok = expect(dirtyRects.size() == 1, "large dirty glyph area should collapse to one full atlas rect") && ok;
    ok = expect(atlas.stats().dirtyRectCollapseCount == 1,
                "area-based dirty rect collapse should be counted in atlas stats") && ok;
    ok = expect(dirtyRects.front().x == 0 && dirtyRects.front().y == 0
                    && dirtyRects.front().width == atlas.stats().width
                    && dirtyRects.front().height == atlas.stats().height,
                "area-collapsed dirty rect should cover the full atlas") && ok;
    return ok;
}

bool testContextLossRebuildHooks()
{
    wsc::text::GlyphAtlas atlas(32, 16, 1);
    const auto first = atlas.uploadGlyph(makeKey('A'), makeBitmap(4, 6, 120));
    const std::uint64_t generation = first->generation;

    atlas.onContextLost();
    bool ok = expect(!atlas.stats().textureValid, "context loss should mark texture invalid");
    ok = expect(atlas.stats().glyphCount == 0, "context loss should clear entries") && ok;
    ok = expect(atlas.pendingRebuildKeys().size() == 1, "context loss should remember glyph rebuild key") && ok;
    ok = expect(atlas.stats().generation == generation + 1, "context loss should advance generation") && ok;

    atlas.onContextRestored();
    ok = expect(atlas.stats().textureValid, "context restore should mark texture valid") && ok;
    const auto rebuilt = atlas.uploadGlyph(makeKey('A'), makeBitmap(4, 6, 130));
    ok = expect(rebuilt.has_value() && rebuilt->generation == generation + 1,
                "rebuilt glyph should use current generation") && ok;
    return ok;
}

bool testColorGlyphUploadKeepsRgbaPixels()
{
    wsc::text::GlyphAtlas atlas(32, 16, 1);
    const auto entry = atlas.uploadGlyph(makeColorKey(0x2605), makeColorBitmap(3, 4, 12, 34, 56, 210));

    bool ok = expect(entry.has_value(), "valid color glyph should upload");
    ok = expect(atlas.hasColorPixels(), "atlas should report color glyph pixels") && ok;
    const std::size_t pixel = static_cast<std::size_t>(entry->y) * 32u + static_cast<std::size_t>(entry->x);
    ok = expect(atlas.pixels()[pixel] == 210, "alpha view should mirror color glyph alpha") && ok;
    ok = expect(atlas.rgbaPixels()[pixel * 4u + 0] == 12, "RGBA view should preserve red") && ok;
    ok = expect(atlas.rgbaPixels()[pixel * 4u + 1] == 34, "RGBA view should preserve green") && ok;
    ok = expect(atlas.rgbaPixels()[pixel * 4u + 2] == 56, "RGBA view should preserve blue") && ok;
    ok = expect(atlas.rgbaPixels()[pixel * 4u + 3] == 210, "RGBA view should preserve alpha") && ok;
    ok = expect(atlas.stats().usedBytes == atlas.pixels().size() + atlas.rgbaPixels().size(),
                "atlas memory stats should include alpha and lazily allocated RGBA storage") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testUploadAndFind() && ok;
    ok = testDuplicateUploadHitsCache() && ok;
    ok = testResizeAndOversizedGlyph() && ok;
    ok = testLookupIndexClearsAfterReset() && ok;
    ok = testDirtyRectsCollapseToFullAtlas() && ok;
    ok = testDirtyRectsCollapseByArea() && ok;
    ok = testContextLossRebuildHooks() && ok;
    ok = testColorGlyphUploadKeepsRgbaPixels() && ok;
    return ok ? 0 : 1;
}
