#include "Game.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace xiangqi;
namespace {
int checks = 0;
void require(bool ok, const std::string& name) {
    ++checks;
    if (!ok) throw std::runtime_error(name);
}
Move move(int x, int y, int tx, int ty) { return {square(x, y), square(tx, ty)}; }
Position bare(Side turn = Side::Red) {
    Position p;
    p.turn = turn;
    p.board[square(4, 9)] = {Kind::General, Side::Red};
    p.board[square(3, 0)] = {Kind::General, Side::Black};
    return p;
}
void put(Position& p, int x, int y, Kind k, Side side = Side::Red) { p.board[square(x, y)] = {k, side}; }
void clickSquare(Game& game, int s) {
    game.click(layout::BoardX + fileOf(s) * layout::Cell, layout::BoardY + rankOf(s) * layout::Cell);
}
void clickButton(Game& game, Rect r) { game.click(r.x + r.w / 2, r.y + r.h / 2); }
void awaitAI(Game& game) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((game.thinking() || game.boardBusy()) && std::chrono::steady_clock::now() < deadline) {
        game.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(!game.thinking() && !game.boardBusy(), "AI replies and lands within five seconds");
}
void settle(Game& game) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (game.boardBusy() && std::chrono::steady_clock::now() < deadline) {
        game.update(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(!game.boardBusy(), "animation releases board input");
}
void rules() {
    const auto start = Position::initial();
    require(legalMoves(start).size() == 44, "initial position has 44 legal moves");
    require(perft(start, 2) == 1920, "initial perft depth 2 = 1920");
    require(perft(start, 3) == 79666, "initial perft depth 3 = 79666");
    require(!isLegal(start, {-1, 90}), "invalid move bounds rejected");
    require(!isLegal(start, move(0, 6, 0, 6)), "null move rejected");
    require(!isLegal(start, move(0, 3, 0, 4)), "wrong side rejected");
    require(!isLegal(start, move(0, 9, 0, 6)), "own piece capture rejected");

    auto p = bare();
    put(p, 4, 5, Kind::Horse);
    require(isLegal(p, move(4, 5, 6, 4)), "horse L move");
    put(p, 5, 5, Kind::Pawn);
    require(!isLegal(p, move(4, 5, 6, 4)), "horse leg blocks sideways jump");
    require(isLegal(p, move(4, 5, 5, 3)), "other horse leg stays free");
    put(p, 4, 4, Kind::Pawn);
    require(!isLegal(p, move(4, 5, 5, 3)), "horse vertical leg");

    p = bare(); put(p, 2, 9, Kind::Elephant);
    require(isLegal(p, move(2, 9, 4, 7)), "elephant diagonal");
    put(p, 3, 8, Kind::Pawn);
    require(!isLegal(p, move(2, 9, 4, 7)), "elephant eye blocked");
    p = bare(); put(p, 2, 5, Kind::Elephant);
    require(!isLegal(p, move(2, 5, 4, 3)), "red elephant cannot cross river");
    p = bare(Side::Black); put(p, 2, 4, Kind::Elephant, Side::Black);
    require(!isLegal(p, move(2, 4, 4, 6)), "black elephant cannot cross river");

    p = bare(); put(p, 3, 9, Kind::Advisor);
    require(isLegal(p, move(3, 9, 4, 8)), "advisor palace diagonal");
    require(!isLegal(p, move(3, 9, 2, 8)), "advisor cannot leave palace");
    p = bare(); p.board[square(4, 9)] = {}; put(p, 5, 8, Kind::General);
    require(!isLegal(p, move(5, 8, 6, 8)), "general palace limit");
    require(!isLegal(p, move(5, 8, 4, 7)), "general cannot move diagonally");

    p = bare(); put(p, 0, 6, Kind::Pawn);
    require(isLegal(p, move(0, 6, 0, 5)), "red pawn forward");
    require(!isLegal(p, move(0, 6, 1, 6)), "pawn cannot move sideways before river");
    p = bare(); put(p, 0, 4, Kind::Pawn);
    require(isLegal(p, move(0, 4, 1, 4)), "pawn sideways after river");
    require(!isLegal(p, move(0, 4, 0, 5)), "pawn cannot retreat");
    p = bare(Side::Black); put(p, 8, 5, Kind::Pawn, Side::Black);
    require(isLegal(p, move(8, 5, 8, 6)) && isLegal(p, move(8, 5, 7, 5)), "black pawn direction and river");

    p = bare(); put(p, 0, 5, Kind::Rook); put(p, 0, 2, Kind::Pawn, Side::Black);
    require(isLegal(p, move(0, 5, 0, 2)), "rook captures along clear file");
    require(!isLegal(p, move(0, 5, 0, 1)), "rook cannot jump occupied square");
    require(!isLegal(p, move(0, 5, 1, 4)), "rook cannot move diagonally");
    p = bare(); put(p, 0, 5, Kind::Cannon); put(p, 0, 1, Kind::Pawn, Side::Black);
    require(isLegal(p, move(0, 5, 0, 2)), "cannon quiet move");
    require(!isLegal(p, move(0, 5, 0, 1)), "cannon needs a screen to capture");
    put(p, 0, 3, Kind::Pawn);
    require(isLegal(p, move(0, 5, 0, 1)), "cannon captures over exactly one screen");
    require(!isLegal(p, move(0, 5, 0, 2)), "cannon cannot jump without capture");
    put(p, 0, 2, Kind::Pawn, Side::Black);
    require(!isLegal(p, move(0, 5, 0, 1)), "two screens block cannon capture");

    p = bare(); p.board[square(3, 0)] = {}; put(p, 4, 0, Kind::General, Side::Black); put(p, 4, 5, Kind::Rook);
    require(!inCheck(p, Side::Red), "blocked generals do not face");
    require(!isLegal(p, move(4, 5, 5, 5)), "cannot expose facing generals");
    p.board[square(4, 5)] = {};
    require(inCheck(p, Side::Red) && inCheck(p, Side::Black), "flying generals attack both ways");
    p = bare(); put(p, 4, 2, Kind::Rook, Side::Black); put(p, 0, 6, Kind::Pawn);
    require(inCheck(p, Side::Red), "rook check detected");
    require(!isLegal(p, move(0, 6, 0, 5)), "must answer check");
    require(isLegal(p, move(4, 9, 5, 9)), "general escapes check");
    std::cout << "PASS rules and perft\n";
}
void outcomesAndHistory() {
    Position p;
    p.turn = Side::Black;
    put(p, 4, 0, Kind::General, Side::Black); put(p, 3, 9, Kind::General);
    put(p, 3, 2, Kind::Rook); put(p, 5, 2, Kind::Rook); put(p, 4, 2, Kind::Pawn);
    Match stale(Difficulty::Medium, p);
    require(!stale.checked() && stale.outcome() == Outcome::RedWins, "stalemate is loss, not draw");
    put(p, 4, 2, Kind::Rook);
    Match mate(Difficulty::Medium, p);
    require(mate.checked() && mate.outcome() == Outcome::RedWins, "checkmate detected");
    require(!mate.play(move(4, 0, 4, 1)), "terminal match rejects input");

    Match match;
    const Move cycle[] = {move(1, 9, 2, 7), move(1, 0, 2, 2), move(2, 7, 1, 9), move(2, 2, 1, 0)};
    for (int i = 0; i < 8; ++i) require(match.play(cycle[i % 4]), "repetition cycle legal");
    require(match.outcome() == Outcome::RepetitionDraw, "third occurrence is a draw");
    require(match.undo() && match.outcome() == Outcome::Playing && match.moves().size() == 6, "undo reopens terminal match");
    p = Position::initial(); p.quietPlies = 119;
    Match quiet(Difficulty::Easy, p);
    require(quiet.play(move(1, 9, 2, 7)) && quiet.outcome() == Outcome::QuietDraw, "120 quiet plies is draw");
    require(p.after(move(0, 6, 0, 5)).quietPlies == 0, "pawn move resets quiet clock");
    p = bare(); p.quietPlies = 119; put(p, 0, 5, Kind::Rook); put(p, 0, 2, Kind::Pawn, Side::Black);
    require(p.after(move(0, 5, 0, 2)).quietPlies == 0, "capture resets quiet clock");

    for (auto d : {Difficulty::Easy, Difficulty::Medium, Difficulty::Hard}) {
        Match game(d);
        require(!game.undo(), "no undo at start");
        require(game.play(move(0, 6, 0, 5)), "human first move");
        if (d == Difficulty::Hard) require(!game.canUndo() && !game.undo(), "hard forbids undo");
        else require(game.undo() && game.position().sameBoard(Position::initial()), "undo before reply restores board");
        game.reset(d);
        require(game.play(move(0, 6, 0, 5)) && game.play(move(0, 3, 0, 4)), "one full turn");
        if (d != Difficulty::Hard) require(game.undo() && game.position().sameBoard(Position::initial()), "undo removes AI reply too");
    }
    std::cout << "PASS outcomes, draw clocks and undo\n";
}
void ai() {
    std::atomic_bool cancel{false};
    for (auto d : {Difficulty::Easy, Difficulty::Medium, Difficulty::Hard}) {
        const auto result = chooseMove(Position::initial(), d, 42, cancel);
        require(isLegal(Position::initial(), result.move), "each AI difficulty returns legal move");
        require(result.nodes <= limitsFor(d).nodes + 1, "AI obeys node budget");
        require(result.milliseconds < limitsFor(d).time.count() + 1500, "AI obeys time budget with scheduling tolerance");
        std::cout << "AI difficulty=" << static_cast<int>(d) << " depth=" << result.completedDepth
                  << " nodes=" << result.nodes << " ms=" << result.milliseconds << '\n';
    }
    cancel = true;
    require(!chooseMove(Position::initial(), Difficulty::Hard, 42, cancel).move.valid(), "cancel before search");
    cancel = false;
    Position p;
    put(p, 4, 0, Kind::General, Side::Black); put(p, 3, 9, Kind::General);
    put(p, 3, 3, Kind::Rook); put(p, 5, 3, Kind::Rook); put(p, 0, 2, Kind::Rook);
    for (auto d : {Difficulty::Medium, Difficulty::Hard}) {
        const auto result = chooseMove(p, d, 11, cancel);
        require(result.move.valid() && Match(d, p.after(result.move)).outcome() == Outcome::RedWins, "search finds mate in one");
    }
    Match selfPlay(Difficulty::Medium);
    int plies = 0;
    while (plies < 240 && selfPlay.outcome() == Outcome::Playing) {
        const auto before = selfPlay.position();
        const auto result = chooseMove(before, Difficulty::Medium, 500 + plies, cancel);
        require(result.move.valid() && selfPlay.play(result.move), "self-play move is accepted");
        require(!inCheck(selfPlay.position(), before.turn), "self-play never leaves own king attacked");
        ++plies;
    }
    require(plies >= 10 && selfPlay.outcome() != Outcome::Playing, "self-play completes a sustained game");
    std::cout << "PASS self-play plies=" << plies << " outcome=" << static_cast<int>(selfPlay.outcome()) << '\n';
}
void interaction() {
    Game game(Difficulty::Medium);
    clickSquare(game, square(0, 3)); require(game.selected() == -1, "cannot select black");
    clickSquare(game, square(0, 6)); require(game.selected() == square(0, 6), "select human pawn");
    require(game.destinations() == std::vector<int>{square(0, 5)}, "only legal destinations highlighted");
    clickSquare(game, square(1, 6)); require(game.match().moves().empty(), "illegal click does not move");
    require(game.selected() == square(0, 6), "illegal destination retains selected piece");
    game.clearSelection();
    clickSquare(game, square(0, 6)); clickSquare(game, square(0, 5)); game.update();
    require(game.thinking(), "human move starts AI turn");
    clickSquare(game, square(2, 6)); require(game.selected() == -1, "board input locked during AI");
    clickButton(game, layout::Undo);
    require(game.match().position().sameBoard(Position::initial()), "undo while AI is running");
    for (int i = 0; i < 30; ++i) { game.update(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    require(game.match().moves().empty(), "stale reply after undo discarded");
    settle(game);
    clickSquare(game, square(0, 6)); clickSquare(game, square(0, 5)); awaitAI(game);
    require(game.match().moves().size() == 2, "complete human/AI turn");
    clickButton(game, layout::Undo);
    require(game.match().position().sameBoard(Position::initial()), "button undo after AI reply");
    settle(game);

    clickSquare(game, square(2, 6)); clickSquare(game, square(2, 5)); game.update();
    clickButton(game, layout::DifficultyButtons[2]); require(game.dialog(), "difficulty change asks before resetting active match");
    clickButton(game, layout::Cancel); require(!game.dialog() && game.match().moves().size() == 1, "cancel keeps active match");
    clickButton(game, layout::DifficultyButtons[2]); clickButton(game, layout::Confirm);
    require(game.match().difficulty() == Difficulty::Hard && game.match().moves().empty(), "confirm changes difficulty and resets");
    clickSquare(game, square(4, 6)); clickSquare(game, square(4, 5));
    clickButton(game, layout::Undo); require(game.match().moves().size() == 1, "hard undo button disabled");
    awaitAI(game); require(game.match().moves().size() == 2, "hard AI full round after canceled prior search");
    clickButton(game, layout::Restart); require(game.dialog(), "restart confirms ongoing match");
    clickButton(game, layout::Confirm); require(game.match().moves().empty(), "restart completed");
    for (auto dimensions : {std::pair<float, float>{1120.0f, 820.0f}, {840.0f, 615.0f}, {1600.0f, 900.0f}, {2240.0f, 1640.0f}}) {
        auto view = layout::Viewport::fit(dimensions.first, dimensions.second);
        for (int s = 0; s < 90; ++s) {
            auto p = view.toDesign(view.x + (layout::BoardX + fileOf(s) * layout::Cell) * view.scale,
                                   view.y + (layout::BoardY + rankOf(s) * layout::Cell) * view.scale);
            require(layout::hitSquare(p.first, p.second) == s, "resize/DPI coordinate round trip");
        }
    }
    require(layout::hitSquare(-100, -100) == -1, "outside board is ignored");
    std::cout << "PASS input, asynchronous cancellation, difficulty, restart and scaled hit tests\n";
}
void terminalInteraction() {
    Position p;
    put(p, 4, 0, Kind::General, Side::Black); put(p, 3, 9, Kind::General);
    put(p, 3, 3, Kind::Rook); put(p, 5, 3, Kind::Rook); put(p, 0, 2, Kind::Rook);
    Game win(Difficulty::Easy, 42, p);
    clickSquare(win, square(0, 2)); clickSquare(win, square(4, 2));
    require(win.match().outcome() == Outcome::RedWins && !win.thinking(), "winning click ends game without starting AI");
    clickSquare(win, square(3, 3));
    require(win.selected() == -1 && win.match().moves().size() == 1, "terminal board locks input");
    win.undo(); require(win.match().position().sameBoard(p), "undo a winning human move");
    settle(win);
    clickSquare(win, square(0, 2)); clickSquare(win, square(4, 2));
    clickButton(win, layout::Restart);
    require(win.match().position().sameBoard(Position::initial()) && !win.dialog(), "restart finished game");

    p = {};
    put(p, 4, 9, Kind::General); put(p, 3, 0, Kind::General, Side::Black);
    put(p, 3, 6, Kind::Rook, Side::Black); put(p, 5, 6, Kind::Rook, Side::Black);
    put(p, 0, 7, Kind::Rook, Side::Black); put(p, 8, 6, Kind::Pawn);
    Game loss(Difficulty::Medium, 42, p);
    clickSquare(loss, square(8, 6)); clickSquare(loss, square(8, 5)); awaitAI(loss);
    require(loss.match().outcome() == Outcome::BlackWins, "AI checkmate ends human game");
    loss.undo();
    require(loss.match().position().sameBoard(p) && loss.match().outcome() == Outcome::Playing, "undo after losing restores human decision");
    std::cout << "PASS winning/losing clicks, terminal input lock, undo and restart\n";
}
void motionAndPacing() {
    double time = 10;
    Game game(Difficulty::Easy, 42, Position::initial(), [&] { return time; });
    clickSquare(game, square(1, 7)); clickSquare(game, square(1, 0));
    const auto transition = game.transition();
    require(transition.steps.size() == 1 && transition.steps[0].before.board[square(1, 0)].kind == Kind::Horse,
            "capture keeps victim in the visual snapshot");
    require(transition.steps[0].after.board[square(1, 0)].kind == Kind::Cannon, "capture snapshot contains landed piece");
    require(transition.sample(time).travel == 0, "move begins at the origin");
    const auto midpoint = transition.sample(time + motion::Travel / 2);
    require(midpoint.travel > 0.3f && midpoint.travel < 0.7f && midpoint.landing == 0, "piece travels before impact");
    require(transition.sample(time + motion::Travel + motion::Landing / 2).landing > 0,
            "landing/capture effect has its own phase");
    game.update(); // Launch the very fast easy AI on a copied position.
    for (int i = 0; i < 30; ++i) { game.update(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    require(game.match().moves().size() == 1, "ready AI cannot interrupt human movement");
    clickSquare(game, square(2, 6)); require(game.selected() == -1, "movement locks board selection");
    time += motion::StepDuration + 0.01; game.update();
    require(!game.boardBusy() && game.match().moves().size() == 1, "brief pause after landing before AI reply");
    game.requestNewGame(Difficulty::Hard);
    const auto serial = game.transition().serial;
    game.cancelDialog();
    require(game.transition().serial == serial, "sidebar revisions do not restart piece animation");
    time = 10 + motion::ReplyDelay + 0.01; game.update();
    require(game.match().moves().size() == 2 && game.boardBusy(), "AI reply starts a separate landing transition");
    game.undo();
    require(game.transition().kind == TransitionKind::Undo && game.transition().steps.size() == 2,
            "undo queues AI and player moves in reverse order");
    require(game.transition().steps.back().after.sameBoard(Position::initial()), "undo timeline restores captured horse");
    require(game.transition().sample(time + motion::StepDuration + 0.01).step == 1, "undo proceeds to second half-move");
    time += game.transition().duration() + 0.01; game.update();
    require(!game.boardBusy() && game.match().position().sameBoard(Position::initial()), "undo completes and releases input");
    clickSquare(game, square(0, 6)); require(game.selected() == square(0, 6), "player can select after undo");
    game.requestNewGame(Difficulty::Medium);
    require(game.transition().kind == TransitionKind::Opening && game.match().moves().empty(), "restart replaces old motion timeline");
    Game interrupted(Difficulty::Easy, 42, Position::initial(), [&] { return time; });
    clickSquare(interrupted, square(1, 7)); clickSquare(interrupted, square(1, 0));
    time += motion::Travel / 2;
    interrupted.undo();
    const auto& reverse = interrupted.transition().steps.front();
    require(std::abs(reverse.startRank - 3.5f) < 0.001f && reverse.startFile == 1,
            "mid-move undo starts at the current visual point without teleporting");
    require(reverse.restoredAlpha == 1, "undo before impact keeps the captured target visible");
    std::cout << "PASS capture snapshots, motion phases, AI pacing, input lock and reverse timeline\n";
}
} // namespace
int main() {
    try {
        rules(); outcomesAndHistory(); ai(); interaction(); terminalInteraction(); motionAndPacing();
        std::cout << "PASS " << checks << " checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
