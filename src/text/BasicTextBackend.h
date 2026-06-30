#pragma once

#include <memory>

#include "text/TextShaper.h"

namespace wsc::text {

class ITextBackend;

struct BasicTextBackendOptions
{
    bool enableNativeText = true;
    TextShapingBackend shapingBackend = TextShapingBackend::Simple;
};

std::unique_ptr<ITextBackend> createBasicTextBackend();
std::unique_ptr<ITextBackend> createBasicTextBackend(const BasicTextBackendOptions &options);
std::unique_ptr<ITextBackend> createPortableTextBackend();

} // namespace wsc::text
