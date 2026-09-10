#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace chess {
enum class Side { White, Black };
enum class Kind { Empty, King, Queen, Rook, Bishop, Knight, Pawn };
constexpr Side opposite(Side side) { return side == Side::White ? Side::Black : Side::White; }
constexpr int square(int file, int rank) { return rank * 8 + file; }
constexpr int fileOf(int sq) { return sq % 8; }
constexpr int rankOf(int sq) { return sq / 8; }
struct Piece {
    Kind kind = Kind::Empty;
    Side side = Side::White;
    explicit operator bool() const { return kind != Kind::Empty; }
    bool operator==(const Piece& p) const { return kind == p.kind && (!*this || side == p.side); }
};
struct Move {
    int from = -1, to = -1;
    Kind promotion = Kind::Empty;
    bool operator==(const Move& m) const { return from == m.from && to == m.to && promotion == m.promotion; }
    bool valid() const { return from >= 0 && from < 64 && to >= 0 && to < 64 && from != to; }
};
enum Castling : unsigned { WhiteKing = 1, WhiteQueen = 2, BlackKing = 4, BlackQueen = 8 };
struct Position {
    std::array<Piece, 64> board{};
    Side turn = Side::White;
    unsigned castling = 0;
    int enPassant = -1;
    int quietPlies = 0, fullmove = 1;
    static Position initial();
    static Position fromFen(const std::string& fen); // Validates structure; for fixtures/tools.
    std::string fen() const;
    Position after(Move move) const; // Caller supplies a legal move.
    bool sameBoard(const Position& other) const;
};
bool attacked(const Position& p, int target, Side by);
bool inCheck(const Position& p, Side side);
std::vector<Move> legalMoves(const Position& p);
bool isLegal(const Position& p, Move move);
bool insufficientMaterial(const Position& p);
int capturedSquare(const Position& p, Move move);
Move castlingRookMove(const Position& p, Move move);
int legalEnPassant(const Position& p);
std::uint64_t perft(const Position& p, int depth);
const char* pieceName(Piece piece);
std::string moveUci(Move move);
Move parseUci(const std::string& text);
} // namespace chess
