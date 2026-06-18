#pragma once

#include "base.h"

#if __has_include("../whatscanvas-src/canvas/Path.h")
#include "../whatscanvas-src/canvas/Path.h"
#else
#include "../../src/canvas/Path.h"
#endif

namespace wsc {
using ::Path;
}