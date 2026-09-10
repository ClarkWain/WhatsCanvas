#include "PieceArt.h"
#include <wsc/Path.h>

namespace chess {
void drawPieceArt(wsc::Canvas& c, Piece piece, float x, float y) {
    using namespace wsc;
    c.save(); c.translate(x, y);
    const bool white = piece.side == Side::White;
    Paint body;
    body.setLinearGradient(14, 10, 48, 54, white ? Color(255, 247, 221) : Color(103, 92, 79),
                                         white ? Color(192, 160, 112) : Color(30, 29, 29));
    Paint edge;
    edge.setColor(white ? Color(91, 63, 36) : Color(205, 178, 128));
    edge.setStyle(Paint::Style::STROKE); edge.setStrokeWidth(1.2f);
    Paint detail = edge; detail.setStrokeWidth(1.5f);
    Paint shadow; shadow.setColor(Color(0, 0, 0, 65));
    c.save(); c.translate(32, 58); c.scale(1, 0.20f); c.drawCircle(0, 0, 26, shadow); c.restore();
    auto shape = [&](Path& path) { c.drawPath(path, body); c.drawPath(path, edge); };
    auto round = [&](RectF r, float radius) { c.drawRoundRect(r, radius, body); c.drawRoundRect(r, radius, edge); };
    auto circle = [&](float cx, float cy, float radius) { c.drawCircle(cx, cy, radius, body); c.drawCircle(cx, cy, radius, edge); };
    auto stem = [&] {
        Path p; p.moveTo(18, 49); p.quadTo(27, 40, 26, 27); p.lineTo(38, 27);
        p.quadTo(37, 40, 46, 49); p.close(); shape(p);
    };
    if (piece.kind == Kind::Knight) {
        Path p;
        p.moveTo(17, 49); p.quadTo(17, 40, 25, 32); p.lineTo(21, 29);
        p.lineTo(15, 32); p.lineTo(12, 26); p.lineTo(23, 17); p.lineTo(26, 10);
        p.lineTo(30, 14); p.lineTo(35, 8); p.lineTo(37, 17);
        p.quadTo(48, 20, 49, 33); p.quadTo(49, 42, 44, 49); p.close(); shape(p);
        c.drawLine(26.0f, 34.0f, 39.0f, 42.0f, detail);
        Paint eye; eye.setColor(white ? Color(56, 41, 29) : Color(237, 207, 152));
        c.drawCircle(29, 24, 1.7f, eye);
        c.drawLine(41.0f, 23.0f, 45.0f, 34.0f, detail);
    } else if (piece.kind == Kind::Rook) {
        Path p; p.moveTo(17, 49); p.lineTo(22, 43); p.lineTo(22, 29); p.lineTo(16, 25);
        p.lineTo(16, 12); p.lineTo(23, 12); p.lineTo(23, 19); p.lineTo(28, 19);
        p.lineTo(28, 12); p.lineTo(36, 12); p.lineTo(36, 19); p.lineTo(41, 19);
        p.lineTo(41, 12); p.lineTo(48, 12); p.lineTo(48, 25); p.lineTo(42, 29);
        p.lineTo(42, 43); p.lineTo(47, 49); p.close(); shape(p);
        c.drawLine(23.0f, 30.0f, 41.0f, 30.0f, detail);
        c.drawLine(23.0f, 42.0f, 41.0f, 42.0f, detail);
    } else {
        stem();
        if (piece.kind == Kind::Pawn) {
            circle(32, 20, 9);
            round(RectF(22, 29, 20, 4), 2);
        } else if (piece.kind == Kind::Bishop) {
            Path p; p.moveTo(32, 8); p.cubicTo(46, 20, 45, 25, 32, 29);
            p.cubicTo(19, 25, 18, 20, 32, 8); p.close(); shape(p);
            c.drawLine(34.0f, 15.0f, 28.0f, 23.0f, detail);
            circle(32, 7, 2.5f); round(RectF(22, 30, 20, 4), 2);
        } else if (piece.kind == Kind::Queen) {
            Path p; p.moveTo(24, 31); p.lineTo(17, 15); p.lineTo(26, 20);
            p.lineTo(32, 10); p.lineTo(38, 20); p.lineTo(47, 15); p.lineTo(40, 31); p.close(); shape(p);
            for (auto pos : {PointF(17, 14), PointF(32, 9), PointF(47, 14)}) circle(pos.getX(), pos.getY(), 2.7f);
            round(RectF(22, 31, 20, 4), 2);
        } else {
            Path p; p.moveTo(23, 29); p.quadTo(17, 19, 25, 17); p.lineTo(39, 17);
            p.quadTo(47, 19, 41, 29); p.close(); shape(p);
            round(RectF(29, 3, 6, 15), 1); round(RectF(24, 7, 16, 5), 1);
            round(RectF(22, 30, 20, 4), 2);
        }
    }
    round(RectF(17, 48, 30, 5), 2);
    round(RectF(12, 53, 40, 5), 2);
    c.restore();
}
} // namespace chess
