#pragma once

#include <memory>

namespace wsc::text {

class ITextBackend;

std::unique_ptr<ITextBackend> createBasicTextBackend();

} // namespace wsc::text
