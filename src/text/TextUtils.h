#pragma once

#include <string>
#include <vector>

#include "canvas/Paint.h"

namespace prismcanvas::text {

std::string sanitizeTextToAscii(const std::string &text);
float measureAsciiTextWidth(const std::string &asciiText, float scale, float letterSpacing);
float measureAsciiTextHeight(const std::string &asciiText, float scale);
float textBaselineOffset(Paint::TextBaseline baseline, float textHeight);
std::vector<float> buildTextVertices(const std::string &asciiText, float x, float y, float scale,
                                     float letterSpacing = 0.0f);

} // namespace prismcanvas::text