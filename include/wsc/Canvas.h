#pragma once

#include "base.h"
#include "Image.h"
#include "Paint.h"
#include "Path.h"

#if __has_include("../../src/canvas/Canvas.h")
#include "../../src/canvas/Canvas.h"
#else
#include "../whatscanvas-src/canvas/Canvas.h"
#endif

namespace wsc {
using ::Canvas;
using ::IRenderer;

namespace text {
using ITextBackend = ::prismcanvas::text::ITextBackend;
}
}