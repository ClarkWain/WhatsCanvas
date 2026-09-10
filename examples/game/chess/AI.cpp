#include "AI.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace chess {
namespace {
constexpr int Mate = 100000;
int value(Kind kind) {
    constexpr int values[] = {0, 20000, 900, 500, 330, 320, 100};
    return values[static_cast<int>(kind)];
}
int evaluate(const Position& p) {
    int score = 0;
    for (int s = 0; s < 64; ++s) {
        const Piece piece = p.board[s];
        if (!piece) continue;
        int v = value(piece.kind);
        const int advance = piece.side == Side::White ? 7 - rankOf(s) : rankOf(s);
        const int center = 7 - std::abs(fileOf(s) * 2 - 7) - std::abs(rankOf(s) * 2 - 7);
        if (piece.kind == Kind::Pawn) v += advance * 9 + center * 2;
        if (piece.kind == Kind::Knight || piece.kind == Kind::Bishop) v += center * 6;
        if (piece.kind == Kind::King && advance < 2 && (fileOf(s) == 6 || fileOf(s) == 2)) v += 35;
        score += piece.side == p.turn ? v : -v;
    }
    return score;
}
void order(const Position& p, std::vector<Move>& moves, Move preferred = {}) {
    auto score = [&](Move m) {
        if (m == preferred) return Mate * 10;
        const int victim = capturedSquare(p, m);
        return (victim >= 0 ? 10 * value(p.board[victim].kind) - value(p.board[m.from].kind) : 0) + value(m.promotion);
    };
    std::stable_sort(moves.begin(), moves.end(), [&](Move a, Move b) { return score(a) > score(b); });
}
struct Search {
    const std::atomic_bool& cancel;
    SearchLimits limits;
    std::chrono::steady_clock::time_point deadline;
    std::uint64_t nodes = 0;
    bool stopped = false;
    std::vector<Position> path;

    int negamax(const Position& p, int depth, int alpha, int beta, int ply) {
        if (cancel.load(std::memory_order_relaxed) || ++nodes > limits.nodes
            || std::chrono::steady_clock::now() >= deadline) {
            stopped = true;
            return 0;
        }
        auto moves = legalMoves(p);
        if (moves.empty()) return inCheck(p, p.turn) ? -Mate + ply : 0;
        if (p.quietPlies >= 100 || insufficientMaterial(p)) return 0;
        for (const auto& previous : path) if (p.sameBoard(previous)) return 0;
        if (depth <= 0) return evaluate(p);
        order(p, moves);
        path.push_back(p);
        int best = -Mate;
        for (Move m : moves) {
            const int score = -negamax(p.after(m), depth - 1, -beta, -alpha, ply + 1);
            if (stopped) break;
            best = std::max(best, score);
            alpha = std::max(alpha, score);
            if (alpha >= beta) break;
        }
        path.pop_back();
        return best;
    }
};
} // namespace

SearchLimits limitsFor(Difficulty d) {
    switch (d) {
    case Difficulty::Easy: return {1, 1000, std::chrono::milliseconds(40)};
    case Difficulty::Medium: return {2, 18000, std::chrono::milliseconds(220)};
    default: return {4, 180000, std::chrono::milliseconds(1000)};
    }
}

SearchResult chooseMove(const Position& p, Difficulty d, std::uint32_t seed, const std::atomic_bool& cancel) {
    const auto start = std::chrono::steady_clock::now();
    SearchResult result;
    auto moves = legalMoves(p);
    if (moves.empty() || cancel.load()) return result;
    std::mt19937 random(seed);
    std::shuffle(moves.begin(), moves.end(), random);
    result.move = moves.front(); // Always retain a legal fallback if the budget expires.
    if (d == Difficulty::Easy) {
        // Deliberately shallow: random legal moves, with only a small capture bias.
        std::vector<double> weights;
        for (Move m : moves) weights.push_back(capturedSquare(p, m) >= 0 ? 3.0 : 1.0);
        result.move = moves[std::discrete_distribution<std::size_t>(weights.begin(), weights.end())(random)];
        result.nodes = moves.size();
        result.completedDepth = 1;
    } else {
        const auto limits = limitsFor(d);
        Search search{cancel, limits, start + limits.time, 0, false, {}};
        for (int depth = 1; depth <= limits.depth; ++depth) {
            order(p, moves, result.move);
            int best = -Mate * 2;
            Move bestMove = result.move;
            search.path = {p};
            for (Move m : moves) {
                const int score = -search.negamax(p.after(m), depth - 1, -Mate * 2, -best, 1);
                if (search.stopped) break;
                if (score > best) { best = score; bestMove = m; }
            }
            if (search.stopped) break; // Do not publish a partially searched iteration.
            result.move = bestMove;
            result.completedDepth = depth;
        }
        result.nodes = search.nodes;
    }
    result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}
} // namespace chess
