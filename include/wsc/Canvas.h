#pragma once

#include "base.h"
#include "Image.h"
#include "Paint.h"
#include "Path.h"

#if __has_include("../whatscanvas-src/canvas/Canvas.h")
#include "../whatscanvas-src/canvas/Canvas.h"
#else
#include "../../src/canvas/Canvas.h"
#endif

namespace wsc {
using ::Canvas;
}