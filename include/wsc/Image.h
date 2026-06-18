#pragma once

#if __has_include("../whatscanvas-src/canvas/Image.h")
#include "../whatscanvas-src/canvas/Image.h"
#else
#include "../../src/canvas/Image.h"
#endif

namespace wsc {
using ::Image;
}