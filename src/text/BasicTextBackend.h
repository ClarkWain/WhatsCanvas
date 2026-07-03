#pragma once

#include <memory>
#include <vector>

#include "text/TextShaper.h"

namespace wsc::text {

class ITextBackend;

enum class TextBackendKind
{
    Auto,
    Portable,
    WindowsNative,
    DirectWrite,
    CoreText
};

struct TextBackendCapability
{
    TextBackendKind kind = TextBackendKind::Portable;
    const char *name = "";
    bool available = false;
    bool nativePlatformAdapter = false;
    bool supportsFontRegistration = false;
    bool supportsGlyphAtlas = false;
    bool supportsColorGlyphAtlas = false;
    bool supportsOpenTypeShaping = false;
};

struct BasicTextBackendOptions
{
    TextBackendKind backendKind = TextBackendKind::Auto;
    bool enableNativeText = true;
    bool enableSystemFontFallback = true;
    TextShapingBackend shapingBackend = TextShapingBackend::Simple;
};

std::vector<TextBackendCapability> queryTextBackendCapabilities();
std::unique_ptr<ITextBackend> createBasicTextBackend();
std::unique_ptr<ITextBackend> createBasicTextBackend(const BasicTextBackendOptions &options);
std::unique_ptr<ITextBackend> createPortableTextBackend();
std::unique_ptr<ITextBackend> createTextBackend(TextBackendKind kind);

} // namespace wsc::text
