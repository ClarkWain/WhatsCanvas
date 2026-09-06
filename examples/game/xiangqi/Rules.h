#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace xiangqi {

enum class Side { Red, Black };
enum class Kind { Empty, General, Advisor, Elephant, Horse, Rook, Cannon, Pawn };
constexpr Side opposite(Side side) { return side == Side::Red ? Side::Black : Side::Red; }
constexpr int square(int file, int rank) { return rank * 9 + file; }
constexpr int fileOf(int sq) { return sq % 9; }
constexpr int rankOf(int sq) { return sq / 9; }

struct Piece {
    Kind kind = Kind::Empty;
    Side side = Side::Red;
    explicit operator bool() const { return kind != Kind::Empty; }
    bool operator==(const Piece& p) const { return kind == p.kind && (!*this || side == p.side); }
};

struct Move {
    int from = -1;
    int to = -1;
    bool operator==(const Move& m) const { return from == m.from && to == m.to; }
    bool valid() const { return from >= 0 && from < 90 && to >= 0 && to < 90 && from != to; }
};

// Value-only positions are cheap to copy and can safely cross the AI thread boundary.
struct Position {
    std::array<Piece, 90> board{};
    Side turn = Side::Red;
    int quietPlies = 0;
    static Position initial();
    Position after(Move move) const; // Internal: caller must supply a legal move.
    bool sameBoard(const Position& p) const { return turn == p.turn && board == p.board; }
};

bool inCheck(const Position& position, Side side);
std::vector<Move> legalMoves(const Position& position);
bool isLegal(const Position& position, Move move);
std::uint64_t perft(const Position& position, int depth);
const char* pieceName(Piece piece);

} // namespace xiangqi
