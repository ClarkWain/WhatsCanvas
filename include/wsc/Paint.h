#pragma once

#include "base.h"

#if __has_include("../../src/canvas/Paint.h")
#include "../../src/canvas/Paint.h"
#else
#include "../whatscanvas-src/canvas/Paint.h"
#endif

namespace wsc {
using ::Color;
using ::Paint;
}