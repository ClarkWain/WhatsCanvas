#include "Rules.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace chess {
namespace {
bool inside(int x, int y) { return x >= 0 && x < 8 && y >= 0 && y < 8; }
Kind decode(char c) {
    switch (static_cast<char>(std::tolower(static_cast<unsigned char>(c)))) {
    case 'k': return Kind::King; case 'q': return Kind::Queen; case 'r': return Kind::Rook;
    case 'b': return Kind::Bishop; case 'n': return Kind::Knight; case 'p': return Kind::Pawn;
    default: return Kind::Empty;
    }
}
char encode(Kind kind) { return " kqrbnp"[static_cast<int>(kind)]; }
unsigned rookRight(int s) {
    switch (s) { case 63: return WhiteKing; case 56: return WhiteQueen; case 7: return BlackKing; case 0: return BlackQueen; default: return 0; }
}
std::vector<Move> pseudo(const Position& p) {
    std::vector<Move> result;
    result.reserve(64);
    for (int from = 0; from < 64; ++from) {
        const Piece piece = p.board[from];
        if (!piece || piece.side != p.turn) continue;
        const int x = fileOf(from), y = rankOf(from);
        auto add = [&](int tx, int ty) {
            if (!inside(tx, ty)) return;
            const int to = square(tx, ty);
            if (p.board[to] && (p.board[to].side == piece.side || p.board[to].kind == Kind::King)) return;
            if (piece.kind == Kind::Pawn && (ty == 0 || ty == 7)) {
                for (Kind kind : {Kind::Queen, Kind::Rook, Kind::Bishop, Kind::Knight}) result.push_back({from, to, kind});
            } else result.push_back({from, to});
        };
        if (piece.kind == Kind::Pawn) {
            const int dy = piece.side == Side::White ? -1 : 1;
            if (inside(x, y + dy) && !p.board[square(x, y + dy)]) {
                add(x, y + dy);
                if (y == (piece.side == Side::White ? 6 : 1) && !p.board[square(x, y + dy * 2)]) add(x, y + dy * 2);
            }
            for (int dx : {-1, 1}) if (inside(x + dx, y + dy)) {
                const int to = square(x + dx, y + dy);
                const Piece victim = p.board[square(x + dx, y)];
                if (p.board[to] || (to == p.enPassant && victim.kind == Kind::Pawn && victim.side != piece.side)) add(x + dx, y + dy);
            }
        } else if (piece.kind == Kind::Knight) {
            constexpr int jumps[][2] = {{1,2},{2,1},{-1,2},{-2,1},{1,-2},{2,-1},{-1,-2},{-2,-1}};
            for (const auto& d : jumps) add(x + d[0], y + d[1]);
        } else if (piece.kind == Kind::King) {
            for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) if (dx || dy) add(x + dx, y + dy);
            const int home = piece.side == Side::White ? 7 : 0;
            if (from == square(4, home) && !inCheck(p, piece.side)) {
                for (bool kingSide : {true, false}) {
                    const unsigned right = piece.side == Side::White ? (kingSide ? WhiteKing : WhiteQueen) : (kingSide ? BlackKing : BlackQueen);
                    const int rook = square(kingSide ? 7 : 0, home);
                    if (!(p.castling & right) || p.board[rook].kind != Kind::Rook || p.board[rook].side != piece.side) continue;
                    bool empty = true;
                    for (int f = kingSide ? 5 : 1; f <= (kingSide ? 6 : 3); ++f) if (p.board[square(f, home)]) empty = false;
                    if (!empty) continue;
                    // Test transit with the king removed from its old square, so
                    // the king cannot itself hide a sliding attack along the rank.
                    const auto transit = p.after({from, square(kingSide ? 5 : 3, home)});
                    if (!inCheck(transit, piece.side)) add(kingSide ? 6 : 2, home);
                }
            }
        } else {
            for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) {
                if ((!dx && !dy) || (piece.kind == Kind::Bishop && (!dx || !dy)) || (piece.kind == Kind::Rook && dx && dy)) continue;
                for (int tx = x + dx, ty = y + dy; inside(tx, ty); tx += dx, ty += dy) {
                    add(tx, ty);
                    if (p.board[square(tx, ty)]) break;
                }
            }
        }
    }
    return result;
}
} // namespace

Position Position::initial() { return fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); }
Position Position::fromFen(const std::string& fen) {
    Position p;
    std::istringstream input(fen);
    std::string boardText, side, rights, ep, extra;
    if (!(input >> boardText >> side >> rights >> ep >> p.quietPlies >> p.fullmove) || (input >> extra)
        || p.quietPlies < 0 || p.fullmove < 1) throw std::invalid_argument("Invalid FEN fields");
    int x = 0, y = 0, kings[2] = {};
    for (char c : boardText) {
        if (c == '/') { if (x != 8 || ++y > 7) throw std::invalid_argument("Invalid FEN ranks"); x = 0; }
        else if (c >= '1' && c <= '8') x += c - '0';
        else {
            const Kind kind = decode(c);
            if (!inside(x, y) || kind == Kind::Empty) throw std::invalid_argument("Invalid FEN piece");
            const Side owner = std::isupper(static_cast<unsigned char>(c)) ? Side::White : Side::Black;
            p.board[square(x++, y)] = {kind, owner};
            if (kind == Kind::King) ++kings[owner == Side::White ? 0 : 1];
        }
        if (x > 8) throw std::invalid_argument("Invalid FEN file count");
    }
    if (x != 8 || y != 7 || kings[0] != 1 || kings[1] != 1 || (side != "w" && side != "b")) throw std::invalid_argument("Invalid FEN position");
    p.turn = side == "w" ? Side::White : Side::Black;
    if (rights != "-") for (char c : rights) {
        unsigned right = c == 'K' ? WhiteKing : c == 'Q' ? WhiteQueen : c == 'k' ? BlackKing : c == 'q' ? BlackQueen : 0;
        if (!right || (p.castling & right)) throw std::invalid_argument("Invalid FEN castling");
        p.castling |= right;
    }
    if (ep != "-") {
        if (ep.size() != 2 || ep[0] < 'a' || ep[0] > 'h' || ep[1] != (p.turn == Side::White ? '6' : '3'))
            throw std::invalid_argument("Invalid FEN en passant");
        p.enPassant = square(ep[0] - 'a', '8' - ep[1]);
    }
    return p;
}
std::string Position::fen() const {
    std::ostringstream out;
    for (int y = 0; y < 8; ++y) {
        int empty = 0;
        for (int x = 0; x < 8; ++x) {
            const Piece piece = board[square(x, y)];
            if (!piece) ++empty;
            else {
                if (empty) { out << empty; empty = 0; }
                char c = encode(piece.kind);
                out << (piece.side == Side::White ? static_cast<char>(std::toupper(c)) : c);
            }
        }
        if (empty) out << empty;
        if (y != 7) out << '/';
    }
    out << (turn == Side::White ? " w " : " b ");
    if (!castling) out << '-';
    else { if (castling & WhiteKing) out << 'K'; if (castling & WhiteQueen) out << 'Q'; if (castling & BlackKing) out << 'k'; if (castling & BlackQueen) out << 'q'; }
    out << ' ';
    if (enPassant < 0) out << '-'; else out << static_cast<char>('a' + fileOf(enPassant)) << static_cast<char>('8' - rankOf(enPassant));
    out << ' ' << quietPlies << ' ' << fullmove;
    return out.str();
}
int capturedSquare(const Position& p, Move m) {
    if (!m.valid()) return -1;
    if (p.board[m.to]) return m.to;
    if (p.board[m.from].kind == Kind::Pawn && m.to == p.enPassant && fileOf(m.from) != fileOf(m.to))
        return square(fileOf(m.to), rankOf(m.from));
    return -1;
}
Move castlingRookMove(const Position& p, Move m) {
    if (!m.valid() || p.board[m.from].kind != Kind::King || std::abs(fileOf(m.to) - fileOf(m.from)) != 2) return {};
    return {square(fileOf(m.to) == 6 ? 7 : 0, rankOf(m.from)), square(fileOf(m.to) == 6 ? 5 : 3, rankOf(m.from))};
}
Position Position::after(Move m) const {
    Position next = *this;
    const Piece piece = board[m.from];
    const int victim = capturedSquare(*this, m);
    if (victim >= 0) { next.board[victim] = {}; next.castling &= ~rookRight(victim); }
    next.board[m.to] = piece;
    next.board[m.from] = {};
    if (m.promotion != Kind::Empty) next.board[m.to].kind = m.promotion;
    const Move rook = castlingRookMove(*this, m);
    if (rook.valid()) { next.board[rook.to] = board[rook.from]; next.board[rook.from] = {}; }
    if (piece.kind == Kind::King) next.castling &= ~(piece.side == Side::White ? (WhiteKing | WhiteQueen) : (BlackKing | BlackQueen));
    next.castling &= ~rookRight(m.from);
    next.enPassant = piece.kind == Kind::Pawn && std::abs(rankOf(m.to) - rankOf(m.from)) == 2 ? (m.from + m.to) / 2 : -1;
    next.quietPlies = piece.kind == Kind::Pawn || victim >= 0 ? 0 : quietPlies + 1;
    next.fullmove += turn == Side::Black ? 1 : 0;
    next.turn = opposite(turn);
    return next;
}
bool attacked(const Position& p, int target, Side by) {
    for (int from = 0; from < 64; ++from) {
        const Piece piece = p.board[from];
        if (!piece || piece.side != by || from == target) continue;
        const int dx = fileOf(target) - fileOf(from), dy = rankOf(target) - rankOf(from);
        const int ax = std::abs(dx), ay = std::abs(dy);
        if (piece.kind == Kind::Pawn) { if (ax == 1 && dy == (by == Side::White ? -1 : 1)) return true; continue; }
        if (piece.kind == Kind::King) { if (std::max(ax, ay) == 1) return true; continue; }
        if (piece.kind == Kind::Knight) { if (ax * ay == 2) return true; continue; }
        const bool diagonal = ax == ay, straight = !dx || !dy;
        if (!(diagonal || straight) || (piece.kind == Kind::Rook && !straight) || (piece.kind == Kind::Bishop && !diagonal)) continue;
        const int step = (dx > 0 ? 1 : dx < 0 ? -1 : 0) + (dy > 0 ? 8 : dy < 0 ? -8 : 0);
        bool clear = true;
        for (int s = from + step; s != target; s += step) if (p.board[s]) { clear = false; break; }
        if (clear) return true;
    }
    return false;
}
bool inCheck(const Position& p, Side side) {
    for (int s = 0; s < 64; ++s) if (p.board[s].kind == Kind::King && p.board[s].side == side) return attacked(p, s, opposite(side));
    return true;
}
std::vector<Move> legalMoves(const Position& p) {
    auto moves = pseudo(p);
    moves.erase(std::remove_if(moves.begin(), moves.end(), [&](Move m) { return inCheck(p.after(m), p.turn); }), moves.end());
    return moves;
}
bool isLegal(const Position& p, Move m) {
    if (!m.valid()) return false;
    const auto moves = legalMoves(p);
    return std::find(moves.begin(), moves.end(), m) != moves.end();
}
int legalEnPassant(const Position& p) {
    if (p.enPassant < 0) return -1;
    for (Move m : legalMoves(p))
        if (m.to == p.enPassant && p.board[m.from].kind == Kind::Pawn && !p.board[m.to]) return p.enPassant;
    return -1;
}
bool Position::sameBoard(const Position& p) const {
    return turn == p.turn && castling == p.castling && board == p.board && legalEnPassant(*this) == legalEnPassant(p);
}
bool insufficientMaterial(const Position& p) {
    int minors = 0, knights = 0, bishopColor = -1;
    bool sameColor = true;
    for (int s = 0; s < 64; ++s) {
        const Kind k = p.board[s].kind;
        if (k == Kind::Empty || k == Kind::King) continue;
        if (k != Kind::Bishop && k != Kind::Knight) return false;
        ++minors;
        if (k == Kind::Knight) ++knights;
        else {
            const int color = (fileOf(s) + rankOf(s)) % 2;
            if (bishopColor >= 0 && bishopColor != color) sameColor = false;
            bishopColor = color;
        }
    }
    return minors <= 1 || (knights == 0 && sameColor);
}
std::uint64_t perft(const Position& p, int depth) {
    if (depth <= 0) return 1;
    std::uint64_t count = 0;
    for (Move m : legalMoves(p)) count += perft(p.after(m), depth - 1);
    return count;
}
const char* pieceName(Piece piece) {
    // Standard chess SAN letters. Pawn moves use no piece letter in real SAN,
    // but the sidebar layout expects a short glyph for every move so we keep P.
    static const char* names[] = {"", "K", "Q", "R", "B", "N", "P"};
    return names[static_cast<int>(piece.kind)];
}
std::string moveUci(Move m) {
    if (!m.valid()) return "0000";
    std::string result{static_cast<char>('a' + fileOf(m.from)), static_cast<char>('8' - rankOf(m.from)),
                       static_cast<char>('a' + fileOf(m.to)), static_cast<char>('8' - rankOf(m.to))};
    if (m.promotion != Kind::Empty) result += encode(m.promotion);
    return result;
}
Move parseUci(const std::string& text) {
    if (text.size() != 4 && text.size() != 5) return {};
    if (text[0] < 'a' || text[0] > 'h' || text[2] < 'a' || text[2] > 'h'
        || text[1] < '1' || text[1] > '8' || text[3] < '1' || text[3] > '8') return {};
    Move m{square(text[0] - 'a', '8' - text[1]), square(text[2] - 'a', '8' - text[3])};
    if (text.size() == 5) {
        m.promotion = decode(text[4]);
        if (m.promotion != Kind::Queen && m.promotion != Kind::Rook && m.promotion != Kind::Bishop && m.promotion != Kind::Knight) return {};
    }
    return m;
}
} // namespace chess
