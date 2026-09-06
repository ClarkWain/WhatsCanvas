#pragma once
#include "Rules.h"
#include <vector>

namespace xiangqi {
namespace motion {
constexpr double Travel = 0.32;
constexpr double Landing = 0.16;
constexpr double StepDuration = Travel + Landing;
constexpr double ReplyDelay = 0.72;
constexpr double Entrance = 0.60;
float unit(double value);
float smooth(float value);
}
enum class TransitionKind { Opening, Move, Undo };
struct MotionStep {
    Position before, after;
    Move move; // Visual direction; undo runs destination -> original square.
    bool reverse = false;
    // An interrupted move can reverse from its current point rather than snap
    // to the logical destination before beginning the undo animation.
    float startFile = -1, startRank = -1, restoredAlpha = 0;
};
struct MotionSample {
    int step = 0;
    float travel = 1;
    float landing = 1;
    bool active = false;
};
// Immutable position snapshots make rendering independent of live match history.
// In particular captured pieces remain visible until impact, and undo can replay
// both half-moves in reverse even though the rules model has already been restored.
struct Transition {
    std::uint64_t serial = 0;
    TransitionKind kind = TransitionKind::Opening;
    double started = 0;
    std::vector<MotionStep> steps;
    double duration() const;
    bool active(double now) const;
    MotionSample sample(double now) const;
};
} // namespace xiangqi
