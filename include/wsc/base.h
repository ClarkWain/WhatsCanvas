#pragma once

#if __has_include("../../src/canvas/base.h")
#include "../../src/canvas/base.h"
#else
#include "../whatscanvas-src/canvas/base.h"
#endif

namespace wsc {
using ::Point;
using ::PointF;
using ::Size;
using ::SizeF;
using ::Rect;
using ::RectF;
}