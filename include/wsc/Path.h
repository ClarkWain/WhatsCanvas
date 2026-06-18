#pragma once

#include "base.h"

#if __has_include("../../src/canvas/Path.h")
#include "../../src/canvas/Path.h"
#else
#include "../whatscanvas-src/canvas/Path.h"
#endif

namespace wsc {
using ::Path;
}