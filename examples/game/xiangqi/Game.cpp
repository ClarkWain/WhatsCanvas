#include "Game.h"
#include <algorithm>
#include <cmath>

namespace xiangqi {
Match::Match(Difficulty difficulty, Position position) : difficulty_(difficulty), history_{position} { adjudicate(); }
void Match::reset(Difficulty difficulty) { *this = Match(difficulty); }

void Match::adjudicate() {
    const auto& p = position();
    checked_ = inCheck(p, p.turn);
    legal_ = legalMoves(p);
    outcome_ = Outcome::Playing;
    if (legal_.empty()) outcome_ = p.turn == Side::Red ? Outcome::BlackWins : Outcome::RedWins;
    else if (std::count_if(history_.begin(), history_.end(), [&](const Position& old) { return p.sameBoard(old); }) >= 3)
        outcome_ = Outcome::RepetitionDraw;
    else if (p.quietPlies >= 120) outcome_ = Outcome::QuietDraw;
}

bool Match::play(Move move) {
    if (outcome_ != Outcome::Playing || std::find(legal_.begin(), legal_.end(), move) == legal_.end()) return false;
    history_.push_back(position().after(move));
    moves_.push_back(move);
    adjudicate();
    return true;
}
bool Match::canUndo() const { return difficulty_ != Difficulty::Hard && !moves_.empty(); }
bool Match::undo() {
    if (!canUndo()) return false;
    // Undo to the last human decision, also when the human has just won/lost.
    do {
        history_.pop_back();
        moves_.pop_back();
    } while (!moves_.empty() && position().turn != Side::Red);
    adjudicate();
    return true;
}

int layout::hitSquare(float x, float y) {
    const int file = static_cast<int>(std::round((x - BoardX) / Cell));
    const int rank = static_cast<int>(std::round((y - BoardY) / Cell));
    if (file < 0 || file > 8 || rank < 0 || rank > 9) return -1;
    const float dx = x - (BoardX + file * Cell), dy = y - (BoardY + rank * Cell);
    return dx * dx + dy * dy <= 29 * 29 ? square(file, rank) : -1;
}
layout::Viewport layout::Viewport::fit(float width, float height) {
    const float scale = std::max(0.01f, std::min(width / Width, height / Height));
    return {scale, (width - Width * scale) * 0.5f, (height - Height * scale) * 0.5f};
}

double Game::steadyTime() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
Game::Game(Difficulty difficulty, std::uint32_t seed, Position initialPosition, Clock clock)
    : match_(difficulty, initialPosition), seed_(seed), clock_(std::move(clock)) {
    transition_ = {1, TransitionKind::Opening, now(), {}};
}
Game::~Game() {
    invalidateSearch();
    if (job_.valid()) job_.wait();
}
void Game::invalidateSearch() {
    ++generation_;
    if (cancel_) cancel_->store(true);
}
void Game::clearSelection() { selected_ = -1; destinations_.clear(); }
void Game::startNewGame(Difficulty difficulty) {
    invalidateSearch();
    match_.reset(difficulty);
    clearSelection();
    pendingDifficulty_ = -1;
    lastSearch_ = {};
    transition_ = {transition_.serial + 1, TransitionKind::Opening, now(), {}};
    transitionPending_ = false;
    openingPresented_ = false;
    rejectionTime_ = -100;
    ++revision_;
}
void Game::requestNewGame(Difficulty difficulty) {
    if (match_.moves().empty() || match_.outcome() != Outcome::Playing) startNewGame(difficulty);
    else { pendingDifficulty_ = static_cast<int>(difficulty); clearSelection(); ++revision_; }
}
void Game::confirmNewGame() { if (dialog()) startNewGame(pendingDifficulty()); }
void Game::cancelDialog() { if (dialog()) { pendingDifficulty_ = -1; ++revision_; } }
void Game::undo() {
    if (dialog() || !canUndo()) return;
    invalidateSearch();
    const std::size_t count = std::min(match_.moves().size(), match_.position().turn == Side::Red ? std::size_t(2) : std::size_t(1));
    Transition reverse{transition_.serial + 1, TransitionKind::Undo, now(), {}};
    for (std::size_t i = 0; i < count; ++i) {
        const Move m = match_.moves()[match_.moves().size() - 1 - i];
        reverse.steps.push_back({match_.positionBefore(i), match_.positionBefore(i + 1), {m.to, m.from}, true});
    }
    if (transition_.kind == TransitionKind::Move && transition_.active(now()) && !reverse.steps.empty()) {
        const auto sample = transition_.sample(now());
        const Move move = transition_.steps[sample.step].move;
        auto& first = reverse.steps.front();
        first.startFile = fileOf(move.from) + (fileOf(move.to) - fileOf(move.from)) * sample.travel;
        first.startRank = rankOf(move.from) + (rankOf(move.to) - rankOf(move.from)) * sample.travel;
        first.restoredAlpha = 1 - sample.landing;
    }
    match_.undo();
    transition_ = std::move(reverse);
    transitionPending_ = true;
    clearSelection();
    ++revision_;
}
void Game::click(float x, float y) {
    if (dialog()) {
        if (layout::Confirm.contains(x, y)) confirmNewGame();
        else if (layout::Cancel.contains(x, y)) cancelDialog();
        return;
    }
    for (int i = 0; i < 3; ++i) if (layout::DifficultyButtons[i].contains(x, y)) {
        if (match_.difficulty() != static_cast<Difficulty>(i)) requestNewGame(static_cast<Difficulty>(i));
        return;
    }
    if (layout::Undo.contains(x, y)) { undo(); return; }
    if (layout::Restart.contains(x, y)) { requestNewGame(match_.difficulty()); return; }
    if (thinking() || boardBusy() || match_.outcome() != Outcome::Playing) return;
    const int s = layout::hitSquare(x, y);
    if (s < 0 || s == selected_) { clearSelection(); return; }
    if (selected_ >= 0 && std::find(destinations_.begin(), destinations_.end(), s) != destinations_.end()) {
        animateMove({selected_, s});
        replyAt_ = now() + motion::ReplyDelay;
        clearSelection();
        ++revision_;
        return;
    }
    const Piece piece = match_.position().board[s];
    if (selected_ >= 0 && (!piece || piece.side != Side::Red)) {
        rejectionTime_ = now(); rejectedSquare_ = s;
        return;
    }
    clearSelection();
    if (piece && piece.side == Side::Red) {
        selected_ = s;
        for (Move m : match_.available()) if (m.from == s) destinations_.push_back(m.to);
    }
}
void Game::animateMove(Move move) {
    const Position before = match_.position();
    if (!match_.play(move)) return;
    transition_ = {transition_.serial + 1, TransitionKind::Move, now(), {{before, match_.position(), move, false}}};
    transitionPending_ = true;
}
int Game::controlAt(float x, float y) const {
    if (dialog()) {
        if (layout::Confirm.contains(x, y)) return 5;
        if (layout::Cancel.contains(x, y)) return 6;
        return -1;
    }
    for (int i = 0; i < 3; ++i) if (layout::DifficultyButtons[i].contains(x, y)) return i;
    if (layout::Undo.contains(x, y) && canUndo()) return 3;
    if (layout::Restart.contains(x, y)) return 4;
    return -1;
}
void Game::beginPresentation() {
    // Start the entrance after initial artwork/font baking, not while the window
    // is still loading. Subsequent frames never rewind a running transition.
    if (!openingPresented_ && transition_.kind == TransitionKind::Opening) {
        transition_.started = now(); openingPresented_ = true;
    }
}
void Game::update() {
    if (transitionPending_ && !transition_.active(now())) { transitionPending_ = false; ++revision_; }
    if (job_.valid()) {
        if (job_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready || dialog()) return;
        if (jobGeneration_ == generation_ && (now() < replyAt_ || boardBusy())) return;
        const auto result = job_.get();
        if (jobGeneration_ == generation_ && thinking() && isLegal(match_.position(), result.move)) {
            animateMove(result.move);
            lastSearch_ = result;
            clearSelection();
            ++revision_;
        }
    }
    if (thinking() && !dialog() && !job_.valid()) {
        const Position position = match_.position();
        const Difficulty difficulty = match_.difficulty();
        const auto seed = seed_ + static_cast<std::uint32_t>(match_.moves().size() * 7919);
        cancel_ = std::make_shared<std::atomic_bool>(false);
        jobGeneration_ = generation_;
        job_ = std::async(std::launch::async, [position, difficulty, seed, cancel = cancel_] {
            return chooseMove(position, difficulty, seed, *cancel);
        });
    }
}
std::string Game::status() const {
    if (boardBusy()) {
        if (transition_.kind == TransitionKind::Undo) return "正在悔棋…";
        return match_.position().turn == Side::Black ? "紅方落子…" : "黑方落子…";
    }
    switch (match_.outcome()) {
    case Outcome::RedWins: return "你贏了 · 紅方勝";
    case Outcome::BlackWins: return "對局結束 · 黑方勝";
    case Outcome::RepetitionDraw: return "和棋 · 三次重複局面";
    case Outcome::QuietDraw: return "和棋 · 60 回合無進展";
    default:
        if (thinking()) return match_.checked() ? "黑方被將軍 · AI 思考中" : "AI 正在思考…";
        return match_.checked() ? "你被將軍了 · 請應將" : "輪到你走棋";
    }
}
const char* difficultyName(Difficulty d) {
    switch (d) { case Difficulty::Easy: return "簡單"; case Difficulty::Medium: return "中等"; default: return "困難"; }
}
} // namespace xiangqi
