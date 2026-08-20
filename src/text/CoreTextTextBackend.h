#pragma once

#include "text/ITextBackend.h"

#include <memory>

namespace wsc::text {

struct CoreTextBackendOptions
{
    bool enableSystemFontFallback = true;
};

/// Returns true when the build can construct a native CoreText backend.
bool isCoreTextAvailable();

/// Creates the Apple-native layout and grayscale bitmap-raster backend.
/// Returns nullptr on non-Apple platforms or when CoreText is unavailable.
std::unique_ptr<ITextBackend> createCoreTextTextBackend(
    const CoreTextBackendOptions &options = {});

} // namespace wsc::text
