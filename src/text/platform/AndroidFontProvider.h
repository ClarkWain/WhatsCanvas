#pragma once

#include <memory>

namespace wsc {
class FontProvider;
}

namespace wsc::text {

/// Create the Android API 29+ system font matcher provider. Returns nullptr on
/// non-Android platforms. On Android API 21-28 the provider remains available
/// with a deliberately small, file-backed compatibility candidate set.
std::shared_ptr<FontProvider> createAndroidSystemFontProvider();

} // namespace wsc::text
