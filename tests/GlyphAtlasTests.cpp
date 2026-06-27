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
    return {"Inter", codepoint, size};
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
    const auto second = atlas.uploadGlyph(makeKey('A'), makeBitmap(4, 6, 220));

    bool ok = expect(first.has_value() && second.has_value(), "duplicate glyph uploads should return entries");
    ok = expect(first->x == second->x && first->y == second->y, "duplicate upload should reuse entry") && ok;
    ok = expect(atlas.stats().glyphCount == 1, "duplicate upload should not add glyph") && ok;
    ok = expect(atlas.stats().uploadCount == 1, "duplicate upload should not count as upload") && ok;
    return ok;
}

bool testEvictionAndOversizedGlyph()
{
    wsc::text::GlyphAtlas atlas(12, 8, 1);
    bool ok = expect(atlas.uploadGlyph(makeKey('A'), makeBitmap(3, 3, 20)).has_value(), "first glyph should fit");
    ok = expect(atlas.uploadGlyph(makeKey('B'), makeBitmap(3, 3, 40)).has_value(), "second glyph should fit");
    ok = expect(atlas.uploadGlyph(makeKey('C'), makeBitmap(3, 3, 60)).has_value(),
                "third glyph should evict and fit") && ok;
    ok = expect(atlas.stats().evictionCount == 1, "atlas should count eviction") && ok;
    ok = expect(atlas.pendingRebuildKeys().size() == 2, "eviction should remember replaced glyphs") && ok;
    ok = expect(!atlas.uploadGlyph(makeKey('Z'), makeBitmap(16, 16, 255)).has_value(),
                "oversized glyph should be rejected") && ok;
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

} // namespace

int main()
{
    bool ok = true;
    ok = testUploadAndFind() && ok;
    ok = testDuplicateUploadHitsCache() && ok;
    ok = testEvictionAndOversizedGlyph() && ok;
    ok = testContextLossRebuildHooks() && ok;
    return ok ? 0 : 1;
}
