#pragma once

#if __has_include("../../src/canvas/Image.h")
#include "../../src/canvas/Image.h"
#else
#include "../whatscanvas-src/canvas/Image.h"
#endif

namespace wsc {
using ::Image;
using ::IRenderer;
}