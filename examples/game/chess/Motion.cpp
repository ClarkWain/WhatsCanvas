#include "Motion.h"
#include <algorithm>

namespace chess {
MotionStep makeStep(const Position& before, const Position& after, Move move, bool reverse) {
    MotionStep step{before, after, move, reverse};
    const Position& original = reverse ? after : before;
    const Move forward = reverse ? Move{move.to, move.from} : move;
    step.victim = capturedSquare(original, forward);
    if (step.victim >= 0) step.captured = original.board[step.victim];
    step.companion = castlingRookMove(original, forward);
    if (reverse && step.companion.valid()) std::swap(step.companion.from, step.companion.to);
    return step;
}
float motion::unit(double value) { return static_cast<float>(std::clamp(value, 0.0, 1.0)); }
float motion::smooth(float t) { t = unit(t); return t * t * (3 - 2 * t); }
double Transition::duration() const {
    return kind == TransitionKind::Opening ? motion::Entrance : steps.size() * motion::StepDuration;
}
bool Transition::active(double now) const { return serial != 0 && now < started + duration(); }
MotionSample Transition::sample(double now) const {
    if (steps.empty()) return {};
    const double elapsed = std::max(0.0, now - started);
    const int index = std::min(static_cast<int>(steps.size()) - 1, static_cast<int>(elapsed / motion::StepDuration));
    const double local = elapsed - index * motion::StepDuration;
    return {index, motion::smooth(motion::unit(local / motion::Travel)),
            motion::unit((local - motion::Travel) / motion::Landing), active(now)};
}
} // namespace chess
