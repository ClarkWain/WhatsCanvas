#pragma once
#include "Rules.h"
#include <atomic>
#include <chrono>

namespace chess {
enum class Difficulty { Easy, Medium, Hard };
struct SearchLimits {
    int depth;
    std::uint64_t nodes;
    std::chrono::milliseconds time;
};
struct SearchResult {
    Move move;
    int completedDepth = 0;
    std::uint64_t nodes = 0;
    double milliseconds = 0;
};
SearchLimits limitsFor(Difficulty difficulty);
// No Canvas or UI access. Cancellation is checked at each search node.
SearchResult chooseMove(const Position& position, Difficulty difficulty, std::uint32_t seed,
                        const std::atomic_bool& cancel);
} // namespace chess
