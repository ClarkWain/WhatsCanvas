#include "text/CoreTextTextBackend.h"

namespace wsc::text {

bool isCoreTextAvailable()
{
    return false;
}

std::unique_ptr<ITextBackend> createCoreTextTextBackend(
    const CoreTextBackendOptions &)
{
    return nullptr;
}

} // namespace wsc::text
