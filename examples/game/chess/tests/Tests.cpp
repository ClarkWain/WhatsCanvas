#include "Game.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace chess;
namespace {
int checks = 0;
void require(bool ok, const std::string& message) { ++checks; if (!ok) throw std::runtime_error(message); }
Position fen(const std::string& s) { return Position::fromFen(s); }
bool legal(const Position& p, const std::string& uci) { return isLegal(p, parseUci(uci)); }
void play(Match& m, const std::string& uci) { require(m.play(parseUci(uci)), "legal move " + uci); }
void click(Game& g, int s) { g.click(layout::BoardX + fileOf(s) * layout::Cell, layout::BoardY + rankOf(s) * layout::Cell); }
void clickMove(Game& g, const std::string& uci) { const auto m = parseUci(uci); click(g, m.from); click(g, m.to); }
void rules() {
    const auto initial = Position::initial();
    require(initial.fen() == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "initial FEN roundtrip");
    for (auto entry : {std::pair<int, std::uint64_t>{1,20}, {2,400}, {3,8902}, {4,197281}})
        require(perft(initial, entry.first) == entry.second, "initial perft depth " + std::to_string(entry.first));
    const auto kiwi = fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    for (auto entry : {std::pair<int, std::uint64_t>{1,48}, {2,2039}, {3,97862}})
        require(perft(kiwi, entry.first) == entry.second, "Kiwipete special-rule perft " + std::to_string(entry.first));
    const auto endgame = fen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
    require(perft(endgame, 4) == 43238, "rook/pawn endgame perft including en passant");
    require(!legal(initial, "e2e5") && !legal(initial, "e7e5") && !isLegal(initial, {-1,64}), "bounds and wrong-side moves");
    require(!legal(initial, "a1a3") && !legal(initial, "c1h6"), "sliders cannot jump blockers");
    require(legal(initial, "b1c3"), "knight jumps blockers");
    auto p = fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    require(legal(p, "e1g1") && legal(p, "e1c1"), "both white castlings available");
    auto castled = p.after(parseUci("e1g1"));
    require(castled.board[square(6,7)].kind == Kind::King && castled.board[square(5,7)].kind == Kind::Rook,
            "castling moves both pieces");
    require(castled.castling == (BlackKing | BlackQueen), "castling consumes both king rights");
    require(p.after(parseUci("a1a8")).castling == (WhiteKing | BlackKing), "rook move and rook capture revoke rights");
    p = fen("r3k2r/8/8/8/8/8/5r2/R3K2R w KQkq - 0 1");
    require(!legal(p, "e1g1") && legal(p, "e1c1"), "cannot castle through check");
    p = fen("r3k2r/8/8/8/8/8/4r3/R3K2R w KQkq - 0 1");
    require(!legal(p, "e1g1") && !legal(p, "e1c1"), "cannot castle out of check");
    p = fen("4k3/8/8/8/8/8/6r1/4K2R w K - 0 1");
    require(!legal(p, "e1g1"), "cannot castle into check");
    p = fen("4k3/8/8/8/8/8/8/4K3 w K - 0 1");
    require(!legal(p, "e1g1"), "missing rook cannot castle even with stale FEN right");
    p = fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    require(legal(p, "e5d6"), "en passant allowed immediately");
    const auto ep = p.after(parseUci("e5d6"));
    require(!ep.board[square(3,3)] && ep.board[square(3,2)].kind == Kind::Pawn && ep.enPassant == -1, "en passant removes adjacent pawn");
    require(!legal(p.after(parseUci("e1f1")).after(parseUci("e8f8")), "e5d6"), "en passant expires after another move");
    p = fen("k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    require(!legal(p, "e5d6") && legalEnPassant(p) == -1, "pinned en passant is illegal");
    auto noEp = p; noEp.enPassant = -1;
    require(p.sameBoard(noEp), "unusable en passant does not affect repetition");
    p = fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"); noEp = p; noEp.enPassant = -1;
    require(!p.sameBoard(noEp), "legal en passant changes repetition identity");
    p = fen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    for (const char* m : {"a7a8q", "a7a8r", "a7a8b", "a7a8n"}) require(legal(p,m), "all promotion choices legal");
    require(!legal(p,"a7a8") && !legal(p,"a7a8k"), "promotion is mandatory and cannot create a king");
    p = fen("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    require(legal(p,"a7b8n") && p.after(parseUci("a7b8n")).board[square(1,0)].kind == Kind::Knight, "capture underpromotion");
    p = fen("4k3/8/8/8/8/8/4r3/4K3 w - - 0 1");
    require(inCheck(p,Side::White) && legal(p,"e1e2"), "king can capture undefended checking piece");
    p = fen("8/8/8/8/8/4k3/4r3/4K3 w - - 0 1");
    require(!legal(p,"e1e2"), "kings may not be adjacent");
    std::cout << "PASS rules, FEN and standard perft suites\n";
}
void endings() {
    Match fool;
    for (const char* m : {"f2f3","e7e5","g2g4","d8h4"}) play(fool,m);
    require(fool.outcome() == Outcome::BlackWins && fool.checked(), "Fool's mate ends game");
    require(!fool.play(parseUci("a2a3")), "terminal game rejects moves");
    require(fool.undo() && fool.moves().size() == 2 && fool.outcome() == Outcome::Playing, "undo after checkmate");
    Match stale(Difficulty::Medium, fen("7k/5K2/6Q1/8/8/8/8/8 b - - 0 1"));
    require(stale.outcome() == Outcome::Stalemate && !stale.checked(), "stalemate is a draw");
    for (const char* position : {"4k3/8/8/8/8/8/8/4K3 w - - 0 1", "4k3/8/8/8/8/8/8/3BK3 w - - 0 1", "4k3/8/8/8/8/8/8/3NK3 w - - 0 1"})
        require(Match(Difficulty::Easy,fen(position)).outcome() == Outcome::MaterialDraw, "basic insufficient material");
    require(!insufficientMaterial(fen("4k3/8/8/8/8/8/8/2NNK3 w - - 0 1")), "two knights are not an automatic dead position");
    Match repeat;
    const char* cycle[] = {"g1f3","g8f6","f3g1","f6g8"};
    for (int i = 0; i < 7; ++i) play(repeat,cycle[i%4]);
    require(repeat.drawClaim().available() && repeat.drawClaim().intended == parseUci("f6g8"), "can claim on an intended third occurrence");
    play(repeat,"f6g8");
    require(repeat.outcome() == Outcome::Playing && repeat.drawClaim().available(), "threefold needs a claim");
    require(repeat.claimDraw() && repeat.outcome() == Outcome::RepetitionDraw, "claim threefold");
    require(repeat.undo() && repeat.outcome() == Outcome::Playing, "undo claimed draw");
    Match five;
    for (int i = 0; i < 16; ++i) play(five,cycle[i%4]);
    require(five.outcome() == Outcome::RepetitionDraw, "fivefold automatically draws");
    Match fifty(Difficulty::Medium,fen("4k3/8/8/8/8/8/8/R3K3 w - - 99 1"));
    require(fifty.drawClaim().available() && fifty.drawClaim().intended.valid(), "intended 50-move claim");
    require(fifty.claimDraw() && fifty.moves().empty(), "claim does not execute intended move");
    Match seventyFive(Difficulty::Medium,fen("4k3/8/8/8/8/8/8/R3K3 w - - 149 1"));
    play(seventyFive,"a1a2");
    require(seventyFive.outcome() == Outcome::QuietDraw, "75-move automatic draw");
    std::cout << "PASS checkmate, stalemate, material and FIDE draw claims\n";
}
void interaction() {
    double time = 5;
    Game g(Difficulty::Medium,42,Position::initial(),[&]{return time;});
    click(g,square(4,1)); require(g.selected() == -1,"cannot select black");
    click(g,square(4,6)); require(g.destinations().size() == 2,"white pawn has two starting destinations");
    click(g,square(5,5)); require(g.match().moves().empty() && g.selected() == square(4,6),"illegal destination retains selection");
    click(g,square(4,4)); g.update();
    require(g.thinking() && g.boardBusy(),"player move starts AI and transition");
    for (int i=0;i<40;++i) { g.update(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    require(g.match().moves().size() == 1,"AI cannot overwrite player movement");
    time += 0.8;
    for (int i=0;i<1000 && g.thinking();++i) { g.update(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    require(g.match().moves().size() == 2,"AI applies one legal reply");
    g.undo(); require(g.match().position().sameBoard(Position::initial()),"undo restores both half-moves and special rights");
    time += 1.1; g.update();
    clickMove(g,"e2e4"); g.update(); g.undo(); time += 1; g.update();
    require(g.match().moves().empty(),"canceled search cannot modify restored game");
    g.requestNewGame(Difficulty::Hard); clickMove(g,"d2d4"); g.undo();
    require(g.match().moves().size()==1,"hard forbids undo");
    g.requestNewGame(Difficulty::Easy); require(g.dialog(),"difficulty change confirms active game");
    g.cancelDialog(); require(g.match().difficulty()==Difficulty::Hard,"cancel preserves difficulty");
    g.requestNewGame(Difficulty::Easy); g.confirmNewGame(); require(g.match().moves().empty(),"confirm resets match");
    const auto promotion = fen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    for (Kind kind : layout::Promotions) {
        Game promote(Difficulty::Medium,42,promotion,[&]{return time;});
        clickMove(promote,"a7a8");
        require(promote.promoting() && promote.match().moves().empty() && !promote.thinking(),"promotion pauses for human choice");
        promote.choosePromotion(kind);
        require(!promote.promoting() && promote.match().position().board[0].kind==kind,"promotion choice applied");
        promote.undo(); require(promote.match().position().sameBoard(promotion),"promotion undo restores pawn");
    }
    Game cancel(Difficulty::Easy,42,promotion,[&]{return time;});
    clickMove(cancel,"a7a8"); cancel.cancelDialog();
    require(!cancel.promoting() && cancel.match().moves().empty(),"escape cancels promotion");
    for (auto size : {std::pair<float,float>{1120.f,820.f},{840.f,615.f},{1680.f,1230.f}}) {
        const auto view=layout::Viewport::fit(size.first,size.second);
        for(int s=0;s<64;++s) {
            const auto pt=view.toDesign(view.x+(layout::BoardX+fileOf(s)*layout::Cell)*view.scale,view.y+(layout::BoardY+rankOf(s)*layout::Cell)*view.scale);
            require(layout::hitSquare(pt.first,pt.second)==s,"scaled cell hit-test");
        }
    }
    std::cout << "PASS UI input, AI pacing/cancellation, undo, promotion, difficulty and resize\n";
}
void ai() {
    std::atomic_bool cancel{false};
    for (auto difficulty : {Difficulty::Easy,Difficulty::Medium,Difficulty::Hard}) {
        const auto result=chooseMove(Position::initial(),difficulty,42,cancel);
        require(isLegal(Position::initial(),result.move),"each AI returns legal move");
        require(result.nodes<=limitsFor(difficulty).nodes+1,"node budget respected");
        std::cout << "AI difficulty=" << static_cast<int>(difficulty) << " depth=" << result.completedDepth << " nodes=" << result.nodes << " ms=" << result.milliseconds << '\n';
    }
    const auto mate=fen("6k1/5ppp/8/8/8/5Q2/8/6K1 w - - 0 1");
    const auto result=chooseMove(mate,Difficulty::Medium,42,cancel);
    require(Match(Difficulty::Medium,mate.after(result.move)).outcome()==Outcome::WhiteWins,"AI finds mate in one");
    Match game;
    int plies=0;
    while(game.outcome()==Outcome::Playing && plies<300) {
        if(game.drawClaim().available()) { require(game.claimDraw(),"self-play claims draw"); break; }
        const auto before=game.position();
        const auto move=chooseMove(before,Difficulty::Medium,700+plies,cancel).move;
        require(game.play(move) && !inCheck(game.position(),before.turn),"self-play remains legal");
        ++plies;
    }
    require(plies>=10 && game.outcome()!=Outcome::Playing,"self-play reaches a terminal result");
    std::cout << "PASS self-play plies=" << plies << " outcome=" << static_cast<int>(game.outcome()) << '\n';
}
void oracle() {
    std::string line;
    while(std::getline(std::cin,line)) {
        try {
            const auto p=fen(line);
            auto moves=legalMoves(p);
            std::sort(moves.begin(),moves.end(),[](Move a,Move b){return moveUci(a)<moveUci(b);});
            std::cout << "{\"check\":" << (inCheck(p,p.turn)?"true":"false") << ",\"material\":" << (insufficientMaterial(p)?"true":"false") << ",\"moves\":[";
            for(std::size_t i=0;i<moves.size();++i) { if(i) std::cout << ','; std::cout << "[\"" << moveUci(moves[i]) << "\",\"" << p.after(moves[i]).fen() << "\"]"; }
            std::cout << "]}\n" << std::flush;
        } catch(const std::exception&) { std::cout << "{\"error\":true}\n" << std::flush; }
    }
}
}
int main(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--oracle") { oracle(); return 0; }
    try { rules(); endings(); interaction(); ai(); std::cout << "PASS " << checks << " checks\n"; return 0; }
    catch(const std::exception& error) { std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1; }
}
