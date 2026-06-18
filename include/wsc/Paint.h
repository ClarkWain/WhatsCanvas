#pragma once

#include "base.h"

#if __has_include("../whatscanvas-src/canvas/Paint.h")
#include "../whatscanvas-src/canvas/Paint.h"
#else
#include "../../src/canvas/Paint.h"
#endif

namespace wsc {
using ::Color;
using ::Paint;
}