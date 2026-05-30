#pragma once

#include <memory>

namespace prismcanvas::text {

class ITextBackend;

std::unique_ptr<ITextBackend> createBasicTextBackend();

} // namespace prismcanvas::text