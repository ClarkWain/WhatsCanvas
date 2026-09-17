#pragma once
#include "Rules.h"
#include <wsc/Canvas.h>
namespace chess {
// Procedural Staunton-style silhouette in one 64x64 atlas cell. No font glyph,
// external vector file, raster asset or downloaded artwork is used for pieces.
void drawPieceArt(wsc::Canvas& canvas, Piece piece, float x, float y);
}
