#include "SpiderGameInternal.h"

namespace spider {

void SpiderGame::updateRenderTransform(int windowW, int windowH) {
    const float scale = std::min(windowW / DESIGN_W, windowH / DESIGN_H);
    const float ox = (windowW - DESIGN_W * scale) * 0.5f;
    const float oy = (windowH - DESIGN_H * scale) * 0.5f;
    renderScale_ = scale;
    renderOffsetX_ = ox;
    renderOffsetY_ = oy;
}

void SpiderGame::render(Canvas& canvas, int windowW, int windowH) {
    if (useCardAtlas_) ensureCardAtlas(canvas);
    updateRenderTransform(windowW, windowH);
    const float scale = renderScale_;
    const float ox = renderOffsetX_;
    const float oy = renderOffsetY_;

    drawOutside(canvas, windowW, windowH);
    canvas.save();
    canvas.translate(ox, oy);
    canvas.scale(scale, scale);
    // Keep every design-space decoration inside the aspect-fitted table.
    // In particular, the diagonal felt lines begin at negative x positions.
    canvas.clipRect(RectF(0, 0, DESIGN_W, DESIGN_H));
    if (usePictureCache_) {
        if (!tablePicture_) {
            tablePicture_ = canvas.recordPicture([this](Canvas& recording) {
                drawTable(recording);
            });
        }
        // A cropped rasterized Picture currently shifts local radial-gradient
        // coordinates by the layer's screen-space origin. The table is static,
        // so the compiled vector Picture still avoids re-recording while
        // preserving the exact design-space gradient centre.
        if (tablePicture_) canvas.drawPicture(*tablePicture_);
        else drawTable(canvas);
    } else {
        drawTable(canvas);
    }
    drawHeader(canvas);
    drawColumns(canvas);
    drawStock(canvas);
    drawToast(canvas);
    if (won_ && completionMotions_.empty()) drawWin(canvas);
    canvas.restore();
}

void SpiderGame::drawTable(Canvas& canvas) {
    Paint line;
    line.setStyle(Paint::Style::STROKE);
    line.setStrokeWidth(1.0f);
    line.setColor(Color(208, 225, 192, 10));
    for (int i = -3; i < 15; ++i) {
        const float sourceX = i * 110.0f;
        constexpr float kLineWidth = 520.0f;
        constexpr float kTop = 108.0f;
        constexpr float kBottom = DESIGN_H - 1.0f;
        const float begin = std::max(0.0f, (1.0f - sourceX) / kLineWidth);
        const float end = std::min(1.0f, (DESIGN_W - 1.0f - sourceX) / kLineWidth);
        if (begin > end) continue;
        canvas.drawLine(sourceX + kLineWidth * begin,
                        kTop + (kBottom - kTop) * begin,
                        sourceX + kLineWidth * end,
                        kTop + (kBottom - kTop) * end,
                        line);
    }

    Paint rail;
    rail.setStyle(Paint::Style::FILL);
    rail.setColor(Color(3, 42, 30, 128));
    canvas.drawRoundRect(RectF(24, 116, 1232, 48), 16, rail);
}

void SpiderGame::drawSpiderMark(Canvas& canvas, float cx, float cy, float s, const Color& color) const {
    Paint stroke;
    stroke.setStyle(Paint::Style::STROKE);
    stroke.setStrokeWidth(std::max(1.0f, s * 0.065f));
    stroke.setStrokeCap(Paint::StrokeCap::ROUND);
    stroke.setColor(color);
    for (int side : {-1, 1}) {
        const float d = static_cast<float>(side);
        Path front;
        front.moveTo(cx + d * s * 0.10f, cy - s * 0.20f);
        front.cubicTo(cx + d * s * 0.23f, cy - s * 0.30f,
                      cx + d * s * 0.35f, cy - s * 0.48f,
                      cx + d * s * 0.52f, cy - s * 0.62f);
        canvas.drawPath(front, stroke);

        Path upper;
        upper.moveTo(cx + d * s * 0.13f, cy - s * 0.08f);
        upper.cubicTo(cx + d * s * 0.28f, cy - s * 0.10f,
                      cx + d * s * 0.45f, cy - s * 0.26f,
                      cx + d * s * 0.65f, cy - s * 0.29f);
        canvas.drawPath(upper, stroke);

        Path lower;
        lower.moveTo(cx + d * s * 0.13f, cy + s * 0.06f);
        lower.cubicTo(cx + d * s * 0.29f, cy + s * 0.09f,
                      cx + d * s * 0.46f, cy + s * 0.23f,
                      cx + d * s * 0.64f, cy + s * 0.32f);
        canvas.drawPath(lower, stroke);

        Path rear;
        rear.moveTo(cx + d * s * 0.10f, cy + s * 0.18f);
        rear.cubicTo(cx + d * s * 0.22f, cy + s * 0.30f,
                     cx + d * s * 0.34f, cy + s * 0.49f,
                     cx + d * s * 0.50f, cy + s * 0.63f);
        canvas.drawPath(rear, stroke);
    }
    Paint body;
    body.setStyle(Paint::Style::FILL);
    body.setColor(color);
    canvas.drawOval(RectF(cx - s * 0.145f, cy - s * 0.02f, s * 0.29f, s * 0.43f), body);
    canvas.drawCircle(cx, cy - s * 0.14f, s * 0.13f, body);
    canvas.drawCircle(cx, cy - s * 0.31f, s * 0.075f, body);
}

void SpiderGame::drawHeader(Canvas& canvas) {
    ensureHeaderChrome(canvas);
    if (usePictureCache_ && headerChromePicture_) {
        replayPicture(canvas, *headerChromePicture_, /*smallSprite=*/true);
    } else {
        drawHeaderChromeContents(canvas);
    }
    // Undo availability changes after every move. Keep this small control
    // dynamic instead of invalidating and rasterizing the entire header.
    if (!history_.empty()) {
        drawButton(canvas, undoButton(), "UNDO", true, 3, false);
    }
    drawHeaderDynamic(canvas);
}

void SpiderGame::drawHeaderDynamic(Canvas& canvas) {
    drawMetricValue(canvas, 260, std::to_string(score_));
    drawMetricValue(canvas, 375, std::to_string(moves_));
    drawMetricValue(canvas, 490, formatTime(elapsed_));
    drawMetricValue(canvas, 605, std::to_string(completed_.size()) + "/8");
}

void SpiderGame::drawMetricValue(Canvas& canvas, float x, const std::string& value) {
    Paint valuePaint;
    valuePaint.setStyle(Paint::Style::FILL);
    valuePaint.setColor(Color(247, 241, 227));
    valuePaint.setTextSize(21);
    useUiFont(valuePaint, 700);
    canvas.drawText(value, x, 49, valuePaint);
}

void SpiderGame::ensureHeaderChrome(Canvas& canvas) {
    if (!usePictureCache_) return;
    // Button 5 is the stock control and is drawn dynamically in drawStock().
    // Including it here rebuilt the entire header raster twice per deal even
    // though no header pixel changed.
    const int activePress = buttonPressTime_ > 0.0f &&
                            (pressedButton_ == 1 || pressedButton_ == 2 || pressedButton_ == 4)
        ? pressedButton_ : 0;
    const std::string signature =
        std::to_string(difficulty_) + "|" +
        std::to_string(activePress) + "|" +
        std::to_string(completed_.size());
    if (headerChromePicture_ && signature == headerChromeSignature_) return;
    headerChromeSignature_ = signature;
    headerChromePicture_ = canvas.recordPicture([this](Canvas& recording) {
        drawHeaderChromeContents(recording);
    });
}

void SpiderGame::drawHeaderChromeContents(Canvas& canvas) {
    drawSpiderMark(canvas, 48, 51, 43, Color(218, 180, 111));
    Paint title;
    title.setStyle(Paint::Style::FILL);
    title.setColor(Color(248, 243, 230));
    title.setTextSize(25);
    title.setLetterSpacing(1.0f);
    useUiFont(title, 760);
    canvas.drawText("SPIDER", 82, 25, title);
    Paint sub = title;
    sub.setTextSize(11.5f);
    sub.setLetterSpacing(1.35f);
    sub.setColor(Color(218, 197, 153));
    canvas.drawText("SOLITAIRE  -  CLASSIC TABLE", 83, 58, sub);

    drawMetricLabel(canvas, 260, "SCORE");
    drawMetricLabel(canvas, 375, "MOVES");
    drawMetricLabel(canvas, 490, "TIME");
    drawMetricLabel(canvas, 605, "RUNS");

    drawButton(canvas, newButton(), "NEW DEAL", true, 1, true);
    drawButton(canvas, difficultyButton(),
               std::to_string(difficulty_) + " SUIT" + (difficulty_ > 1 ? "S" : ""),
               true, 2, false);
    drawButton(canvas, undoButton(), "UNDO", false, 3, false);
    drawButton(canvas, hintButton(), "HINT", true, 4, false);

    Paint runLabel;
    runLabel.setStyle(Paint::Style::FILL);
    runLabel.setColor(Color(225, 209, 174));
    runLabel.setTextSize(13.5f);
    runLabel.setLetterSpacing(1.0f);
    useUiFont(runLabel, 700);
    canvas.drawText("COMPLETED", 44, 133, runLabel);
    for (int i = 0; i < 8; ++i) {
        const float x = 146.0f + i * 42.0f;
        Paint slot;
        slot.setStyle(Paint::Style::FILL);
        slot.setColor(i < static_cast<int>(completed_.size())
                      ? Color(123, 50, 68, 170)
                      : Color(19, 23, 38, 190));
        canvas.drawRoundRect(RectF(x, 124, 32, 30), 7, slot);
        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeWidth(1);
        border.setColor(i < static_cast<int>(completed_.size())
                        ? Color(223, 184, 111, 210)
                        : Color(139, 129, 112, 52));
        canvas.drawRoundRect(RectF(x, 124, 32, 30), 7, border);
        if (i < static_cast<int>(completed_.size()))
            drawSuit(canvas, completed_[i], x + 16, 139, 7.5f, Color(236, 209, 157));
    }

    Paint rule;
    rule.setStyle(Paint::Style::FILL);
    rule.setColor(Color(226, 218, 198));
    rule.setTextSize(13.5f);
    rule.setLetterSpacing(0.15f);
    useUiFont(rule, 580);
    canvas.drawText("Build same-suit runs from King to Ace", 760, 133, rule);
}

void SpiderGame::drawMetricLabel(Canvas& canvas, float x, const std::string& label) {
    Paint labelPaint;
    labelPaint.setStyle(Paint::Style::FILL);
    labelPaint.setColor(Color(215, 199, 165));
    labelPaint.setTextSize(13.0f);
    labelPaint.setLetterSpacing(0.8f);
    useUiFont(labelPaint, 650);
    canvas.drawText(label, x, 27, labelPaint);
}

void SpiderGame::drawButton(Canvas& canvas, const RectF& rect, const std::string& label,
                bool enabled, int id, bool primary) {
    // Touch has no hover. Reporting hover=false here keeps buttons from
    // flickering the "highlighted" style on every finger movement.
    const bool hover = enabled && !touchInputMode_ && inside(rect, pointerX_, pointerY_);
    const bool pressed = enabled && pressedButton_ == id && buttonPressTime_ > 0.0f;
    const float scale = pressed ? 0.97f : hover ? 1.018f : 1.0f;
    const float cx = rect.getX() + rect.getWidth() * 0.5f;
    const float cy = rect.getY() + rect.getHeight() * 0.5f;
    canvas.save();
    canvas.translate(cx, cy);
    canvas.scale(scale, scale);
    canvas.translate(-cx, -cy);
    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    if (primary) {
        fill.setLinearGradient(rect.getX(), rect.getY(), rect.getX(), rect.getY() + rect.getHeight(),
                               hover ? Color(242, 211, 151) : Color(222, 183, 112),
                               hover ? Color(204, 157, 83) : Color(179, 132, 69));
    } else {
        fill.setColor(enabled ? (hover ? Color(45, 43, 54, 245) : Color(24, 27, 42, 230))
                              : Color(17, 19, 30, 160));
    }
    canvas.drawRoundRect(rect, 10, fill);
    Paint border;
    border.setStyle(Paint::Style::STROKE);
    border.setStrokeWidth(1);
    border.setColor(primary ? Color(248, 221, 166, hover ? 230 : 150)
                            : enabled ? Color(185, 159, 112, hover ? 190 : 82)
                                      : Color(103, 98, 91, 42));
    canvas.drawRoundRect(rect, 10, border);
    Paint text;
    text.setStyle(Paint::Style::FILL);
    text.setColor(primary ? Color(35, 27, 24) : enabled ? Color(238, 230, 212) : Color(102, 99, 96));
    text.setTextSize(14.0f);
    text.setLetterSpacing(0.45f);
    text.setTextAlign(Paint::TextAlign::CENTER);
    text.setTextBaseline(Paint::TextBaseline::MIDDLE);
    useUiFont(text, 700);
    canvas.drawText(label, rect.getX() + rect.getWidth() * 0.5f,
                    rect.getY() + rect.getHeight() * 0.5f, text);
    canvas.restore();
}

void SpiderGame::drawColumnBase(Canvas& canvas, int col) {
    const auto ys = columnPositions(col);
    if (columns_[col].empty()) drawEmptySlot(canvas, col);
    for (int i = 0; i < static_cast<int>(columns_[col].size()); ++i) {
        if (col == selectedColumn_ && i >= selectedIndex_) continue;
        if (isCardAnimating(columns_[col][i].id)) continue;
        drawCard(canvas, columns_[col][i], columnX(col), ys[i], false, false, 1.0f);
    }
}

void SpiderGame::drawTableauBase(Canvas& canvas) {
    for (int col = 0; col < 10; ++col) {
        drawColumnBase(canvas, col);
    }
}

void SpiderGame::drawColumns(Canvas& canvas) {
    if (usePictureCache_ && !useCardAtlas_) {
        for (int col = 0; col < 10; ++col) {
            auto& picture = columnPictures_[static_cast<std::size_t>(col)];
            auto& dirty = columnPictureDirty_[static_cast<std::size_t>(col)];
            if (dirty || !picture) {
                picture = canvas.recordPicture([this, col](Canvas& recording) {
                    drawColumnBase(recording, col);
                });
                dirty = false;
            }
            if (picture) replayPicture(canvas, *picture);
            else drawColumnBase(canvas, col);
        }
    } else {
        drawTableauBase(canvas);
    }

    if (selectedColumn_ >= 0) {
        const auto ys = columnPositions(selectedColumn_);
        const float x = dragging_ ? pointerX_ - dragOffsetX_ : columnX(selectedColumn_);
        const float y = dragging_ ? pointerY_ - dragOffsetY_ : ys[selectedIndex_];
        for (int i = selectedIndex_; i < static_cast<int>(columns_[selectedColumn_].size()); ++i) {
            if (isCardAnimating(columns_[selectedColumn_][i].id)) continue;
            const float cardY = y + (ys[i] - ys[selectedIndex_]);
            if (dragging_) {
                // The detailed face uses multiple gradients, mirrored
                // labels and suit paths. Rebuilding all of that geometry
                // at every finger position dominates an older GLES GPU.
                // The motion card keeps rank/suit legible with a compact
                // set of primitives and is already used by deal/landing
                // animations.
                drawMotionCard(canvas, columns_[selectedColumn_][i],
                               x, cardY, 0.96f);
            } else {
                drawCard(canvas, columns_[selectedColumn_][i], x, cardY,
                         true, false, 1.0f);
            }
        }
    }

    if (!dragging_ && hintTime_ > 0 && hintSource_ >= 0 && hintSource_ != selectedColumn_) {
        const auto ys = columnPositions(hintSource_);
        for (int i = hintIndex_; i < static_cast<int>(columns_[hintSource_].size()); ++i) {
            if (isCardAnimating(columns_[hintSource_][i].id)) continue;
            drawCard(canvas, columns_[hintSource_][i], columnX(hintSource_), ys[i], false, true, 1.0f);
        }
    }

    for (const CardMotion& motion : motions_) {
        const float local = (motionTime_ - motion.delay) / std::max(0.001f, motion.duration);
        if (local < 0.0f || local >= 1.0f) continue;
        const float progress = clamp01(local);
        const float t = easeOutCubic(progress);
        const float x = motion.fromX + (motion.toX - motion.fromX) * t;
        float y = motion.fromY + (motion.toY - motion.fromY) * t;
        if (motion.kind == MotionKind::Deal) {
            y -= std::sin(progress * 3.14159265f) * motion::kDealArcHeight;
        }
        // Keep moving cards opaque. Per-card fading assigns ten different
        // image tints during a deal, which prevents the GLES image commands
        // from batching on older drivers and turns ten cheap sprites into ten
        // expensive state transitions.
        drawMotionCard(canvas, motion.card, x, y, 1.0f);
    }

    for (const CardMotion& motion : completionMotions_) {
        const float local = (completionMotionTime_ - motion.delay) /
                            std::max(0.001f, motion.duration);
        const float progress = clamp01(local);
        const float t = easeInOutCubic(progress);
        const float startX = motion.fromX + CARD_W * 0.5f;
        const float startY = motion.fromY + CARD_H * 0.5f;
        const float centerX = startX + (motion.toX - startX) * t;
        const float centerY = startY + (motion.toY - startY) * t
                              - std::sin(progress * 3.14159265f) * 24.0f;
        const float scale = 1.0f - 0.72f * t;
        const float alpha = 1.0f - clamp01((progress - 0.72f) / 0.28f);
        canvas.save();
        canvas.translate(centerX, centerY);
        canvas.scale(scale, scale);
        drawMotionCard(canvas, motion.card, -CARD_W * 0.5f, -CARD_H * 0.5f, alpha);
        canvas.restore();
    }

    if (hintTime_ > 0 && hintDest_ >= 0 && hintDest_ < 10) {
        Paint h;
        h.setStyle(Paint::Style::STROKE);
        h.setStrokeWidth(3);
        h.setColor(Color(255, 210, 91, static_cast<int>(150 + 80 * std::sin(phase_ * 5.0f))));
        const auto ys = columnPositions(hintDest_);
        const float y = ys.empty() ? TABLE_Y : ys.back();
        canvas.drawRoundRect(RectF(columnX(hintDest_) - 4, y - 4, CARD_W + 8, CARD_H + 8), 14, h);
    }
}

void SpiderGame::drawEmptySlot(Canvas& canvas, int col) {
    const float x = columnX(col);
    if (usePictureCache_) {
        if (!emptySlotPicture_) {
            emptySlotPicture_ = canvas.recordPicture([this](Canvas& recording) {
                drawEmptySlotContents(recording, 0.0f, 0.0f);
            });
        }
        if (emptySlotPicture_) {
            canvas.save();
            canvas.translate(x, TABLE_Y);
            replayPicture(canvas, *emptySlotPicture_, /*smallSprite=*/true);
            canvas.restore();
            return;
        }
    }
    drawEmptySlotContents(canvas, x, TABLE_Y);
}

void SpiderGame::drawEmptySlotContents(Canvas& canvas, float x, float y) {
    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(8, 11, 22, 92));
    canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 11, fill);
    Paint stroke;
    stroke.setStyle(Paint::Style::STROKE);
    stroke.setStrokeWidth(1.2f);
    stroke.setColor(Color(180, 158, 118, 52));
    canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 11, stroke);
    drawSpiderMark(canvas, x + CARD_W * 0.5f, y + CARD_H * 0.5f, 30, Color(176, 150, 103, 38));
}

void SpiderGame::drawCard(Canvas& canvas, const Card& card, float x, float y, bool selected, bool hinted, float alpha) {
    if (card.faceUp) drawFaceUp(canvas, card, x, y, selected, hinted, alpha);
    else drawFaceDown(canvas, x, y, alpha);
}

void SpiderGame::drawMotionCard(Canvas& canvas, const Card& card, float x, float y, float alpha) {
    if (useCardAtlas_ && cardAtlas_) {
        drawAtlasFace(canvas, card, x, y, false, false, alpha);
        return;
    }
    drawCardShadow(canvas, x, y, false, alpha);
    Paint paper;
    paper.setStyle(Paint::Style::FILL);
    paper.setColor(Color(248, 243, 232, static_cast<int>(255 * alpha)));
    canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 10, paper);
    Paint border;
    border.setStyle(Paint::Style::STROKE);
    border.setStrokeWidth(1.1f);
    border.setColor(Color(196, 170, 124, static_cast<int>(220 * alpha)));
    canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 10, border);

    const bool red = card.suit == Suit::Heart || card.suit == Suit::Diamond;
    const Color ink = red ? Color(174, 48, 64, static_cast<int>(255 * alpha))
                          : Color(28, 31, 43, static_cast<int>(255 * alpha));
    Paint rank;
    rank.setStyle(Paint::Style::FILL);
    rank.setColor(ink);
    rank.setTextSize(card.rank == 10 ? 22 : 26);
    useUiFont(rank, 780);
    canvas.drawText(rankText(card.rank), x + 10, y + 7, rank);
    drawSuit(canvas, card.suit, x + CARD_W * 0.5f, y + CARD_H * 0.55f, 18.0f, ink);
}

void SpiderGame::drawCardShadow(Canvas& canvas, float x, float y, bool selected, float alpha) const {
    Paint ambient;
    ambient.setStyle(Paint::Style::FILL);
    ambient.setColor(Color(1, 18, 12, static_cast<int>((selected ? 54 : 26) * alpha)));
    canvas.drawRoundRect(RectF(x - 0.5f, y + 1, CARD_W + 1, CARD_H + 1.5f), 11, ambient);

    Paint key;
    key.setStyle(Paint::Style::FILL);
    key.setColor(Color(1, 12, 8, static_cast<int>((selected ? 92 : 46) * alpha)));
    canvas.drawRoundRect(RectF(x + 0.5f, y + (selected ? 4 : 2), CARD_W - 1, CARD_H), 10, key);
}

void SpiderGame::drawFaceUp(Canvas& canvas, const Card& card, float x, float y, bool selected, bool hinted, float alpha) {
    if (useCardAtlas_ && cardAtlas_) {
        drawAtlasFace(canvas, card, x, y, selected, hinted, alpha);
        return;
    }
    const float lift = selected ? -4.0f : 0.0f;
    y += lift;
    drawCardShadow(canvas, x, y, selected, alpha);
    Paint paper;
    paper.setStyle(Paint::Style::FILL);
    paper.setLinearGradient(x, y, x + CARD_W, y + CARD_H,
                            Color(255, 253, 246, static_cast<int>(255 * alpha)),
                            Color(233, 226, 213, static_cast<int>(255 * alpha)));
    canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 10, paper);

    Paint topSheen;
    topSheen.setStyle(Paint::Style::FILL);
    topSheen.setLinearGradient(x, y, x, y + 42,
                               Color(255, 255, 255, static_cast<int>(145 * alpha)),
                               Color(255, 255, 255, 0));
    canvas.drawRoundRect(RectF(x + 3, y + 3, CARD_W - 6, 38), 7, topSheen);

    Paint border;
    border.setStyle(Paint::Style::STROKE);
    border.setStrokeWidth(selected || hinted ? 2.2f : 0.8f);
    border.setColor(selected ? Color(232, 188, 103, 245) : hinted ? Color(238, 190, 96, 230)
                             : Color(178, 168, 151, 210));
    canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 10, border);
    const bool red = card.suit == Suit::Heart || card.suit == Suit::Diamond;
    const Color ink = red ? Color(174, 48, 64, static_cast<int>(255 * alpha))
                          : Color(28, 31, 43, static_cast<int>(255 * alpha));
    Paint text;
    text.setStyle(Paint::Style::FILL);
    text.setColor(ink);
    text.setTextSize(card.rank == 10 ? 22 : 26);
    useUiFont(text, 780);
    canvas.drawText(rankText(card.rank), x + 10, y + 7, text);
    drawSuit(canvas, card.suit, x + 19, y + 45, 8.1f, ink);
    drawSuit(canvas, card.suit, x + CARD_W * 0.5f, y + 79, 17.2f, ink);

    canvas.save();
    canvas.translate(x + CARD_W - 9, y + CARD_H - 8);
    canvas.rotate(3.14159265f);
    Paint lower = text;
    lower.setTextSize(card.rank == 10 ? 18 : 20);
    canvas.drawText(rankText(card.rank), 0, 0, lower);
    drawSuit(canvas, card.suit, 8, 31, 7.0f, ink);
    canvas.restore();
}

void SpiderGame::drawFaceDown(Canvas& canvas, float x, float y, float alpha) {
    if (useCardAtlas_ && cardAtlas_) {
        drawAtlasBack(canvas, x, y, alpha);
        return;
    }
    // Every hidden-card slot has the exact same visual. Recording it once
    // and replaying at each position collapses ~40 primitives per card
    // (paper, inset, lattice, borders, crest, spider mark) into a single
    // rasterized blit. The initial deal alone has ~44 hidden cards, so
    // this dominates the tableauPicture record cost.
    if (usePictureCache_ && alpha >= 0.999f) {
        if (!faceDownPicture_) {
            faceDownPicture_ = canvas.recordPicture([this](Canvas& recording) {
                drawFaceDownContents(recording, 0.0f, 0.0f, 1.0f);
            });
        }
        if (faceDownPicture_) {
            canvas.save();
            canvas.translate(x, y);
            replayPicture(canvas, *faceDownPicture_, /*smallSprite=*/true);
            canvas.restore();
            return;
        }
    }
    drawFaceDownContents(canvas, x, y, alpha);
}

void SpiderGame::drawFaceDownContents(Canvas& canvas, float x, float y, float alpha) {
    drawCardShadow(canvas, x, y, false, alpha);
    Paint paper;
    paper.setStyle(Paint::Style::FILL);
    paper.setColor(Color(190, 177, 153, static_cast<int>(255 * alpha)));
    canvas.drawRoundRect(RectF(x, y, CARD_W, CARD_H), 10, paper);
    Paint inset;
    inset.setStyle(Paint::Style::FILL);
    inset.setLinearGradient(x + 2.5f, y + 2.5f, x + CARD_W - 2.5f, y + CARD_H - 2.5f,
                            Color(112, 38, 60, static_cast<int>(255 * alpha)),
                            Color(49, 20, 43, static_cast<int>(255 * alpha)));
    canvas.drawRoundRect(RectF(x + 2.5f, y + 2.5f, CARD_W - 5, CARD_H - 5), 8, inset);

    Paint lattice;
    lattice.setStyle(Paint::Style::FILL);
    lattice.setColor(Color(235, 198, 128, static_cast<int>(54 * alpha)));
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 5; ++col) {
            const float px = x + 12 + col * 18.0f + (row % 2 ? 8.0f : 0.0f);
            const float py = y + 14 + row * 15.0f;
            if (px > x + CARD_W - 8) continue;
            canvas.drawPolygon(std::vector<PointF>{{px, py - 3}, {px + 3, py},
                                                   {px, py + 3}, {px - 3, py}}, lattice);
        }
    }

    Paint border;
    border.setStyle(Paint::Style::STROKE);
    border.setStrokeWidth(1.1f);
    border.setColor(Color(226, 190, 119, static_cast<int>(190 * alpha)));
    canvas.drawRoundRect(RectF(x + 4, y + 4, CARD_W - 8, CARD_H - 8), 7, border);
    Paint innerBorder = border;
    innerBorder.setColor(Color(248, 224, 174, static_cast<int>(80 * alpha)));
    canvas.drawRoundRect(RectF(x + 7, y + 7, CARD_W - 14, CARD_H - 14), 5, innerBorder);

    const float cx = x + CARD_W * 0.5f;
    const float cy = y + CARD_H * 0.5f;
    Paint crest;
    crest.setStyle(Paint::Style::FILL);
    crest.setColor(Color(232, 195, 124, static_cast<int>(230 * alpha)));
    canvas.drawPolygon(std::vector<PointF>{{cx, cy - 31}, {cx + 24, cy},
                                           {cx, cy + 31}, {cx - 24, cy}}, crest);
    Paint crestInset = crest;
    crestInset.setColor(Color(62, 24, 48, static_cast<int>(255 * alpha)));
    canvas.drawPolygon(std::vector<PointF>{{cx, cy - 27}, {cx + 20, cy},
                                           {cx, cy + 27}, {cx - 20, cy}}, crestInset);
    drawSpiderMark(canvas, cx, cy, 25, Color(242, 207, 140, static_cast<int>(255 * alpha)));
}

void SpiderGame::drawSuit(Canvas& canvas, Suit suit, float cx, float cy, float size, const Color& color) const {
    Paint p;
    p.setStyle(Paint::Style::FILL);
    p.setColor(color);
    if (suit == Suit::Diamond) {
        canvas.drawPolygon(std::vector<PointF>{{cx, cy - size * 1.22f}, {cx + size * 0.82f, cy},
                                               {cx, cy + size * 1.22f}, {cx - size * 0.82f, cy}}, p);
        return;
    }

    if (suit == Suit::Heart) {
        Path heart;
        heart.moveTo(cx, cy + size * 1.15f);
        heart.cubicTo(cx - size * 0.20f, cy + size * 0.80f,
                      cx - size * 1.10f, cy + size * 0.25f,
                      cx - size * 1.10f, cy - size * 0.38f);
        heart.cubicTo(cx - size * 1.10f, cy - size * 1.08f,
                      cx - size * 0.24f, cy - size * 1.24f,
                      cx, cy - size * 0.58f);
        heart.cubicTo(cx + size * 0.24f, cy - size * 1.24f,
                      cx + size * 1.10f, cy - size * 1.08f,
                      cx + size * 1.10f, cy - size * 0.38f);
        heart.cubicTo(cx + size * 1.10f, cy + size * 0.25f,
                      cx + size * 0.20f, cy + size * 0.80f,
                      cx, cy + size * 1.15f);
        heart.close();
        canvas.drawPath(heart, p);
        return;
    }

    if (suit == Suit::Spade) {
        Path spade;
        spade.moveTo(cx, cy - size * 1.20f);
        spade.cubicTo(cx - size * 0.18f, cy - size * 0.86f,
                      cx - size * 1.05f, cy - size * 0.38f,
                      cx - size * 1.05f, cy + size * 0.20f);
        spade.cubicTo(cx - size * 1.05f, cy + size * 0.75f,
                      cx - size * 0.44f, cy + size * 0.92f,
                      cx - size * 0.17f, cy + size * 0.58f);
        spade.lineTo(cx - size * 0.40f, cy + size * 1.18f);
        spade.lineTo(cx + size * 0.40f, cy + size * 1.18f);
        spade.lineTo(cx + size * 0.17f, cy + size * 0.58f);
        spade.cubicTo(cx + size * 0.44f, cy + size * 0.92f,
                      cx + size * 1.05f, cy + size * 0.75f,
                      cx + size * 1.05f, cy + size * 0.20f);
        spade.cubicTo(cx + size * 1.05f, cy - size * 0.38f,
                      cx + size * 0.18f, cy - size * 0.86f,
                      cx, cy - size * 1.20f);
        spade.close();
        canvas.drawPath(spade, p);
        return;
    }

    Path club;
    club.moveTo(cx, cy - size * 1.16f);
    club.cubicTo(cx - size * 0.42f, cy - size * 1.16f,
                 cx - size * 0.68f, cy - size * 0.86f,
                 cx - size * 0.68f, cy - size * 0.52f);
    club.cubicTo(cx - size * 0.68f, cy - size * 0.34f,
                 cx - size * 0.60f, cy - size * 0.18f,
                 cx - size * 0.46f, cy - size * 0.06f);
    club.cubicTo(cx - size * 0.62f, cy - size * 0.20f,
                 cx - size * 0.84f, cy - size * 0.24f,
                 cx - size * 1.01f, cy - size * 0.14f);
    club.cubicTo(cx - size * 1.29f, cy + size * 0.02f,
                 cx - size * 1.29f, cy + size * 0.54f,
                 cx - size * 1.04f, cy + size * 0.72f);
    club.cubicTo(cx - size * 0.82f, cy + size * 0.89f,
                 cx - size * 0.48f, cy + size * 0.82f,
                 cx - size * 0.33f, cy + size * 0.58f);
    club.lineTo(cx - size * 0.42f, cy + size * 1.16f);
    club.lineTo(cx + size * 0.42f, cy + size * 1.16f);
    club.lineTo(cx + size * 0.33f, cy + size * 0.58f);
    club.cubicTo(cx + size * 0.48f, cy + size * 0.82f,
                 cx + size * 0.82f, cy + size * 0.89f,
                 cx + size * 1.04f, cy + size * 0.72f);
    club.cubicTo(cx + size * 1.29f, cy + size * 0.54f,
                 cx + size * 1.29f, cy + size * 0.02f,
                 cx + size * 1.01f, cy - size * 0.14f);
    club.cubicTo(cx + size * 0.84f, cy - size * 0.24f,
                 cx + size * 0.62f, cy - size * 0.20f,
                 cx + size * 0.46f, cy - size * 0.06f);
    club.cubicTo(cx + size * 0.60f, cy - size * 0.18f,
                 cx + size * 0.68f, cy - size * 0.34f,
                 cx + size * 0.68f, cy - size * 0.52f);
    club.cubicTo(cx + size * 0.68f, cy - size * 0.86f,
                 cx + size * 0.42f, cy - size * 1.16f,
                 cx, cy - size * 1.16f);
    club.close();
    canvas.drawPath(club, p);
}

void SpiderGame::drawStockBack(Canvas& canvas, float x, float y) {
    if (useCardAtlas_ && cardAtlas_) {
        constexpr int backIndex = 52;
        constexpr int atlasColumns = 8;
        constexpr int cellW = 112;
        constexpr int cellH = 144;
        drawAtlasSprite(canvas,
                        RectF(static_cast<float>((backIndex % atlasColumns) * cellW),
                              static_cast<float>((backIndex / atlasColumns) * cellH),
                              98, 136),
                        RectF(x, y, 70, 98), Color::WHITE, 1.0f);
        return;
    }
    // Cache the static back once and translate each copy: this static ornament
    // has ~50 draws per back and 3 backs stack up every frame. Recording it
    // to a Picture once collapses ~150 draws and 800 tessellated verts into
    // a single rasterized replay per copy.
    if (usePictureCache_ && !stockBackPicture_) {
        stockBackPicture_ = canvas.recordPicture([this](Canvas& recording) {
            drawStockBackContents(recording, 0.0f, 0.0f);
        });
    }
    if (usePictureCache_ && stockBackPicture_) {
        canvas.save();
        canvas.translate(x, y);
        replayPicture(canvas, *stockBackPicture_, /*smallSprite=*/true);
        canvas.restore();
        return;
    }
    drawStockBackContents(canvas, x, y);
}

void SpiderGame::drawStockBackContents(Canvas& canvas, float x, float y) {
    constexpr float w = 70.0f;
    constexpr float h = 98.0f;
    Paint shadow;
    shadow.setStyle(Paint::Style::FILL);
    shadow.setColor(Color(1, 14, 9, 48));
    canvas.drawRoundRect(RectF(x + 0.5f, y + 2, w - 1, h), 8, shadow);

    Paint edge;
    edge.setStyle(Paint::Style::FILL);
    edge.setColor(Color(190, 177, 153));
    canvas.drawRoundRect(RectF(x, y, w, h), 8, edge);

    Paint back;
    back.setStyle(Paint::Style::FILL);
    back.setLinearGradient(x + 2, y + 2, x + w - 2, y + h - 2,
                           Color(112, 38, 60), Color(49, 20, 43));
    canvas.drawRoundRect(RectF(x + 2, y + 2, w - 4, h - 4), 6, back);

    Paint lattice;
    lattice.setStyle(Paint::Style::FILL);
    lattice.setColor(Color(235, 198, 128, 58));
    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 4; ++col) {
            const float px = x + 10 + col * 16.0f + (row % 2 ? 7.0f : 0.0f);
            const float py = y + 12 + row * 14.5f;
            if (px > x + w - 7) continue;
            canvas.drawPolygon(std::vector<PointF>{{px, py - 2.5f}, {px + 2.5f, py},
                                                   {px, py + 2.5f}, {px - 2.5f, py}}, lattice);
        }
    }

    Paint border;
    border.setStyle(Paint::Style::STROKE);
    border.setStrokeWidth(1.0f);
    border.setColor(Color(231, 196, 127, 210));
    canvas.drawRoundRect(RectF(x + 4, y + 4, w - 8, h - 8), 5, border);
    Paint inner = border;
    inner.setColor(Color(248, 224, 174, 92));
    canvas.drawRoundRect(RectF(x + 7, y + 7, w - 14, h - 14), 4, inner);
    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;
    Paint crest;
    crest.setStyle(Paint::Style::FILL);
    crest.setColor(Color(232, 195, 124, 230));
    canvas.drawPolygon(std::vector<PointF>{{cx, cy - 23}, {cx + 17, cy},
                                           {cx, cy + 23}, {cx - 17, cy}}, crest);
    Paint crestInset = crest;
    crestInset.setColor(Color(62, 24, 48));
    canvas.drawPolygon(std::vector<PointF>{{cx, cy - 20}, {cx + 14, cy},
                                           {cx, cy + 20}, {cx - 14, cy}}, crestInset);
    drawSpiderMark(canvas, cx, cy, 19, Color(242, 207, 140));
}

void SpiderGame::drawStock(Canvas& canvas) {
    const RectF r = stockRect();
    const int deals = static_cast<int>(stock_.size() / 10);
    const bool hover = deals > 0 && !touchInputMode_ && inside(r, pointerX_, pointerY_);
    const bool pressed = pressedButton_ == 5 && buttonPressTime_ > 0.0f;
    const float stockScale = pressed ? 0.97f : hover ? 1.025f : 1.0f;
    const float cx = r.getX() + r.getWidth() * 0.5f;
    const float cy = r.getY() + r.getHeight() * 0.5f;
    canvas.save();
    canvas.translate(cx, cy);
    canvas.scale(stockScale, stockScale);
    canvas.translate(-cx, -cy);
    if (deals > 0) {
        for (int i = std::min(3, deals - 1); i >= 0; --i)
            drawStockBack(canvas, r.getX() + 7 + i * 5.5f, r.getY() + 1 + i * 2.2f);
        Paint badge;
        badge.setStyle(Paint::Style::FILL);
        badge.setColor(Color(224, 184, 110));
        canvas.drawCircle(r.getX() + r.getWidth() - 5, r.getY() + 10, 12 + dealPulse_ * 2, badge);
        Paint count;
        count.setStyle(Paint::Style::FILL);
        count.setColor(Color(39, 29, 25));
        count.setTextSize(14);
        count.setTextAlign(Paint::TextAlign::CENTER);
        count.setTextBaseline(Paint::TextBaseline::MIDDLE);
        useUiFont(count, 800);
        canvas.drawText(std::to_string(deals), r.getX() + r.getWidth() - 5, r.getY() + 10, count);
    } else {
        Paint empty;
        empty.setStyle(Paint::Style::STROKE);
        empty.setStrokeWidth(1.4f);
        empty.setColor(Color(185, 159, 112, 70));
        canvas.drawRoundRect(RectF(r.getX() + 8, r.getY() + 1, 72, 98), 9, empty);
    }
    Paint label;
    label.setStyle(Paint::Style::FILL);
    label.setColor(hover ? Color(249, 222, 169) : Color(222, 201, 160));
    label.setTextSize(13);
    label.setLetterSpacing(0.8f);
    label.setTextAlign(Paint::TextAlign::CENTER);
    useUiFont(label, 700);
    canvas.drawText(deals > 0 ? "DEAL" : "EMPTY", cx, r.getY() + 135, label);
    if (hintTime_ > 0 && hintDest_ == 10) {
        Paint h;
        h.setStyle(Paint::Style::STROKE);
        h.setStrokeWidth(3);
        h.setColor(Color(232, 188, 103, 220));
        canvas.drawRoundRect(RectF(r.getX() + 3, r.getY() - 2, 84, 112), 12, h);
    }
    canvas.restore();
}

void SpiderGame::drawToast(Canvas& canvas) {
    if (toastTime_ <= 0 || won_) return;
    const float age = std::max(0.0f, toastDuration_ - toastTime_);
    const float enter = easeOutQuint(age / 0.25f);
    const float exit = clamp01(toastTime_ / 0.30f);
    const float alpha = std::min(enter, exit);
    Paint text;
    text.setStyle(Paint::Style::FILL);
    text.setTextSize(16.5f);
    text.setTextAlign(Paint::TextAlign::CENTER);
    text.setTextBaseline(Paint::TextBaseline::MIDDLE);
    useUiFont(text, 680);
    if (measuredToast_ != toast_) {
        measuredToast_ = toast_;
        measuredToastWidth_ = canvas.measureTextMetrics(toast_, text).width;
    }
    const float w = measuredToastWidth_ + 64;
    const float y = 792.0f + (1.0f - enter) * 16.0f;
    const RectF rect((DESIGN_W - w) * 0.5f, y, w, 48);
    Paint bg;
    bg.setStyle(Paint::Style::FILL);
    bg.setColor(Color(8, 10, 18, static_cast<int>(232 * alpha)));
    canvas.drawRoundRect(rect, 24, bg);
    Paint border;
    border.setStyle(Paint::Style::STROKE);
    border.setStrokeWidth(1);
    border.setColor(Color(211, 174, 107, static_cast<int>(145 * alpha)));
    canvas.drawRoundRect(rect, 24, border);
    text.setColor(Color(239, 232, 216, static_cast<int>(255 * alpha)));
    canvas.drawText(toast_, DESIGN_W * 0.5f, y + 24, text);
}

void SpiderGame::drawWin(Canvas& canvas) {
    Paint veil;
    veil.setStyle(Paint::Style::FILL);
    veil.setColor(Color(1, 18, 22, 220));
    canvas.drawRect(RectF(0, 96, DESIGN_W, DESIGN_H - 96), veil);
    const float pulse = 1.0f + std::sin(phase_ * 3.0f) * 0.06f;
    drawSpiderMark(canvas, DESIGN_W * 0.5f, 320, 112 * pulse, Color(104, 241, 193));
    Paint title;
    title.setStyle(Paint::Style::FILL);
    title.setColor(Color(242, 252, 247));
    title.setTextSize(42);
    title.setTextAlign(Paint::TextAlign::CENTER);
    useUiFont(title, 800);
    canvas.drawText("MASTERFUL!", DESIGN_W * 0.5f, 415, title);
    Paint sub = title;
    sub.setTextSize(17);
    sub.setColor(Color(151, 218, 197));
    useUiFont(sub, 500);
    canvas.drawText("Eight complete runs cleared in " + formatTime(elapsed_), DESIGN_W * 0.5f, 475, sub);
    sub.setTextSize(14);
    sub.setColor(Color(235, 208, 111));
    canvas.drawText("Final score  " + std::to_string(score_) + "   |   Click anywhere for a new deal", DESIGN_W * 0.5f, 520, sub);
}

} // namespace spider
