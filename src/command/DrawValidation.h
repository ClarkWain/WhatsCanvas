#pragma once

#include <iostream>
#include <string>

/// Centralized draw-state validation utilities.
/// Provides runtime checks that catch common errors early
/// with meaningful diagnostic messages instead of silent GL failures.
namespace DrawValidation {

/// Check that a vertex data buffer is non-empty.
/// Returns true if validation passes (data is non-empty).
inline bool validateVertexData(std::size_t vertexCount, const char *caller)
{
    if (vertexCount == 0) {
        std::cerr << "[DrawValidation] " << caller << ": vertex data is empty, draw call skipped." << std::endl;
        return false;
    }
    return true;
}

/// Check that image dimensions are positive.
/// Returns true if validation passes.
inline bool validateImageDimensions(float width, float height, const char *caller)
{
    if (width <= 0.0f || height <= 0.0f) {
        std::cerr << "[DrawValidation] " << caller
                  << ": image dimensions non-positive (w=" << width
                  << ", h=" << height << "), draw call skipped." << std::endl;
        return false;
    }
    return true;
}

/// Check that a pointer/resource is valid (non-null and isValid()).
/// Returns true if validation passes.
template<typename T>
bool validateResource(const T &resource, const char *name, const char *caller)
{
    if (!resource || !resource->isValid()) {
        std::cerr << "[DrawValidation] " << caller << ": "
                  << name << " is null or invalid, draw call skipped." << std::endl;
        return false;
    }
    return true;
}

/// Check that a blend mode value is within valid range.
/// Returns true if validation passes.
inline bool validateBlendMode(int mode, int maxMode, const char *caller)
{
    if (mode < 0 || mode > maxMode) {
        std::cerr << "[DrawValidation] " << caller
                  << ": blend mode " << mode << " out of range [0, "
                  << maxMode << "]." << std::endl;
        return false;
    }
    return true;
}

/// Check that a program/shader is initialized.
/// Returns true if validation passes.
inline bool validateProgram(bool initialized, const char *caller)
{
    if (!initialized) {
        std::cerr << "[DrawValidation] " << caller
                  << ": shader program not initialized, draw call skipped." << std::endl;
        return false;
    }
    return true;
}

} // namespace DrawValidation
