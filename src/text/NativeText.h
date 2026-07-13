#pragma once

#include <string>
#include <vector>

#include "canvas/Paint.h"

namespace wsc::text {

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
    // GDI's logical advance can end exactly at the final ink pixel. Keep the
    // bitmap allocation wider without changing layout measurement, and let
    // the renderer offset the transparent safety pixels back out.
    int leftPadding = 0;
    int rightPadding = 0;
    std::vector<unsigned char> pixels;
};

NativeTextMeasure measureNativeText(const std::string &text, const Paint &paint);
NativeTextBitmap renderNativeTextBitmap(const std::string &text, const Paint &paint,
                                        const NativeTextMeasure &measure);

} // namespace wsc::text
