#pragma once

// Auto-generated payloads from textured.vert and textured.frag using:
// glslc --target-env=vulkan1.1 -O -mfmt=c <input> -o <payload>

#include <cstdint>

static const std::uint32_t kTexturedVertSpv[] =
#include "TexturedVertSpv.h"
;

static const std::uint32_t kTexturedInstancedVertSpv[] =
#include "TexturedInstancedVertSpv.h"
;

static const std::uint32_t kTexturedFragSpv[] =
#include "TexturedFragSpv.h"
;

static const std::uint32_t kTexturedFastFragSpv[] =
#include "TexturedFastFragSpv.h"
;
