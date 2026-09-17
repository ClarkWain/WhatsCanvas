#include "Rules.h"

#include <algorithm>
#include <cstdlib>

namespace xiangqi {
namespace {
bool onBoard(int x, int y) { return x >= 0 && x < 9 && y >= 0 && y < 10; }
bool palace(Side side, int x, int y) {
    return x >= 3 && x <= 5 && (side == Side::Red ? y >= 7 && y <= 9 : y >= 0 && y <= 2);
}
bool crossed(Side side, int y) { return side == Side::Red ? y <= 4 : y >= 5; }

// Attack geometry also handles flying generals. Horse legs and elephant eyes
// are tested here, so check detection and move legality share the same rules.
bool attacks(const Position& p, int from, int to) {
    const Piece piece = p.board[from];
    const int x = fileOf(from), y = rankOf(from);
    const int tx = fileOf(to), ty = rankOf(to);
    const int dx = tx - x, dy = ty - y;
    const int ax = std::abs(dx), ay = std::abs(dy);
    if (from == to) return false;
    auto screens = [&] {
        if (dx && dy) return -1;
        const int step = dx ? (dx > 0 ? 1 : -1) : (dy > 0 ? 9 : -9);
        int count = 0;
        for (int s = from + step; s != to; s += step) if (p.board[s]) ++count;
        return count;
    };
    switch (piece.kind) {
    case Kind::General:
        if (dx == 0 && p.board[to].kind == Kind::General && screens() == 0) return true;
        return ax + ay == 1 && palace(piece.side, tx, ty);
    case Kind::Advisor: return ax == 1 && ay == 1 && palace(piece.side, tx, ty);
    case Kind::Elephant:
        return ax == 2 && ay == 2 && !crossed(piece.side, ty)
            && !p.board[square(x + dx / 2, y + dy / 2)];
    case Kind::Horse:
        if (ax == 2 && ay == 1) return !p.board[square(x + dx / 2, y)];
        if (ax == 1 && ay == 2) return !p.board[square(x, y + dy / 2)];
        return false;
    case Kind::Rook: return screens() == 0;
    case Kind::Cannon: return screens() == (p.board[to] ? 1 : 0);
    case Kind::Pawn:
        return (dx == 0 && dy == (piece.side == Side::Red ? -1 : 1))
            || (crossed(piece.side, y) && ay == 0 && ax == 1);
    default: return false;
    }
}

std::vector<Move> pseudoMoves(const Position& p) {
    std::vector<Move> moves;
    moves.reserve(64);
    for (int from = 0; from < 90; ++from) {
        const Piece piece = p.board[from];
        if (!piece || piece.side != p.turn) continue;
        const int x = fileOf(from), y = rankOf(from);
        auto add = [&](int tx, int ty) {
            if (!onBoard(tx, ty)) return;
            const int to = square(tx, ty);
            if (p.board[to] && p.board[to].side == piece.side) return;
            if (attacks(p, from, to)) moves.push_back({from, to});
        };
        if (piece.kind == Kind::Rook || piece.kind == Kind::Cannon) {
            constexpr int directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& dir : directions) {
                bool screen = false;
                for (int tx = x + dir[0], ty = y + dir[1]; onBoard(tx, ty); tx += dir[0], ty += dir[1]) {
                    const int to = square(tx, ty);
                    const Piece target = p.board[to];
                    if (!screen) {
                        if (!target) moves.push_back({from, to});
                        else if (piece.kind == Kind::Rook) {
                            if (target.side != piece.side) moves.push_back({from, to});
                            break;
                        } else screen = true;
                    } else if (target) {
                        if (target.side != piece.side) moves.push_back({from, to});
                        break;
                    }
                }
            }
        } else if (piece.kind == Kind::Horse) {
            constexpr int offsets[8][2] = {{1, 2}, {2, 1}, {-1, 2}, {-2, 1}, {1, -2}, {2, -1}, {-1, -2}, {-2, -1}};
            for (auto& d : offsets) add(x + d[0], y + d[1]);
        } else if (piece.kind == Kind::Elephant || piece.kind == Kind::Advisor) {
            const int d = piece.kind == Kind::Elephant ? 2 : 1;
            for (int dx : {-d, d}) for (int dy : {-d, d}) add(x + dx, y + dy);
        } else {
            add(x + 1, y); add(x - 1, y); add(x, y + 1); add(x, y - 1);
            if (piece.kind == Kind::General) {
                for (int to = 0; to < 90; ++to)
                    if (p.board[to].kind == Kind::General && p.board[to].side != piece.side)
                        add(fileOf(to), rankOf(to));
            }
        }
    }
    return moves;
}
} // namespace

Position Position::initial() {
    Position p;
    constexpr Kind back[] = {Kind::Rook, Kind::Horse, Kind::Elephant, Kind::Advisor,
                            Kind::General, Kind::Advisor, Kind::Elephant, Kind::Horse, Kind::Rook};
    for (Side side : {Side::Black, Side::Red}) {
        const bool red = side == Side::Red;
        for (int x = 0; x < 9; ++x) p.board[square(x, red ? 9 : 0)] = {back[x], side};
        for (int x : {1, 7}) p.board[square(x, red ? 7 : 2)] = {Kind::Cannon, side};
        for (int x : {0, 2, 4, 6, 8}) p.board[square(x, red ? 6 : 3)] = {Kind::Pawn, side};
    }
    return p;
}

Position Position::after(Move move) const {
    Position next = *this;
    next.quietPlies = board[move.to] || board[move.from].kind == Kind::Pawn ? 0 : quietPlies + 1;
    next.board[move.to] = board[move.from];
    next.board[move.from] = {};
    next.turn = opposite(turn);
    return next;
}

bool inCheck(const Position& p, Side side) {
    int general = -1;
    for (int s = 0; s < 90; ++s)
        if (p.board[s].kind == Kind::General && p.board[s].side == side) { general = s; break; }
    if (general < 0) return true;
    for (int s = 0; s < 90; ++s)
        if (p.board[s] && p.board[s].side != side && attacks(p, s, general)) return true;
    return false;
}

std::vector<Move> legalMoves(const Position& p) {
    auto moves = pseudoMoves(p);
    moves.erase(std::remove_if(moves.begin(), moves.end(), [&](Move m) {
        return inCheck(p.after(m), p.turn);
    }), moves.end());
    return moves;
}

bool isLegal(const Position& p, Move move) {
    if (!move.valid()) return false;
    const auto moves = legalMoves(p);
    return std::find(moves.begin(), moves.end(), move) != moves.end();
}

std::uint64_t perft(const Position& p, int depth) {
    if (depth <= 0) return 1;
    std::uint64_t count = 0;
    for (Move m : legalMoves(p)) count += perft(p.after(m), depth - 1);
    return count;
}

const char* pieceName(Piece p) {
    static const char* red[] = {"", "帥", "仕", "相", "馬", "車", "炮", "兵"};
    static const char* black[] = {"", "將", "士", "象", "馬", "車", "砲", "卒"};
    return (p.side == Side::Red ? red : black)[static_cast<int>(p.kind)];
}
} // namespace xiangqi
