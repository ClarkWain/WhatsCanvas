#pragma once

#include <string>
#include <vector>

#include "canvas/Paint.h"

namespace prismcanvas::text {

struct NativeTextMeasure
{
    bool valid = false;
    float width = 0.0f;
    float height = 0.0f;
    int pixelWidth = 0;
    int pixelHeight = 0;
};

struct NativeTextBitmap
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};

NativeTextMeasure measureNativeText(const std::string &text, const Paint &paint);
NativeTextBitmap renderNativeTextBitmap(const std::string &text, const Paint &paint,
                                        const NativeTextMeasure &measure);

} // namespace prismcanvas::text