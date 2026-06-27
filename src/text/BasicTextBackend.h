#pragma once

#include <memory>

namespace wsc::text {

class ITextBackend;

struct BasicTextBackendOptions
{
    bool enableNativeText = true;
};

std::unique_ptr<ITextBackend> createBasicTextBackend();
std::unique_ptr<ITextBackend> createBasicTextBackend(const BasicTextBackendOptions &options);
std::unique_ptr<ITextBackend> createPortableTextBackend();

} // namespace wsc::text
