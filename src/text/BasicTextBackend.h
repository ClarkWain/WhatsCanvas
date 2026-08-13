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
    bool preferClearType = false; // DirectWrite: use ClearType instead of grayscale.
    TextShapingBackend shapingBackend = TextShapingBackend::OpenType;
};

std::vector<TextBackendCapability> queryTextBackendCapabilities();
std::unique_ptr<ITextBackend> createBasicTextBackend();
std::unique_ptr<ITextBackend> createBasicTextBackend(const BasicTextBackendOptions &options);
std::unique_ptr<ITextBackend> createPortableTextBackend();
std::unique_ptr<ITextBackend> createTextBackend(TextBackendKind kind);

/// Returns true only if `backend` was produced by createBasicTextBackend() AND
/// actually constructed a native DirectWrite adapter. Returns false for a null
/// pointer, a non-BasicTextBackend, or a BasicTextBackend that fell back to the
/// portable backend at runtime (e.g. a Windows COM/factory failure). This is the
/// source of truth for what the backend really is, as opposed to the
/// compile-time isDirectWriteAvailable() probe.
bool isNativeDirectWriteActive(const ITextBackend *backend);

} // namespace wsc::text
