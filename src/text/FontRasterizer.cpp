#include "text/FontRasterizer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "stb_image.h"

#if defined(WHATSCANVAS_HAS_FREETYPE)
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_COLOR_H
#include FT_GLYPH_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H
#endif

#include "../../include/wsc/Font.h"

namespace {

constexpr std::size_t kDefaultLoadedFaceCacheCapacity = 64;

std::uint32_t readU32BE(const unsigned char *data)
{
    return (static_cast<std::uint32_t>(data[0]) << 24u)
        | (static_cast<std::uint32_t>(data[1]) << 16u)
        | (static_cast<std::uint32_t>(data[2]) << 8u)
        | static_cast<std::uint32_t>(data[3]);
}

std::uint16_t readU16BE(const unsigned char *data)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8u)
                                      | static_cast<std::uint16_t>(data[1]));
}

bool tagEquals(const unsigned char *data, const char tag[4])
{
    return data[0] == static_cast<unsigned char>(tag[0])
        && data[1] == static_cast<unsigned char>(tag[1])
        && data[2] == static_cast<unsigned char>(tag[2])
        && data[3] == static_cast<unsigned char>(tag[3]);
}

std::string makeFaceKey(const wsc::FontFace &face)
{
    const std::string indexSuffix = "#" + std::to_string(face.faceIndex())
        + wsc::text::fontVariationIdentity(face.variationCoordinates());
    if (face.sourceType() == wsc::FontSourceType::FILE) {
        return std::string("file:") + face.path() + indexSuffix;
    }
    const std::string source = face.sourceId().empty()
        ? face.family() : face.sourceId();
    return std::string("memory:") + source + ":"
        + std::to_string(reinterpret_cast<std::uintptr_t>(face.bytes()))
        + indexSuffix;
}

std::string makeVariationIdentity(
    const std::vector<wsc::FontVariationCoordinate> &coordinates)
{
    std::vector<std::pair<std::string, std::uint32_t>> variations;
    variations.reserve(coordinates.size());
    for (const wsc::FontVariationCoordinate &coordinate :
         coordinates) {
        std::uint32_t valueBits = 0;
        static_assert(sizeof(valueBits) == sizeof(coordinate.value));
        std::memcpy(&valueBits, &coordinate.value, sizeof(valueBits));
        variations.emplace_back(coordinate.tag, valueBits);
    }
    std::sort(variations.begin(), variations.end());
    std::string result;
    result.reserve(variations.size() * 18u);
    for (const auto &[tag, valueBits] : variations) {
        result += ":" + tag + "=" + std::to_string(valueBits);
    }
    return result;
}

std::vector<unsigned char> readFileBytes(const std::string &path)
{
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
    if (!input) {
        return {};
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0) {
        return {};
    }

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char *>(bytes.data()), size);
    return input ? bytes : std::vector<unsigned char>();
}

struct TableView
{
    const unsigned char *data = nullptr;
    std::size_t size = 0;
};

struct ColorLayer
{
    int glyphIndex = 0;
    std::uint16_t paletteIndex = 0;
};

struct RgbaColor
{
    unsigned char r = 255;
    unsigned char g = 255;
    unsigned char b = 255;
    unsigned char a = 255;
};

std::optional<TableView> findSfntTable(const std::vector<unsigned char> &bytes,
                                       std::size_t fontOffset,
                                       const char tag[4])
{
    if (fontOffset > bytes.size() || bytes.size() - fontOffset < 12u) {
        return std::nullopt;
    }

    const unsigned char *sfnt = bytes.data() + fontOffset;
    const std::size_t sfntSize = bytes.size() - fontOffset;
    const std::uint16_t tableCount = readU16BE(sfnt + 4u);
    if (tableCount == 0u || tableCount > (sfntSize - 12u) / 16u) {
        return std::nullopt;
    }

    for (std::uint16_t i = 0; i < tableCount; ++i) {
        const unsigned char *record = sfnt + 12u + static_cast<std::size_t>(i) * 16u;
        if (!tagEquals(record, tag)) {
            continue;
        }

        const std::uint32_t offset = readU32BE(record + 8u);
        const std::uint32_t length = readU32BE(record + 12u);
        if (offset > bytes.size() || length > bytes.size() - offset) {
            return std::nullopt;
        }
        return TableView{bytes.data() + offset, static_cast<std::size_t>(length)};
    }

    return std::nullopt;
}

std::optional<std::vector<ColorLayer>> findColrLayers(TableView colr, int glyphIndex)
{
    if (colr.data == nullptr || colr.size < 14u || glyphIndex <= 0 || glyphIndex > 0xffff
        || readU16BE(colr.data) != 0u) {
        return std::nullopt;
    }

    const std::uint16_t baseGlyphCount = readU16BE(colr.data + 2u);
    const std::uint32_t baseGlyphOffset = readU32BE(colr.data + 4u);
    const std::uint32_t layerOffset = readU32BE(colr.data + 8u);
    const std::uint16_t layerCount = readU16BE(colr.data + 12u);
    if (baseGlyphOffset > colr.size || layerOffset > colr.size
        || static_cast<std::size_t>(baseGlyphCount) * 6u > colr.size - baseGlyphOffset
        || static_cast<std::size_t>(layerCount) * 4u > colr.size - layerOffset) {
        return std::nullopt;
    }

    for (std::uint16_t i = 0; i < baseGlyphCount; ++i) {
        const unsigned char *base = colr.data + baseGlyphOffset + static_cast<std::size_t>(i) * 6u;
        if (readU16BE(base) != static_cast<std::uint16_t>(glyphIndex)) {
            continue;
        }

        const std::uint16_t firstLayer = readU16BE(base + 2u);
        const std::uint16_t glyphLayerCount = readU16BE(base + 4u);
        if (firstLayer > layerCount || glyphLayerCount > layerCount - firstLayer) {
            return std::nullopt;
        }

        std::vector<ColorLayer> layers;
        layers.reserve(glyphLayerCount);
        for (std::uint16_t layer = 0; layer < glyphLayerCount; ++layer) {
            const unsigned char *record = colr.data + layerOffset
                + static_cast<std::size_t>(firstLayer + layer) * 4u;
            layers.push_back({static_cast<int>(readU16BE(record)), readU16BE(record + 2u)});
        }
        return layers.empty() ? std::nullopt : std::optional<std::vector<ColorLayer>>(std::move(layers));
    }

    return std::nullopt;
}

std::optional<RgbaColor> cpalColor(TableView cpal, std::uint16_t paletteIndex)
{
    if (paletteIndex == 0xffffu) {
        return RgbaColor{};
    }
    if (cpal.data == nullptr || cpal.size < 12u) {
        return std::nullopt;
    }

    const std::uint16_t version = readU16BE(cpal.data);
    const std::uint16_t paletteEntryCount = readU16BE(cpal.data + 2u);
    const std::uint16_t paletteCount = readU16BE(cpal.data + 4u);
    const std::uint16_t colorRecordCount = readU16BE(cpal.data + 6u);
    const std::uint32_t colorRecordOffset = readU32BE(cpal.data + 8u);
    if (version > 1u || paletteCount == 0u || paletteIndex >= paletteEntryCount
        || static_cast<std::size_t>(paletteCount) * 2u > cpal.size - 12u
        || colorRecordOffset > cpal.size
        || static_cast<std::size_t>(colorRecordCount) * 4u > cpal.size - colorRecordOffset) {
        return std::nullopt;
    }

    const std::uint16_t firstPaletteColor = readU16BE(cpal.data + 12u);
    const std::uint32_t colorIndex = static_cast<std::uint32_t>(firstPaletteColor) + paletteIndex;
    if (colorIndex >= colorRecordCount) {
        return std::nullopt;
    }

    const unsigned char *color = cpal.data + colorRecordOffset + static_cast<std::size_t>(colorIndex) * 4u;
    return RgbaColor{color[2], color[1], color[0], color[3]};
}

void compositePixel(unsigned char *dst, RgbaColor color, unsigned char coverage)
{
    const int srcA = static_cast<int>(color.a) * static_cast<int>(coverage) / 255;
    if (srcA <= 0) {
        return;
    }

    const int dstA = dst[3];
    const int outA = srcA + dstA * (255 - srcA) / 255;
    if (outA <= 0) {
        dst[0] = 0;
        dst[1] = 0;
        dst[2] = 0;
        dst[3] = 0;
        return;
    }

    dst[0] = static_cast<unsigned char>((static_cast<int>(color.r) * srcA
        + static_cast<int>(dst[0]) * dstA * (255 - srcA) / 255) / outA);
    dst[1] = static_cast<unsigned char>((static_cast<int>(color.g) * srcA
        + static_cast<int>(dst[1]) * dstA * (255 - srcA) / 255) / outA);
    dst[2] = static_cast<unsigned char>((static_cast<int>(color.b) * srcA
        + static_cast<int>(dst[2]) * dstA * (255 - srcA) / 255) / outA);
    dst[3] = static_cast<unsigned char>(outA);
}

#if defined(WHATSCANVAS_HAS_FREETYPE)
struct FreeTypeLibrary
{
    FT_Library library = nullptr;

    FreeTypeLibrary()
    {
        if (FT_Init_FreeType(&library) != 0) {
            library = nullptr;
        }
    }

    ~FreeTypeLibrary()
    {
        if (library != nullptr) {
            FT_Done_FreeType(library);
        }
    }

    bool valid() const
    {
        return library != nullptr;
    }
};

FreeTypeLibrary &freeTypeLibrary()
{
    static FreeTypeLibrary *library = new FreeTypeLibrary();
    return *library;
}

void applyVariationCoordinates(
    FT_Face ftFace,
    const std::vector<wsc::FontVariationCoordinate> &coordinates)
{
    if (ftFace == nullptr || coordinates.empty()) return;
    FT_MM_Var *metadata = nullptr;
    if (FT_Get_MM_Var(ftFace, &metadata) != 0 || metadata == nullptr) return;

    std::vector<FT_Fixed> values(metadata->num_axis);
    for (FT_UInt axisIndex = 0; axisIndex < metadata->num_axis; ++axisIndex) {
        const FT_Var_Axis &axis = metadata->axis[axisIndex];
        values[axisIndex] = axis.def;
        for (const wsc::FontVariationCoordinate &coordinate : coordinates) {
            const FT_ULong tag = FT_MAKE_TAG(
                coordinate.tag[0], coordinate.tag[1],
                coordinate.tag[2], coordinate.tag[3]);
            if (axis.tag != tag) continue;
            const double fixed = static_cast<double>(coordinate.value) * 65536.0;
            const double clamped = std::clamp(
                fixed, static_cast<double>(axis.minimum),
                static_cast<double>(axis.maximum));
            values[axisIndex] = static_cast<FT_Fixed>(std::llround(clamped));
            break;
        }
    }
    (void)FT_Set_Var_Design_Coordinates(
        ftFace, metadata->num_axis, values.data());
    FT_Done_MM_Var(freeTypeLibrary().library, metadata);
}

bool setFreeTypePixelSize(FT_Face face, float pixelSize)
{
    if (face == nullptr || pixelSize <= 0.0f) {
        return false;
    }
    const auto roundedSize = static_cast<FT_UInt>(std::max(1.0f, std::round(pixelSize)));
    // Shaping, metrics, kerning and rasterization repeatedly touch every glyph
    // in a run. FT_Set_Pixel_Sizes rebuilds size-dependent face state even
    // when the requested size is unchanged, so avoid paying that cost for
    // every glyph in a same-size label.
    if (face->size != nullptr
        && face->size->metrics.y_ppem == roundedSize) {
        return true;
    }
    if (FT_Set_Pixel_Sizes(face, 0, roundedSize) == 0) return true;
    if (face->num_fixed_sizes <= 0 || face->available_sizes == nullptr) return false;

    int bestIndex = 0;
    long bestDistance = std::numeric_limits<long>::max();
    for (int index = 0; index < face->num_fixed_sizes; ++index) {
        const FT_Bitmap_Size &strike = face->available_sizes[index];
        const long strikePixels = strike.y_ppem > 0
            ? static_cast<long>(strike.y_ppem / 64)
            : static_cast<long>(strike.height);
        const long distance = std::abs(
            strikePixels - static_cast<long>(roundedSize));
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = index;
        }
    }
    return FT_Select_Size(face, bestIndex) == 0;
}

const unsigned char *freeTypeBitmapRow(const FT_Bitmap &bitmap, int row)
{
    if (bitmap.buffer == nullptr || row < 0
        || row >= static_cast<int>(bitmap.rows)) {
        return nullptr;
    }
    const std::ptrdiff_t pitch = static_cast<std::ptrdiff_t>(bitmap.pitch);
    const unsigned char *top = bitmap.buffer;
    if (pitch < 0) top -= pitch * static_cast<std::ptrdiff_t>(bitmap.rows - 1);
    return top + pitch * static_cast<std::ptrdiff_t>(row);
}

struct ColrAffine
{
    double xx = 1.0;
    double xy = 0.0;
    double yx = 0.0;
    double yy = 1.0;
    double dx = 0.0;
    double dy = 0.0;
};

ColrAffine multiplyAffine(const ColrAffine &outer, const ColrAffine &inner)
{
    return {
        outer.xx * inner.xx + outer.xy * inner.yx,
        outer.xx * inner.xy + outer.xy * inner.yy,
        outer.yx * inner.xx + outer.yy * inner.yx,
        outer.yx * inner.xy + outer.yy * inner.yy,
        outer.xx * inner.dx + outer.xy * inner.dy + outer.dx,
        outer.yx * inner.dx + outer.yy * inner.dy + outer.dy
    };
}

double fixed16(FT_Fixed value)
{
    return static_cast<double>(value) / 65536.0;
}

struct ColrStop
{
    double offset = 0.0;
    RgbaColor color;
};

enum class ColrFillKind { Solid, Linear, Radial };

struct ColrFill
{
    ColrFillKind kind = ColrFillKind::Solid;
    RgbaColor solid;
    std::vector<ColrStop> stops;
    FT_PaintExtend extend = FT_COLR_PAINT_EXTEND_PAD;
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 1.0;
    double y1 = 0.0;
    double r0 = 0.0;
    double r1 = 1.0;
};

struct ColrV1Context
{
    FT_Face face = nullptr;
    FT_Color *palette = nullptr;
    FT_UShort paletteEntries = 0;
    double scale = 1.0;
    double xMin = 0.0;
    double yMax = 0.0;
    int width = 0;
    int height = 0;
};

RgbaColor colrPaletteColor(const ColrV1Context &context,
                           const FT_ColorIndex &index)
{
    RgbaColor result;
    if (index.palette_index != 0xffffu && context.palette != nullptr
        && index.palette_index < context.paletteEntries) {
        const FT_Color &color = context.palette[index.palette_index];
        result = {color.red, color.green, color.blue, color.alpha};
    }
    const double alpha = std::clamp(
        static_cast<double>(index.alpha) / 16384.0, 0.0, 1.0);
    result.a = static_cast<unsigned char>(std::clamp(
        std::lround(static_cast<double>(result.a) * alpha), 0l, 255l));
    return result;
}

std::vector<ColrStop> readColrStops(const ColrV1Context &context,
                                    const FT_ColorLine &line)
{
    std::vector<ColrStop> result;
    FT_ColorStopIterator iterator = line.color_stop_iterator;
    result.reserve(iterator.num_color_stops);
    FT_ColorStop stop = {};
    while (FT_Get_Colorline_Stops(context.face, &stop, &iterator)) {
        result.push_back({fixed16(stop.stop_offset),
                          colrPaletteColor(context, stop.color)});
    }
    std::sort(result.begin(), result.end(), [](const ColrStop &left,
                                               const ColrStop &right) {
        return left.offset < right.offset;
    });
    return result;
}

std::optional<ColrFill> readColrFill(const ColrV1Context &context,
                                     FT_OpaquePaint opaque)
{
    FT_COLR_Paint paint = {};
    if (!FT_Get_Paint(context.face, opaque, &paint)) return std::nullopt;

    ColrFill fill;
    switch (paint.format) {
    case FT_COLR_PAINTFORMAT_SOLID:
        fill.solid = colrPaletteColor(context, paint.u.solid.color);
        return fill;
    case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT:
        fill.kind = ColrFillKind::Linear;
        fill.extend = paint.u.linear_gradient.colorline.extend;
        fill.stops = readColrStops(context,
                                   paint.u.linear_gradient.colorline);
        fill.x0 = fixed16(paint.u.linear_gradient.p0.x);
        fill.y0 = fixed16(paint.u.linear_gradient.p0.y);
        fill.x1 = fixed16(paint.u.linear_gradient.p1.x);
        fill.y1 = fixed16(paint.u.linear_gradient.p1.y);
        return fill.stops.empty() ? std::nullopt
                                  : std::optional<ColrFill>(std::move(fill));
    case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT:
        fill.kind = ColrFillKind::Radial;
        fill.extend = paint.u.radial_gradient.colorline.extend;
        fill.stops = readColrStops(context,
                                   paint.u.radial_gradient.colorline);
        fill.x0 = fixed16(paint.u.radial_gradient.c0.x);
        fill.y0 = fixed16(paint.u.radial_gradient.c0.y);
        fill.r0 = fixed16(paint.u.radial_gradient.r0);
        fill.x1 = fixed16(paint.u.radial_gradient.c1.x);
        fill.y1 = fixed16(paint.u.radial_gradient.c1.y);
        fill.r1 = fixed16(paint.u.radial_gradient.r1);
        return fill.stops.empty() ? std::nullopt
                                  : std::optional<ColrFill>(std::move(fill));
    default:
        return std::nullopt;
    }
}

double applyColrExtend(double value, FT_PaintExtend extend)
{
    if (!std::isfinite(value)) return 0.0;
    if (extend == FT_COLR_PAINT_EXTEND_REPEAT) {
        value -= std::floor(value);
    } else if (extend == FT_COLR_PAINT_EXTEND_REFLECT) {
        value = std::fmod(value, 2.0);
        if (value < 0.0) value += 2.0;
        if (value > 1.0) value = 2.0 - value;
    } else {
        value = std::clamp(value, 0.0, 1.0);
    }
    return value;
}

RgbaColor interpolateColrStops(const ColrFill &fill, double offset)
{
    if (fill.stops.empty()) return fill.solid;
    offset = applyColrExtend(offset, fill.extend);
    if (offset <= fill.stops.front().offset) return fill.stops.front().color;
    if (offset >= fill.stops.back().offset) return fill.stops.back().color;
    const auto upper = std::upper_bound(
        fill.stops.begin(), fill.stops.end(), offset,
        [](double value, const ColrStop &stop) { return value < stop.offset; });
    const ColrStop &right = *upper;
    const ColrStop &left = *(upper - 1);
    const double span = right.offset - left.offset;
    const double t = span > 1e-9 ? (offset - left.offset) / span : 0.0;
    const auto channel = [&](unsigned char a, unsigned char b) {
        return static_cast<unsigned char>(std::clamp(
            std::lround(static_cast<double>(a) * (1.0 - t)
                        + static_cast<double>(b) * t), 0l, 255l));
    };
    return {channel(left.color.r, right.color.r),
            channel(left.color.g, right.color.g),
            channel(left.color.b, right.color.b),
            channel(left.color.a, right.color.a)};
}

RgbaColor sampleColrFill(const ColrFill &fill, double x, double y)
{
    if (fill.kind == ColrFillKind::Solid) return fill.solid;
    double offset = 0.0;
    if (fill.kind == ColrFillKind::Linear) {
        const double dx = fill.x1 - fill.x0;
        const double dy = fill.y1 - fill.y0;
        const double lengthSquared = dx * dx + dy * dy;
        if (lengthSquared > 1e-9) {
            offset = ((x - fill.x0) * dx + (y - fill.y0) * dy)
                / lengthSquared;
        }
    } else {
        const double dcx = fill.x1 - fill.x0;
        const double dcy = fill.y1 - fill.y0;
        const double dr = fill.r1 - fill.r0;
        const double qx = x - fill.x0;
        const double qy = y - fill.y0;
        const double a = dcx * dcx + dcy * dcy - dr * dr;
        const double b = -2.0 * (qx * dcx + qy * dcy + fill.r0 * dr);
        const double c = qx * qx + qy * qy - fill.r0 * fill.r0;
        if (std::abs(a) < 1e-9) {
            offset = std::abs(b) > 1e-9 ? -c / b : 0.0;
        } else {
            const double discriminant = std::max(0.0, b * b - 4.0 * a * c);
            const double root = std::sqrt(discriminant);
            const double first = (-b - root) / (2.0 * a);
            const double second = (-b + root) / (2.0 * a);
            offset = std::max(first, second);
            if (offset < 0.0) offset = std::min(first, second);
        }
    }
    return interpolateColrStops(fill, offset);
}

bool invertAffinePoint(const ColrAffine &affine, double x, double y,
                       double &localX, double &localY)
{
    const double determinant = affine.xx * affine.yy - affine.xy * affine.yx;
    if (std::abs(determinant) < 1e-12) return false;
    const double translatedX = x - affine.dx;
    const double translatedY = y - affine.dy;
    localX = (affine.yy * translatedX - affine.xy * translatedY)
        / determinant;
    localY = (-affine.yx * translatedX + affine.xx * translatedY)
        / determinant;
    return true;
}

std::vector<unsigned char> rasterizeColrGlyphMask(
    const ColrV1Context &context, FT_UInt glyphIndex,
    const ColrAffine &affine)
{
    std::vector<unsigned char> mask(
        static_cast<std::size_t>(context.width)
        * static_cast<std::size_t>(context.height));
    if (FT_Load_Glyph(context.face, glyphIndex,
                      FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING
                          | FT_LOAD_NO_BITMAP | FT_LOAD_IGNORE_TRANSFORM)
            != 0
        || context.face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
        return mask;
    }

    FT_Outline &outline = context.face->glyph->outline;
    for (short index = 0; index < outline.n_points; ++index) {
        const double x = static_cast<double>(outline.points[index].x);
        const double y = static_cast<double>(outline.points[index].y);
        const double transformedX = affine.xx * x + affine.xy * y + affine.dx;
        const double transformedY = affine.yx * x + affine.yy * y + affine.dy;
        outline.points[index].x = static_cast<FT_Pos>(std::lround(
            (transformedX - context.xMin) * context.scale * 64.0));
        outline.points[index].y = static_cast<FT_Pos>(std::lround(
            (context.yMax - transformedY) * context.scale * 64.0));
    }

    FT_Bitmap bitmap = {};
    bitmap.width = static_cast<unsigned int>(context.width);
    bitmap.rows = static_cast<unsigned int>(context.height);
    bitmap.pitch = context.width;
    bitmap.buffer = mask.data();
    bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
    bitmap.num_grays = 256;
    if (FT_Outline_Get_Bitmap(freeTypeLibrary().library, &outline, &bitmap)
        != 0) {
        std::fill(mask.begin(), mask.end(), 0);
    }
    return mask;
}

void sourceOverCanvas(std::vector<unsigned char> &destination,
                      const std::vector<unsigned char> &source)
{
    const std::size_t pixels = std::min(destination.size(), source.size()) / 4u;
    for (std::size_t index = 0; index < pixels; ++index) {
        const unsigned char *src = source.data() + index * 4u;
        compositePixel(destination.data() + index * 4u,
                       {src[0], src[1], src[2], src[3]}, 255);
    }
}

bool renderColrV1Paint(const ColrV1Context &context, FT_OpaquePaint opaque,
                       const ColrAffine &affine,
                       std::vector<unsigned char> &canvas, int depth)
{
    if (depth > 64) return false;
    FT_COLR_Paint paint = {};
    if (!FT_Get_Paint(context.face, opaque, &paint)) return false;

    switch (paint.format) {
    case FT_COLR_PAINTFORMAT_COLR_LAYERS: {
        FT_LayerIterator iterator = paint.u.colr_layers.layer_iterator;
        FT_OpaquePaint layer = {};
        bool rendered = false;
        while (FT_Get_Paint_Layers(context.face, &iterator, &layer)) {
            rendered = renderColrV1Paint(context, layer, affine, canvas,
                                         depth + 1) || rendered;
        }
        return rendered;
    }
    case FT_COLR_PAINTFORMAT_GLYPH: {
        const auto fill = readColrFill(context, paint.u.glyph.paint);
        if (!fill) return false;
        const std::vector<unsigned char> mask = rasterizeColrGlyphMask(
            context, paint.u.glyph.glyphID, affine);
        bool rendered = false;
        for (int y = 0; y < context.height; ++y) {
            for (int x = 0; x < context.width; ++x) {
                const std::size_t index = static_cast<std::size_t>(y)
                    * static_cast<std::size_t>(context.width)
                    + static_cast<std::size_t>(x);
                const unsigned char coverage = mask[index];
                if (coverage == 0) continue;
                const double globalX = context.xMin
                    + (static_cast<double>(x) + 0.5) / context.scale;
                const double globalY = context.yMax
                    - (static_cast<double>(y) + 0.5) / context.scale;
                double localX = globalX;
                double localY = globalY;
                if (!invertAffinePoint(affine, globalX, globalY,
                                       localX, localY)) {
                    continue;
                }
                compositePixel(canvas.data() + index * 4u,
                               sampleColrFill(*fill, localX, localY), coverage);
                rendered = true;
            }
        }
        return rendered;
    }
    case FT_COLR_PAINTFORMAT_COLR_GLYPH: {
        FT_OpaquePaint nested = {};
        if (!FT_Get_Color_Glyph_Paint(
                context.face, paint.u.colr_glyph.glyphID,
                FT_COLOR_NO_ROOT_TRANSFORM, &nested)) {
            return false;
        }
        return renderColrV1Paint(context, nested, affine, canvas, depth + 1);
    }
    case FT_COLR_PAINTFORMAT_TRANSFORM: {
        const FT_Affine23 &value = paint.u.transform.affine;
        const ColrAffine transform = {
            fixed16(value.xx), fixed16(value.xy), fixed16(value.yx),
            fixed16(value.yy), fixed16(value.dx), fixed16(value.dy)};
        return renderColrV1Paint(context, paint.u.transform.paint,
                                 multiplyAffine(affine, transform), canvas,
                                 depth + 1);
    }
    case FT_COLR_PAINTFORMAT_TRANSLATE: {
        ColrAffine transform;
        transform.dx = fixed16(paint.u.translate.dx);
        transform.dy = fixed16(paint.u.translate.dy);
        return renderColrV1Paint(context, paint.u.translate.paint,
                                 multiplyAffine(affine, transform), canvas,
                                 depth + 1);
    }
    case FT_COLR_PAINTFORMAT_SCALE: {
        const double sx = fixed16(paint.u.scale.scale_x);
        const double sy = fixed16(paint.u.scale.scale_y);
        const double cx = fixed16(paint.u.scale.center_x);
        const double cy = fixed16(paint.u.scale.center_y);
        ColrAffine transform;
        transform.xx = sx;
        transform.yy = sy;
        transform.dx = cx - sx * cx;
        transform.dy = cy - sy * cy;
        return renderColrV1Paint(context, paint.u.scale.paint,
                                 multiplyAffine(affine, transform), canvas,
                                 depth + 1);
    }
    case FT_COLR_PAINTFORMAT_ROTATE: {
        const double angle = fixed16(paint.u.rotate.angle)
            * 3.14159265358979323846;
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        const double cx = fixed16(paint.u.rotate.center_x);
        const double cy = fixed16(paint.u.rotate.center_y);
        ColrAffine transform;
        transform.xx = cosine;
        transform.xy = -sine;
        transform.yx = sine;
        transform.yy = cosine;
        transform.dx = cx - cosine * cx + sine * cy;
        transform.dy = cy - sine * cx - cosine * cy;
        return renderColrV1Paint(context, paint.u.rotate.paint,
                                 multiplyAffine(affine, transform), canvas,
                                 depth + 1);
    }
    case FT_COLR_PAINTFORMAT_SKEW: {
        const double xAngle = fixed16(paint.u.skew.x_skew_angle)
            * 3.14159265358979323846;
        const double yAngle = fixed16(paint.u.skew.y_skew_angle)
            * 3.14159265358979323846;
        const double xSkew = std::tan(xAngle);
        const double ySkew = std::tan(yAngle);
        const double cx = fixed16(paint.u.skew.center_x);
        const double cy = fixed16(paint.u.skew.center_y);
        ColrAffine transform;
        transform.xy = xSkew;
        transform.yx = ySkew;
        transform.dx = -xSkew * cy;
        transform.dy = -ySkew * cx;
        return renderColrV1Paint(context, paint.u.skew.paint,
                                 multiplyAffine(affine, transform), canvas,
                                 depth + 1);
    }
    case FT_COLR_PAINTFORMAT_COMPOSITE: {
        std::vector<unsigned char> backdrop(canvas.size());
        std::vector<unsigned char> source(canvas.size());
        const bool drewBackdrop = renderColrV1Paint(
            context, paint.u.composite.backdrop_paint, affine, backdrop,
            depth + 1);
        const bool drewSource = renderColrV1Paint(
            context, paint.u.composite.source_paint, affine, source,
            depth + 1);
        if (paint.u.composite.composite_mode == FT_COLR_COMPOSITE_SRC_IN) {
            const std::size_t pixels = source.size() / 4u;
            for (std::size_t index = 0; index < pixels; ++index) {
                unsigned char *src = source.data() + index * 4u;
                const unsigned char backdropAlpha = backdrop[index * 4u + 3u];
                src[3] = static_cast<unsigned char>(
                    static_cast<unsigned int>(src[3]) * backdropAlpha / 255u);
            }
            backdrop = std::move(source);
        } else {
            // Preserve a visible result for uncommon artistic blend modes;
            // exact Porter-Duff and separable-blend coverage can be extended
            // here without changing the font/provider contract.
            sourceOverCanvas(backdrop, source);
        }
        sourceOverCanvas(canvas, backdrop);
        return drewBackdrop || drewSource;
    }
    default:
        return false;
    }
}

std::optional<wsc::text::GlyphBitmap> rasterizeColrV1Glyph(
    FT_Face face, FT_UInt glyphIndex, float pixelSize)
{
    if (face == nullptr || face->units_per_EM <= 0 || pixelSize <= 0.0f) {
        return std::nullopt;
    }
    FT_OpaquePaint root = {};
    if (!FT_Get_Color_Glyph_Paint(face, glyphIndex,
                                  FT_COLOR_NO_ROOT_TRANSFORM, &root)) {
        return std::nullopt;
    }

    FT_Color *palette = nullptr;
    FT_Palette_Data paletteData = {};
    if (FT_Palette_Data_Get(face, &paletteData) != 0
        || FT_Palette_Select(face, 0, &palette) != 0) {
        palette = nullptr;
    }

    const double scale = static_cast<double>(pixelSize)
        / static_cast<double>(face->units_per_EM);
    constexpr int padding = 2;
    ColrV1Context context;
    context.face = face;
    context.palette = palette;
    context.paletteEntries = paletteData.num_palette_entries;
    context.scale = scale;
    context.xMin = static_cast<double>(face->bbox.xMin)
        - static_cast<double>(padding) / scale;
    context.yMax = static_cast<double>(face->bbox.yMax)
        + static_cast<double>(padding) / scale;
    context.width = std::clamp(
        static_cast<int>(std::ceil(
            static_cast<double>(face->bbox.xMax - face->bbox.xMin) * scale))
            + padding * 2,
        1, 2048);
    context.height = std::clamp(
        static_cast<int>(std::ceil(
            static_cast<double>(face->bbox.yMax - face->bbox.yMin) * scale))
            + padding * 2,
        1, 2048);

    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(context.width)
        * static_cast<std::size_t>(context.height) * 4u);
    const bool rendered = renderColrV1Paint(
        context, root, ColrAffine{}, pixels, 0);
    bool hasVisiblePixel = false;
    for (std::size_t offset = 3; offset < pixels.size(); offset += 4u) {
        if (pixels[offset] != 0) {
            hasVisiblePixel = true;
            break;
        }
    }
    if (!rendered || !hasVisiblePixel) {
        return std::nullopt;
    }

    float advance = pixelSize;
    if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT) == 0) {
        advance = static_cast<float>(face->glyph->advance.x) / 64.0f;
    }

    wsc::text::GlyphBitmap bitmap;
    bitmap.format = wsc::text::GlyphBitmapFormat::RGBA;
    bitmap.width = context.width;
    bitmap.height = context.height;
    bitmap.bearingX = static_cast<float>(context.xMin * scale);
    bitmap.bearingY = static_cast<float>(-context.yMax * scale);
    bitmap.advanceX = advance;
    bitmap.rgbaPixels = std::move(pixels);
    return bitmap;
}

std::vector<unsigned char> resizeAlphaBitmap(
    const std::vector<unsigned char> &source, int sourceWidth, int sourceHeight,
    int targetWidth, int targetHeight)
{
    std::vector<unsigned char> result(
        static_cast<std::size_t>(targetWidth)
        * static_cast<std::size_t>(targetHeight));
    for (int y = 0; y < targetHeight; ++y) {
        const float sourceY = (static_cast<float>(y) + 0.5f)
            * static_cast<float>(sourceHeight) / static_cast<float>(targetHeight)
            - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(sourceY)), 0, sourceHeight - 1);
        const int y1 = std::min(y0 + 1, sourceHeight - 1);
        const float fy = std::clamp(sourceY - std::floor(sourceY), 0.0f, 1.0f);
        for (int x = 0; x < targetWidth; ++x) {
            const float sourceX = (static_cast<float>(x) + 0.5f)
                * static_cast<float>(sourceWidth) / static_cast<float>(targetWidth)
                - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(sourceX)), 0, sourceWidth - 1);
            const int x1 = std::min(x0 + 1, sourceWidth - 1);
            const float fx = std::clamp(sourceX - std::floor(sourceX), 0.0f, 1.0f);
            const auto sample = [&](int sampleX, int sampleY) {
                return static_cast<float>(source[
                    static_cast<std::size_t>(sampleY) * sourceWidth + sampleX]);
            };
            const float top = sample(x0, y0) * (1.0f - fx) + sample(x1, y0) * fx;
            const float bottom = sample(x0, y1) * (1.0f - fx) + sample(x1, y1) * fx;
            result[static_cast<std::size_t>(y) * targetWidth + x] =
                static_cast<unsigned char>(std::clamp(
                    std::lround(top * (1.0f - fy) + bottom * fy), 0l, 255l));
        }
    }
    return result;
}

std::vector<unsigned char> resizeRgbaBitmap(
    const std::vector<unsigned char> &source, int sourceWidth, int sourceHeight,
    int targetWidth, int targetHeight)
{
    std::vector<unsigned char> result(
        static_cast<std::size_t>(targetWidth)
        * static_cast<std::size_t>(targetHeight) * 4u);
    for (int y = 0; y < targetHeight; ++y) {
        const float sourceY = (static_cast<float>(y) + 0.5f)
            * static_cast<float>(sourceHeight) / static_cast<float>(targetHeight)
            - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(sourceY)), 0, sourceHeight - 1);
        const int y1 = std::min(y0 + 1, sourceHeight - 1);
        const float fy = std::clamp(sourceY - std::floor(sourceY), 0.0f, 1.0f);
        for (int x = 0; x < targetWidth; ++x) {
            const float sourceX = (static_cast<float>(x) + 0.5f)
                * static_cast<float>(sourceWidth) / static_cast<float>(targetWidth)
                - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(sourceX)), 0, sourceWidth - 1);
            const int x1 = std::min(x0 + 1, sourceWidth - 1);
            const float fx = std::clamp(sourceX - std::floor(sourceX), 0.0f, 1.0f);
            const float weights[4] = {
                (1.0f - fx) * (1.0f - fy), fx * (1.0f - fy),
                (1.0f - fx) * fy, fx * fy
            };
            const int sampleX[4] = {x0, x1, x0, x1};
            const int sampleY[4] = {y0, y0, y1, y1};
            float alpha = 0.0f;
            float premultiplied[3] = {0.0f, 0.0f, 0.0f};
            for (int sampleIndex = 0; sampleIndex < 4; ++sampleIndex) {
                const std::size_t offset =
                    (static_cast<std::size_t>(sampleY[sampleIndex]) * sourceWidth
                     + sampleX[sampleIndex]) * 4u;
                const float sampleAlpha = source[offset + 3u] / 255.0f;
                alpha += sampleAlpha * weights[sampleIndex];
                for (int channel = 0; channel < 3; ++channel) {
                    premultiplied[channel] += source[offset + channel]
                        * sampleAlpha * weights[sampleIndex];
                }
            }
            const std::size_t target =
                (static_cast<std::size_t>(y) * targetWidth + x) * 4u;
            for (int channel = 0; channel < 3; ++channel) {
                result[target + channel] = alpha <= 0.0f ? 0
                    : static_cast<unsigned char>(std::clamp(
                        std::lround(premultiplied[channel] / alpha), 0l, 255l));
            }
            result[target + 3u] = static_cast<unsigned char>(std::clamp(
                std::lround(alpha * 255.0f), 0l, 255l));
        }
    }
    return result;
}

// CBDT image format 17 stores small glyph metrics followed by PNG data.
// Android devices ship both 2.0 (for example Pixel 3 / Android 12) and 3.0
// table versions with the same index-format-1/image-format-17 layout. Accept
// both so the fallback decoder still works when FreeType lacks PNG support.
std::optional<wsc::text::GlyphBitmap> rasterizeCbdtPngGlyph(
    const std::vector<unsigned char> &fontBytes, std::size_t fontOffset,
    int glyphIndex, float pixelSize)
{
    const auto cblc = findSfntTable(fontBytes, fontOffset, "CBLC");
    const auto cbdt = findSfntTable(fontBytes, fontOffset, "CBDT");
    const auto supportedBitmapVersion = [](std::uint32_t version) {
        return version == 0x00020000u || version == 0x00030000u;
    };
    if (!cblc || !cbdt || cblc->size < 8u || cbdt->size < 4u
        || !supportedBitmapVersion(readU32BE(cblc->data))
        || !supportedBitmapVersion(readU32BE(cbdt->data)) || glyphIndex <= 0
        || pixelSize <= 0.0f) {
        return std::nullopt;
    }

    const std::uint32_t strikeCount = readU32BE(cblc->data + 4u);
    if (strikeCount == 0u || strikeCount > (cblc->size - 8u) / 48u) {
        return std::nullopt;
    }

    struct Candidate
    {
        const unsigned char *subtable = nullptr;
        std::uint16_t firstGlyph = 0;
        std::uint16_t lastGlyph = 0;
        int ppem = 0;
        int distance = std::numeric_limits<int>::max();
    } best;
    const int requestedPixels = std::max(1, static_cast<int>(std::lround(pixelSize)));
    for (std::uint32_t strikeIndex = 0; strikeIndex < strikeCount; ++strikeIndex) {
        const unsigned char *strike = cblc->data + 8u
            + static_cast<std::size_t>(strikeIndex) * 48u;
        const std::uint16_t startGlyph = readU16BE(strike + 40u);
        const std::uint16_t endGlyph = readU16BE(strike + 42u);
        if (glyphIndex < startGlyph || glyphIndex > endGlyph) continue;
        const std::uint32_t arrayOffset = readU32BE(strike);
        const std::uint32_t subtableCount = readU32BE(strike + 8u);
        if (arrayOffset > cblc->size
            || subtableCount > (cblc->size - arrayOffset) / 8u) {
            continue;
        }
        const int ppem = strike[45u] != 0 ? strike[45u] : strike[44u];
        if (ppem <= 0) continue;
        for (std::uint32_t tableIndex = 0; tableIndex < subtableCount; ++tableIndex) {
            const unsigned char *entry = cblc->data + arrayOffset
                + static_cast<std::size_t>(tableIndex) * 8u;
            const std::uint16_t firstGlyph = readU16BE(entry);
            const std::uint16_t lastGlyph = readU16BE(entry + 2u);
            if (glyphIndex < firstGlyph || glyphIndex > lastGlyph) continue;
            const std::uint32_t additionalOffset = readU32BE(entry + 4u);
            if (additionalOffset > cblc->size - arrayOffset) continue;
            const std::size_t subtableOffset = static_cast<std::size_t>(arrayOffset)
                + additionalOffset;
            if (subtableOffset > cblc->size || cblc->size - subtableOffset < 8u) {
                continue;
            }
            const unsigned char *subtable = cblc->data + subtableOffset;
            if (readU16BE(subtable) != 1u || readU16BE(subtable + 2u) != 17u) {
                continue;
            }
            const std::size_t offsetCount =
                static_cast<std::size_t>(lastGlyph - firstGlyph) + 2u;
            if (offsetCount > (cblc->size - subtableOffset - 8u) / 4u) continue;
            const int distance = std::abs(ppem - requestedPixels);
            if (distance < best.distance) {
                best = {subtable, firstGlyph, lastGlyph, ppem, distance};
            }
        }
    }
    if (best.subtable == nullptr) return std::nullopt;

    const std::size_t glyphOffsetIndex =
        static_cast<std::size_t>(glyphIndex - best.firstGlyph);
    const std::uint32_t startOffset = readU32BE(
        best.subtable + 8u + glyphOffsetIndex * 4u);
    const std::uint32_t endOffset = readU32BE(
        best.subtable + 8u + (glyphOffsetIndex + 1u) * 4u);
    const std::uint32_t imageDataOffset = readU32BE(best.subtable + 4u);
    if (endOffset <= startOffset || imageDataOffset > cbdt->size
        || startOffset > cbdt->size - imageDataOffset) {
        return std::nullopt;
    }
    const std::size_t imageOffset = static_cast<std::size_t>(imageDataOffset)
        + startOffset;
    const std::size_t recordSize = static_cast<std::size_t>(endOffset - startOffset);
    if (imageOffset > cbdt->size || recordSize > cbdt->size - imageOffset
        || recordSize < 9u) {
        return std::nullopt;
    }
    const unsigned char *record = cbdt->data + imageOffset;
    const unsigned int metricHeight = record[0u];
    const unsigned int metricWidth = record[1u];
    const auto bearingX = static_cast<std::int8_t>(record[2u]);
    const auto bearingY = static_cast<std::int8_t>(record[3u]);
    const unsigned int advance = record[4u];
    const std::uint32_t pngLength = readU32BE(record + 5u);
    if (pngLength == 0u || pngLength > recordSize - 9u
        || pngLength > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    int decodedWidth = 0;
    int decodedHeight = 0;
    int decodedChannels = 0;
    stbi_uc *decoded = stbi_load_from_memory(
        record + 9u, static_cast<int>(pngLength),
        &decodedWidth, &decodedHeight, &decodedChannels, 4);
    if (decoded == nullptr || decodedWidth <= 0 || decodedHeight <= 0) {
        if (decoded != nullptr) stbi_image_free(decoded);
        return std::nullopt;
    }
    if ((metricWidth != 0u && decodedWidth != static_cast<int>(metricWidth))
        || (metricHeight != 0u && decodedHeight != static_cast<int>(metricHeight))) {
        stbi_image_free(decoded);
        return std::nullopt;
    }
    if (static_cast<std::size_t>(decodedWidth)
        > std::numeric_limits<std::size_t>::max()
            / static_cast<std::size_t>(decodedHeight) / 4u) {
        stbi_image_free(decoded);
        return std::nullopt;
    }
    const std::size_t decodedSize = static_cast<std::size_t>(decodedWidth)
        * static_cast<std::size_t>(decodedHeight) * 4u;
    std::vector<unsigned char> pixels(decoded, decoded + decodedSize);
    stbi_image_free(decoded);

    const float scale = pixelSize / static_cast<float>(best.ppem);
    const int targetWidth = std::max(
        1, static_cast<int>(std::lround(decodedWidth * scale)));
    const int targetHeight = std::max(
        1, static_cast<int>(std::lround(decodedHeight * scale)));
    if (targetWidth != decodedWidth || targetHeight != decodedHeight) {
        pixels = resizeRgbaBitmap(
            pixels, decodedWidth, decodedHeight, targetWidth, targetHeight);
    }

    wsc::text::GlyphBitmap bitmap;
    bitmap.format = wsc::text::GlyphBitmapFormat::RGBA;
    bitmap.width = targetWidth;
    bitmap.height = targetHeight;
    bitmap.bearingX = static_cast<float>(bearingX) * scale;
    bitmap.bearingY = -static_cast<float>(bearingY) * scale;
    bitmap.advanceX = static_cast<float>(advance) * scale;
    bitmap.rgbaPixels = std::move(pixels);
    return bitmap;
}
#endif

} // namespace

namespace wsc::text {

std::string fontVariationIdentity(
    const std::vector<FontVariationCoordinate> &coordinates)
{
    return makeVariationIdentity(coordinates);
}

std::string fontFaceIdentity(const FontFace &face)
{
    return makeFaceKey(face);
}

ColorFontTables detectColorFontTables(FontDataView fontData, int faceIndex)
{
    ColorFontTables result;
    if (fontData.data == nullptr || fontData.size < 12u) {
        return result;
    }

    std::size_t fontOffset = 0;
    const int clampedFaceIndex = std::max(0, faceIndex);
    if (tagEquals(fontData.data, "ttcf")) {
        if (fontData.size < 16u) {
            return result;
        }
        const std::uint32_t faceCount = readU32BE(fontData.data + 8u);
        if (faceCount == 0u || static_cast<std::uint32_t>(clampedFaceIndex) >= faceCount
            || fontData.size - 12u < (static_cast<std::size_t>(clampedFaceIndex) + 1u) * 4u) {
            return result;
        }
        fontOffset = static_cast<std::size_t>(readU32BE(fontData.data + 12u
            + static_cast<std::size_t>(clampedFaceIndex) * 4u));
        if (fontOffset > fontData.size || fontData.size - fontOffset < 12u) {
            return result;
        }
    } else if (clampedFaceIndex > 0) {
        return result;
    }

    const unsigned char *sfnt = fontData.data + fontOffset;
    const std::size_t sfntSize = fontData.size - fontOffset;
    const std::uint16_t tableCount = readU16BE(sfnt + 4u);
    if (tableCount == 0u || tableCount > (sfntSize - 12u) / 16u) {
        return result;
    }

    for (std::uint16_t i = 0; i < tableCount; ++i) {
        const unsigned char *record = sfnt + 12u + static_cast<std::size_t>(i) * 16u;
        result.colr = result.colr || tagEquals(record, "COLR");
        result.cpal = result.cpal || tagEquals(record, "CPAL");
        result.cbdt = result.cbdt || tagEquals(record, "CBDT");
        result.cblc = result.cblc || tagEquals(record, "CBLC");
        result.sbix = result.sbix || tagEquals(record, "sbix");
        result.svg = result.svg || tagEquals(record, "SVG ");
    }
    return result;
}

struct FontRasterizer::LoadedFace
{
    ~LoadedFace()
    {
#if defined(WHATSCANVAS_HAS_FREETYPE)
        if (ftFace != nullptr) {
            FT_Done_Face(ftFace);
        }
#endif
    }

    // FreeType and stb_truetype only borrow the byte range, so retain the
    // provider's immutable blob instead of copying large Android TTC/emoji
    // fonts into every rasterizer face entry.
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    stbtt_fontinfo info = {};
    std::size_t fontOffset = 0;
    bool stbValid = false;
#if defined(WHATSCANVAS_HAS_FREETYPE)
    FT_Face ftFace = nullptr;
#endif
    bool valid = false;
};

struct FontRasterizer::CacheState
{
    std::unordered_map<std::string, std::unique_ptr<LoadedFace>> entries;
    std::deque<std::string> lruOrder;
    std::size_t capacity = kDefaultLoadedFaceCacheCapacity;
    std::size_t hitCount = 0;
    std::size_t missCount = 0;
    std::size_t evictionCount = 0;
};

FontRasterizer::CacheState &FontRasterizer::cacheState()
{
    static CacheState state;
    return state;
}

std::mutex &FontRasterizer::cacheMutex()
{
    static std::mutex mutex;
    return mutex;
}

void FontRasterizer::touchCacheEntry(CacheState &cache, const std::string &key)
{
    const auto existing = std::find(cache.lruOrder.begin(), cache.lruOrder.end(), key);
    if (existing != cache.lruOrder.end()) {
        cache.lruOrder.erase(existing);
    }
    cache.lruOrder.push_back(key);
}

void FontRasterizer::trimCache(CacheState &cache)
{
    while (cache.entries.size() > cache.capacity && !cache.lruOrder.empty()) {
        const std::string evictedKey = cache.lruOrder.front();
        cache.lruOrder.pop_front();
        if (cache.entries.erase(evictedKey) > 0u) {
            ++cache.evictionCount;
        }
    }
}

FontRasterizerCacheStats FontRasterizer::cacheStats() const
{
    std::lock_guard<std::mutex> lock(cacheMutex());
    const CacheState &cache = cacheState();
    FontRasterizerCacheStats stats;
    stats.faceCount = cache.entries.size();
    stats.capacity = cache.capacity;
    stats.hitCount = cache.hitCount;
    stats.missCount = cache.missCount;
    stats.evictionCount = cache.evictionCount;
    return stats;
}

void FontRasterizer::clearCache() const
{
    std::lock_guard<std::mutex> lock(cacheMutex());
    CacheState &cache = cacheState();
    cache.entries.clear();
    cache.lruOrder.clear();
    cache.hitCount = 0;
    cache.missCount = 0;
    cache.evictionCount = 0;
}

void FontRasterizer::setCacheCapacity(std::size_t capacity) const
{
    std::lock_guard<std::mutex> lock(cacheMutex());
    CacheState &cache = cacheState();
    cache.capacity = std::max<std::size_t>(1u, capacity);
    trimCache(cache);
}

const FontRasterizer::LoadedFace *FontRasterizer::loadFace(const FontFace &face) const
{
    CacheState &cache = cacheState();
    const std::string key = makeFaceKey(face);
    const auto found = cache.entries.find(key);
    if (found != cache.entries.end()) {
        ++cache.hitCount;
        touchCacheEntry(cache, key);
        return found->second->valid ? found->second.get() : nullptr;
    }
    ++cache.missCount;

    auto loaded = std::make_unique<LoadedFace>();
    if (face.sourceType() == FontSourceType::FILE) {
        std::vector<unsigned char> fileBytes = readFileBytes(face.path());
        if (!fileBytes.empty()) {
            loaded->bytes =
                std::make_shared<const std::vector<std::uint8_t>>(
                    std::move(fileBytes));
        }
    } else {
        loaded->bytes = face.sharedBytes();
    }

    if (loaded->bytes && !loaded->bytes->empty()) {
        const int fontOffset = stbtt_GetFontOffsetForIndex(
            loaded->bytes->data(), face.faceIndex());
        loaded->fontOffset = fontOffset >= 0 ? static_cast<std::size_t>(fontOffset) : 0u;
        loaded->stbValid = fontOffset >= 0
            && stbtt_InitFont(
                &loaded->info, loaded->bytes->data(), fontOffset) != 0;
        loaded->valid = loaded->stbValid;
#if defined(WHATSCANVAS_HAS_FREETYPE)
        if (freeTypeLibrary().valid()) {
            FT_Face ftFace = nullptr;
            if (FT_New_Memory_Face(freeTypeLibrary().library,
                                   loaded->bytes->data(),
                                   static_cast<FT_Long>(loaded->bytes->size()),
                                   static_cast<FT_Long>(face.faceIndex()),
                                   &ftFace) == 0) {
                loaded->ftFace = ftFace;
                applyVariationCoordinates(ftFace, face.variationCoordinates());
                loaded->valid = true;
            }
        }
#endif
    }

    LoadedFace *result = loaded.get();
    cache.entries.emplace(key, std::move(loaded));
    touchCacheEntry(cache, key);
    trimCache(cache);
    return result->valid ? result : nullptr;
}

bool FontRasterizer::hasGlyph(const FontFace &face, std::uint32_t codepoint) const
{
    if (face.hasCodepointRanges() && !face.supportsCodepoint(codepoint)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(cacheMutex());
    const LoadedFace *loaded = loadFace(face);
#if defined(WHATSCANVAS_HAS_FREETYPE)
    if (loaded != nullptr && loaded->ftFace != nullptr) {
        return FT_Get_Char_Index(loaded->ftFace, static_cast<FT_ULong>(codepoint)) != 0;
    }
#endif
    return loaded != nullptr && loaded->stbValid
        && stbtt_FindGlyphIndex(&loaded->info, static_cast<int>(codepoint)) != 0;
}

std::optional<int> FontRasterizer::glyphIndex(const FontFace &face, std::uint32_t codepoint) const
{
    std::lock_guard<std::mutex> lock(cacheMutex());
    const LoadedFace *loaded = loadFace(face);
    if (loaded == nullptr) {
        return std::nullopt;
    }

#if defined(WHATSCANVAS_HAS_FREETYPE)
    if (loaded->ftFace != nullptr) {
        const FT_UInt index = FT_Get_Char_Index(loaded->ftFace, static_cast<FT_ULong>(codepoint));
        return index == 0 ? std::nullopt : std::optional<int>(static_cast<int>(index));
    }
#endif

    if (!loaded->stbValid) {
        return std::nullopt;
    }

    const int index = stbtt_FindGlyphIndex(&loaded->info, static_cast<int>(codepoint));
    return index == 0 ? std::nullopt : std::optional<int>(index);
}

std::optional<float> FontRasterizer::glyphAdvance(const FontFace &face, std::uint32_t codepoint,
                                                  float pixelSize) const
{
    const auto metrics = glyphMetrics(face, codepoint, pixelSize);
    return metrics ? std::optional<float>(metrics->advanceX) : std::nullopt;
}

std::optional<float> FontRasterizer::glyphKerning(const FontFace &face, int leftGlyphIndex, int rightGlyphIndex,
                                                  float pixelSize) const
{
    std::lock_guard<std::mutex> lock(cacheMutex());
    const LoadedFace *loaded = loadFace(face);
    if (loaded == nullptr || pixelSize <= 0.0f || leftGlyphIndex <= 0 || rightGlyphIndex <= 0) {
        return std::nullopt;
    }

#if defined(WHATSCANVAS_HAS_FREETYPE)
    if (loaded->ftFace != nullptr && FT_HAS_KERNING(loaded->ftFace)
        && setFreeTypePixelSize(loaded->ftFace, pixelSize)) {
        FT_Vector kerning = {};
        if (FT_Get_Kerning(loaded->ftFace,
                           static_cast<FT_UInt>(leftGlyphIndex),
                           static_cast<FT_UInt>(rightGlyphIndex),
                           FT_KERNING_DEFAULT,
                           &kerning) == 0) {
            return static_cast<float>(kerning.x) / 64.0f;
        }
    }

#endif

    if (!loaded->stbValid) {
        return std::nullopt;
    }

    const int advance = stbtt_GetGlyphKernAdvance(&loaded->info, leftGlyphIndex, rightGlyphIndex);
    return static_cast<float>(advance) * stbtt_ScaleForPixelHeight(&loaded->info, pixelSize);
}

std::optional<FontVerticalMetrics> FontRasterizer::verticalMetrics(const FontFace &face, float pixelSize) const
{
    std::lock_guard<std::mutex> lock(cacheMutex());
    const LoadedFace *loaded = loadFace(face);
    if (loaded == nullptr || pixelSize <= 0.0f) {
        return std::nullopt;
    }

#if defined(WHATSCANVAS_HAS_FREETYPE)
    if (loaded->ftFace != nullptr && setFreeTypePixelSize(loaded->ftFace, pixelSize)) {
        const FT_Size_Metrics &ftMetrics = loaded->ftFace->size->metrics;
        FontVerticalMetrics metrics;
        metrics.ascent = static_cast<float>(ftMetrics.ascender) / 64.0f;
        metrics.descent = static_cast<float>(ftMetrics.descender) / 64.0f;
        metrics.lineHeight = std::max(metrics.ascent - metrics.descent,
                                      static_cast<float>(ftMetrics.height) / 64.0f);
        metrics.lineGap = metrics.lineHeight - (metrics.ascent - metrics.descent);
        return metrics;
    }
#endif

    if (!loaded->stbValid) {
        return std::nullopt;
    }

    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&loaded->info, &ascent, &descent, &lineGap);
    const float scale = stbtt_ScaleForPixelHeight(&loaded->info, pixelSize);

    FontVerticalMetrics metrics;
    metrics.ascent = static_cast<float>(ascent) * scale;
    metrics.descent = static_cast<float>(descent) * scale;
    metrics.lineGap = static_cast<float>(lineGap) * scale;
    metrics.lineHeight = metrics.ascent - metrics.descent + metrics.lineGap;
    return metrics;
}

std::optional<GlyphMetrics> FontRasterizer::glyphMetrics(const FontFace &face, std::uint32_t codepoint,
                                                         float pixelSize) const
{
    std::lock_guard<std::mutex> lock(cacheMutex());
    const LoadedFace *loaded = loadFace(face);
    if (loaded == nullptr || pixelSize <= 0.0f) {
        return std::nullopt;
    }

#if defined(WHATSCANVAS_HAS_FREETYPE)
    if (loaded->ftFace != nullptr && setFreeTypePixelSize(loaded->ftFace, pixelSize)) {
        const FT_UInt index = FT_Get_Char_Index(loaded->ftFace, static_cast<FT_ULong>(codepoint));
        if (index == 0 || FT_Load_Glyph(loaded->ftFace, index, FT_LOAD_DEFAULT) != 0) {
            return std::nullopt;
        }

        GlyphMetrics metrics;
        metrics.glyphIndex = static_cast<int>(index);
        metrics.advanceX = static_cast<float>(loaded->ftFace->glyph->advance.x) / 64.0f;
        return metrics;
    }
#endif

    if (!loaded->stbValid) {
        return std::nullopt;
    }

    const int glyphIndex = stbtt_FindGlyphIndex(&loaded->info, static_cast<int>(codepoint));
    if (glyphIndex == 0) {
        return std::nullopt;
    }

    int advance = 0;
    int leftBearing = 0;
    stbtt_GetGlyphHMetrics(&loaded->info, glyphIndex, &advance, &leftBearing);
    GlyphMetrics metrics;
    metrics.glyphIndex = glyphIndex;
    metrics.advanceX = static_cast<float>(advance) * stbtt_ScaleForPixelHeight(&loaded->info, pixelSize);
    return metrics;
}

std::optional<RasterizedGlyph> FontRasterizer::rasterizeGlyph(const FontFace &face, std::uint32_t codepoint,
                                                              float pixelSize) const
{
    const auto index = glyphIndex(face, codepoint);
    if (!index) {
        return std::nullopt;
    }

    return rasterizeGlyphIndex(face, *index, codepoint, pixelSize);
}

std::optional<RasterizedGlyph> FontRasterizer::rasterizeGlyphIndex(const FontFace &face, int glyphIndex,
                                                                   std::uint32_t sourceCodepoint,
                                                                   float pixelSize) const
{
    std::lock_guard<std::mutex> lock(cacheMutex());
    const LoadedFace *loaded = loadFace(face);
    if (loaded == nullptr || pixelSize <= 0.0f || glyphIndex <= 0) {
        return std::nullopt;
    }

#if defined(WHATSCANVAS_HAS_FREETYPE)
    if (loaded->ftFace != nullptr && setFreeTypePixelSize(loaded->ftFace, pixelSize)) {
        if (auto colorBitmap = rasterizeColrV1Glyph(
                loaded->ftFace, static_cast<FT_UInt>(glyphIndex), pixelSize)) {
            RasterizedGlyph glyph;
            glyph.key.fontFamily = face.family();
            glyph.key.codepoint = sourceCodepoint;
            glyph.key.glyphIndex = glyphIndex;
            glyph.key.pixelSize = pixelSize;
            glyph.key.format = GlyphBitmapFormat::RGBA;
            glyph.key.weight = face.weight();
            glyph.key.slant = face.slant();
            glyph.key.faceIndex = face.faceIndex();
            glyph.key.fontIdentity = fontFaceIdentity(face);
            glyph.bitmap = std::move(*colorBitmap);
            return glyph;
        }
        const FT_Error loadError = FT_Load_Glyph(
            loaded->ftFace, static_cast<FT_UInt>(glyphIndex),
            FT_LOAD_DEFAULT | FT_LOAD_COLOR);
        FT_Error renderError = 0;
        bool bitmapReady = loadError == 0;
        if (bitmapReady
            && loaded->ftFace->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
            renderError = FT_Render_Glyph(
                loaded->ftFace->glyph, FT_RENDER_MODE_NORMAL);
            bitmapReady = renderError == 0;
        }
        if (bitmapReady) {
        const FT_GlyphSlot slot = loaded->ftFace->glyph;
        const FT_Bitmap &ftBitmap = slot->bitmap;

        GlyphBitmap bitmap;
        bitmap.format = ftBitmap.pixel_mode == FT_PIXEL_MODE_BGRA
            ? GlyphBitmapFormat::RGBA : GlyphBitmapFormat::Alpha;
        bitmap.width = static_cast<int>(ftBitmap.width);
        bitmap.height = static_cast<int>(ftBitmap.rows);
        bitmap.bearingX = static_cast<float>(slot->bitmap_left);
        bitmap.bearingY = -static_cast<float>(slot->bitmap_top);
        bitmap.advanceX = static_cast<float>(slot->advance.x) / 64.0f;
        const std::size_t pixelCount =
            static_cast<std::size_t>(std::max(0, bitmap.width))
            * static_cast<std::size_t>(std::max(0, bitmap.height));
        if (bitmap.format == GlyphBitmapFormat::RGBA) {
            bitmap.rgbaPixels.resize(pixelCount * 4u);
        } else {
            bitmap.alphaPixels.resize(pixelCount);
        }

        if (bitmap.width > 0 && bitmap.height > 0) {
            for (int row = 0; row < bitmap.height; ++row) {
                const unsigned char *srcRow = freeTypeBitmapRow(ftBitmap, row);
                if (srcRow == nullptr) continue;
                for (int col = 0; col < bitmap.width; ++col) {
                    const std::size_t dst = static_cast<std::size_t>(row) * static_cast<std::size_t>(bitmap.width)
                        + static_cast<std::size_t>(col);
                    if (bitmap.format == GlyphBitmapFormat::RGBA) {
                        const unsigned char *src = srcRow
                            + static_cast<std::ptrdiff_t>(col) * 4;
                        const unsigned int alpha = src[3];
                        const auto unpremultiply = [&](unsigned char channel) {
                            return alpha == 0 ? static_cast<unsigned char>(0)
                                : static_cast<unsigned char>(std::min(
                                    255u, (static_cast<unsigned int>(channel) * 255u
                                           + alpha / 2u) / alpha));
                        };
                        bitmap.rgbaPixels[dst * 4u + 0u] = unpremultiply(src[2]);
                        bitmap.rgbaPixels[dst * 4u + 1u] = unpremultiply(src[1]);
                        bitmap.rgbaPixels[dst * 4u + 2u] = unpremultiply(src[0]);
                        bitmap.rgbaPixels[dst * 4u + 3u] = src[3];
                    } else if (ftBitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
                        const unsigned int gray = srcRow[col];
                        bitmap.alphaPixels[dst] = ftBitmap.num_grays > 1
                            ? static_cast<unsigned char>(gray * 255u
                                / static_cast<unsigned int>(ftBitmap.num_grays - 1u))
                            : (gray == 0 ? 0 : 255);
                    } else if (ftBitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
                        bitmap.alphaPixels[dst] =
                            (srcRow[col >> 3] & (0x80u >> (col & 7))) != 0
                            ? 255 : 0;
                    } else {
                        bitmap.alphaPixels[dst] = srcRow[col] == 0 ? 0 : 255;
                    }
                }
            }
        }

        const float selectedPixelSize = loaded->ftFace->size != nullptr
            ? static_cast<float>(loaded->ftFace->size->metrics.y_ppem)
            : pixelSize;
        if (bitmap.width > 0 && bitmap.height > 0 && selectedPixelSize > 0.0f
            && std::abs(selectedPixelSize - pixelSize) > 0.5f) {
            const float bitmapScale = pixelSize / selectedPixelSize;
            const int targetWidth = std::max(
                1, static_cast<int>(std::lround(bitmap.width * bitmapScale)));
            const int targetHeight = std::max(
                1, static_cast<int>(std::lround(bitmap.height * bitmapScale)));
            if (bitmap.format == GlyphBitmapFormat::RGBA) {
                bitmap.rgbaPixels = resizeRgbaBitmap(
                    bitmap.rgbaPixels, bitmap.width, bitmap.height,
                    targetWidth, targetHeight);
            } else {
                bitmap.alphaPixels = resizeAlphaBitmap(
                    bitmap.alphaPixels, bitmap.width, bitmap.height,
                    targetWidth, targetHeight);
            }
            bitmap.width = targetWidth;
            bitmap.height = targetHeight;
            bitmap.bearingX *= bitmapScale;
            bitmap.bearingY *= bitmapScale;
            bitmap.advanceX *= bitmapScale;
        }

        RasterizedGlyph glyph;
        glyph.key.fontFamily = face.family();
        glyph.key.codepoint = sourceCodepoint;
        glyph.key.glyphIndex = glyphIndex;
        glyph.key.pixelSize = pixelSize;
        glyph.key.format = bitmap.format;
        glyph.key.weight = face.weight();
        glyph.key.slant = face.slant();
        glyph.key.faceIndex = face.faceIndex();
        glyph.key.fontIdentity = fontFaceIdentity(face);
        glyph.bitmap = std::move(bitmap);
        return glyph;
        }
    }

    if (auto bitmap = rasterizeCbdtPngGlyph(
            *loaded->bytes, loaded->fontOffset, glyphIndex, pixelSize)) {
        RasterizedGlyph glyph;
        glyph.key.fontFamily = face.family();
        glyph.key.codepoint = sourceCodepoint;
        glyph.key.glyphIndex = glyphIndex;
        glyph.key.pixelSize = pixelSize;
        glyph.key.format = GlyphBitmapFormat::RGBA;
        glyph.key.weight = face.weight();
        glyph.key.slant = face.slant();
        glyph.key.faceIndex = face.faceIndex();
        glyph.key.fontIdentity = fontFaceIdentity(face);
        glyph.bitmap = std::move(*bitmap);
        return glyph;
    }
#endif

    if (loaded->stbValid) {
        if (auto colorGlyph = rasterizeColorGlyph(
                face, *loaded, glyphIndex, sourceCodepoint, pixelSize)) {
            return colorGlyph;
        }
    }

    if (!loaded->stbValid) {
        return std::nullopt;
    }

    const float scale = stbtt_ScaleForPixelHeight(&loaded->info, pixelSize);
    int advance = 0;
    int leftBearing = 0;
    stbtt_GetGlyphHMetrics(&loaded->info, glyphIndex, &advance, &leftBearing);

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetGlyphBitmapBox(&loaded->info, glyphIndex, scale, scale, &x0, &y0, &x1, &y1);

    GlyphBitmap bitmap;
    bitmap.format = GlyphBitmapFormat::Alpha;
    bitmap.width = std::max(0, x1 - x0);
    bitmap.height = std::max(0, y1 - y0);
    bitmap.bearingX = static_cast<float>(x0);
    bitmap.bearingY = static_cast<float>(y0);
    bitmap.advanceX = static_cast<float>(advance) * scale;
    bitmap.alphaPixels.resize(static_cast<std::size_t>(bitmap.width) * static_cast<std::size_t>(bitmap.height));

    if (bitmap.width > 0 && bitmap.height > 0) {
        stbtt_MakeGlyphBitmap(&loaded->info, bitmap.alphaPixels.data(), bitmap.width, bitmap.height,
                              bitmap.width, scale, scale, glyphIndex);
    }

    RasterizedGlyph glyph;
    glyph.key.fontFamily = face.family();
    glyph.key.codepoint = sourceCodepoint;
    glyph.key.glyphIndex = glyphIndex;
    glyph.key.pixelSize = pixelSize;
    glyph.key.format = GlyphBitmapFormat::Alpha;
    glyph.key.weight = face.weight();
    glyph.key.slant = face.slant();
    glyph.key.faceIndex = face.faceIndex();
    glyph.key.fontIdentity = fontFaceIdentity(face);
    glyph.bitmap = std::move(bitmap);
    return glyph;
}

std::optional<RasterizedGlyph> FontRasterizer::rasterizeColorGlyph(const FontFace &face,
                                                                   const LoadedFace &loaded,
                                                                   int glyphIndex,
                                                                   std::uint32_t sourceCodepoint,
                                                                   float pixelSize) const
{
    if (!loaded.bytes) {
        return std::nullopt;
    }
    const auto colr = findSfntTable(*loaded.bytes, loaded.fontOffset, "COLR");
    const auto cpal = findSfntTable(*loaded.bytes, loaded.fontOffset, "CPAL");
    if (!colr || !cpal) {
        return std::nullopt;
    }

    const auto layers = findColrLayers(*colr, glyphIndex);
    if (!layers) {
        return std::nullopt;
    }

    const float scale = stbtt_ScaleForPixelHeight(&loaded.info, pixelSize);
    int advance = 0;
    int leftBearing = 0;
    stbtt_GetGlyphHMetrics(&loaded.info, glyphIndex, &advance, &leftBearing);

    int unionLeft = 0;
    int unionTop = 0;
    int unionRight = 0;
    int unionBottom = 0;
    bool hasBounds = false;
    for (const ColorLayer &layer : *layers) {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetGlyphBitmapBox(&loaded.info, layer.glyphIndex, scale, scale, &x0, &y0, &x1, &y1);
        if (x1 <= x0 || y1 <= y0) {
            continue;
        }
        if (!hasBounds) {
            unionLeft = x0;
            unionTop = y0;
            unionRight = x1;
            unionBottom = y1;
            hasBounds = true;
        } else {
            unionLeft = std::min(unionLeft, x0);
            unionTop = std::min(unionTop, y0);
            unionRight = std::max(unionRight, x1);
            unionBottom = std::max(unionBottom, y1);
        }
    }

    if (!hasBounds) {
        return std::nullopt;
    }

    GlyphBitmap bitmap;
    bitmap.format = GlyphBitmapFormat::RGBA;
    bitmap.width = unionRight - unionLeft;
    bitmap.height = unionBottom - unionTop;
    bitmap.bearingX = static_cast<float>(unionLeft);
    bitmap.bearingY = static_cast<float>(unionTop);
    bitmap.advanceX = static_cast<float>(advance) * scale;
    bitmap.rgbaPixels.resize(static_cast<std::size_t>(bitmap.width) * static_cast<std::size_t>(bitmap.height) * 4u);

    bool painted = false;
    for (const ColorLayer &layer : *layers) {
        const auto color = cpalColor(*cpal, layer.paletteIndex);
        if (!color) {
            continue;
        }

        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetGlyphBitmapBox(&loaded.info, layer.glyphIndex, scale, scale, &x0, &y0, &x1, &y1);
        const int layerWidth = x1 - x0;
        const int layerHeight = y1 - y0;
        if (layerWidth <= 0 || layerHeight <= 0) {
            continue;
        }

        std::vector<unsigned char> coverage(static_cast<std::size_t>(layerWidth) * static_cast<std::size_t>(layerHeight));
        stbtt_MakeGlyphBitmap(&loaded.info, coverage.data(), layerWidth, layerHeight,
                              layerWidth, scale, scale, layer.glyphIndex);

        for (int y = 0; y < layerHeight; ++y) {
            for (int x = 0; x < layerWidth; ++x) {
                const std::size_t coverageIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(layerWidth)
                    + static_cast<std::size_t>(x);
                const int dstX = x0 - unionLeft + x;
                const int dstY = y0 - unionTop + y;
                const std::size_t dstIndex = (static_cast<std::size_t>(dstY) * static_cast<std::size_t>(bitmap.width)
                    + static_cast<std::size_t>(dstX)) * 4u;
                compositePixel(bitmap.rgbaPixels.data() + dstIndex, *color, coverage[coverageIndex]);
                painted = painted || coverage[coverageIndex] != 0;
            }
        }
    }

    if (!painted) {
        return std::nullopt;
    }

    RasterizedGlyph glyph;
    glyph.key.fontFamily = face.family();
    glyph.key.codepoint = sourceCodepoint;
    glyph.key.glyphIndex = glyphIndex;
    glyph.key.pixelSize = pixelSize;
    glyph.key.format = GlyphBitmapFormat::RGBA;
    glyph.key.weight = face.weight();
    glyph.key.slant = face.slant();
    glyph.key.faceIndex = face.faceIndex();
    glyph.key.fontIdentity = fontFaceIdentity(face);
    glyph.bitmap = std::move(bitmap);
    return glyph;
}

std::optional<FontDataView> FontRasterizer::fontData(const FontFace &face) const
{
    std::lock_guard<std::mutex> lock(cacheMutex());
    const LoadedFace *loaded = loadFace(face);
    if (loaded == nullptr || !loaded->bytes || loaded->bytes->empty()) {
        return std::nullopt;
    }

    thread_local std::shared_ptr<const std::vector<std::uint8_t>> snapshot;
    snapshot = loaded->bytes;
    return FontDataView{
        snapshot->data(), snapshot->size(), face.faceIndex()};
}

std::optional<ColorFontTables> FontRasterizer::colorFontTables(const FontFace &face) const
{
    std::lock_guard<std::mutex> lock(cacheMutex());
    const LoadedFace *loaded = loadFace(face);
    if (loaded == nullptr || !loaded->bytes || loaded->bytes->empty()) {
        return std::nullopt;
    }
    return detectColorFontTables(FontDataView{
                                     loaded->bytes->data(),
                                     loaded->bytes->size(),
                                     face.faceIndex()},
                                 face.faceIndex());
}

} // namespace wsc::text
