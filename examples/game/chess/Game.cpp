#include "Game.h"
#include <algorithm>
#include <cmath>

namespace chess {
Match::Match(Difficulty difficulty, Position position) : difficulty_(difficulty), history_{position} { adjudicate(); }
void Match::reset(Difficulty difficulty) { *this = Match(difficulty); }

void Match::adjudicate() {
    const auto& p = position();
    checked_ = inCheck(p, p.turn);
    legal_ = legalMoves(p);
    outcome_ = Outcome::Playing;
    claim_ = {};
    auto occurrences = [&](const Position& position) {
        return std::count_if(history_.begin(), history_.end(), [&](const Position& old) { return position.sameBoard(old); });
    };
    if (legal_.empty()) outcome_ = !checked_ ? Outcome::Stalemate : p.turn == Side::White ? Outcome::BlackWins : Outcome::WhiteWins;
    else if (insufficientMaterial(p)) outcome_ = Outcome::MaterialDraw;
    else if (occurrences(p) >= 5) outcome_ = Outcome::RepetitionDraw;
    else if (p.quietPlies >= 150) outcome_ = Outcome::QuietDraw;
    if (outcome_ != Outcome::Playing) return;
    if (occurrences(p) >= 3) claim_ = {Outcome::RepetitionDraw, {}};
    else if (p.quietPlies >= 100) claim_ = {Outcome::QuietDraw, {}};
    else if (history_.size() >= 5 || p.quietPlies >= 99) {
        for (Move move : legal_) {
            const auto next = p.after(move);
            if (occurrences(next) >= 2) { claim_ = {Outcome::RepetitionDraw, move}; break; }
            if (next.quietPlies >= 100) { claim_ = {Outcome::QuietDraw, move}; break; }
        }
    }
}
bool Match::claimDraw() {
    if (outcome_ != Outcome::Playing || !claim_.available()) return false;
    outcome_ = claim_.reason;
    return true;
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
    } while (!moves_.empty() && position().turn != Side::White);
    adjudicate();
    return true;
}

int layout::hitSquare(float x, float y) {
    const int file = static_cast<int>(std::round((x - BoardX) / Cell));
    const int rank = static_cast<int>(std::round((y - BoardY) / Cell));
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return -1;
    const float dx = x - (BoardX + file * Cell), dy = y - (BoardY + rank * Cell);
    return std::abs(dx) <= Cell / 2 && std::abs(dy) <= Cell / 2 ? square(file, rank) : -1;
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
    promotionMove_ = {};
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
void Game::cancelDialog() { cancelPromotion(); if (dialog()) { pendingDifficulty_ = -1; ++revision_; } }
void Game::undo() {
    if (dialog() || !canUndo()) return;
    invalidateSearch();
    const std::size_t count = std::min(match_.moves().size(), match_.position().turn == Side::White ? std::size_t(2) : std::size_t(1));
    Transition reverse{transition_.serial + 1, TransitionKind::Undo, now(), {}};
    for (std::size_t i = 0; i < count; ++i) {
        const Move m = match_.moves()[match_.moves().size() - 1 - i];
        reverse.steps.push_back(makeStep(match_.positionBefore(i), match_.positionBefore(i + 1), {m.to, m.from}, true));
    }
    if (transition_.kind == TransitionKind::Move && transition_.active(now()) && !reverse.steps.empty()) {
        const auto sample = transition_.sample(now());
        const Move move = transition_.steps[sample.step].move;
        auto& first = reverse.steps.front();
        first.startFile = fileOf(move.from) + (fileOf(move.to) - fileOf(move.from)) * sample.travel;
        first.startRank = rankOf(move.from) + (rankOf(move.to) - rankOf(move.from)) * sample.travel;
        first.restoredAlpha = 1 - sample.landing;
        if (first.companion.valid()) {
            const auto companion = transition_.steps[sample.step].companion;
            first.companionStartFile = fileOf(companion.from) + (fileOf(companion.to) - fileOf(companion.from)) * sample.travel;
        }
    }
    match_.undo();
    transition_ = std::move(reverse);
    transitionPending_ = true;
    clearSelection();
    ++revision_;
}
void Game::click(float x, float y) {
    if (promoting()) {
        for (int i = 0; i < 4; ++i) if (layout::Promotion[i].contains(x, y)) choosePromotion(layout::Promotions[i]);
        return;
    }
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
    if (layout::Claim.contains(x, y)) { claimDraw(); return; }
    if (thinking() || boardBusy() || match_.outcome() != Outcome::Playing) return;
    const int s = layout::hitSquare(x, y);
    if (s < 0 || s == selected_) { clearSelection(); return; }
    if (selected_ >= 0 && std::find(destinations_.begin(), destinations_.end(), s) != destinations_.end()) {
        if (match_.position().board[selected_].kind == Kind::Pawn && rankOf(s) == 0) {
            promotionMove_ = {selected_, s}; ++revision_; return;
        }
        animateMove({selected_, s});
        replyAt_ = now() + motion::ReplyDelay;
        clearSelection();
        ++revision_;
        return;
    }
    const Piece piece = match_.position().board[s];
    if (selected_ >= 0 && (!piece || piece.side != Side::White)) {
        rejectionTime_ = now(); rejectedSquare_ = s;
        return;
    }
    clearSelection();
    if (piece && piece.side == Side::White) {
        selected_ = s;
        for (Move m : match_.available()) if (m.from == s && std::find(destinations_.begin(), destinations_.end(), m.to) == destinations_.end()) destinations_.push_back(m.to);
    }
}
void Game::choosePromotion(Kind kind) {
    if (!promoting()) return;
    Move move = promotionMove_; move.promotion = kind;
    if (!isLegal(match_.position(), move)) return;
    animateMove(move); promotionMove_ = {}; clearSelection();
    replyAt_ = now() + motion::ReplyDelay; ++revision_;
}
void Game::cancelPromotion() {
    if (promoting()) { promotionMove_ = {}; clearSelection(); ++revision_; }
}
void Game::claimDraw() {
    if (promoting() || dialog() || boardBusy() || match_.position().turn != Side::White) return;
    if (match_.claimDraw()) { invalidateSearch(); clearSelection(); ++revision_; }
}
void Game::animateMove(Move move) {
    const Position before = match_.position();
    if (!match_.play(move)) return;
    transition_ = {transition_.serial + 1, TransitionKind::Move, now(), {makeStep(before, match_.position(), move)}};
    transitionPending_ = true;
}
int Game::controlAt(float x, float y) const {
    if (promoting()) {
        for (int i = 0; i < 4; ++i) if (layout::Promotion[i].contains(x, y)) return 7 + i;
        return -1;
    }
    if (dialog()) {
        if (layout::Confirm.contains(x, y)) return 5;
        if (layout::Cancel.contains(x, y)) return 6;
        return -1;
    }
    for (int i = 0; i < 3; ++i) if (layout::DifficultyButtons[i].contains(x, y)) return i;
    if (layout::Undo.contains(x, y) && canUndo()) return 3;
    if (layout::Restart.contains(x, y)) return 4;
    if (layout::Claim.contains(x, y) && match_.drawClaim().available() && !thinking() && !boardBusy()
        && match_.outcome() == Outcome::Playing) return 11;
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
    if (thinking() && !dialog() && !boardBusy() && now() >= replyAt_ && match_.drawClaim().available()) {
        match_.claimDraw(); invalidateSearch(); ++revision_;
    }
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
    if (promoting()) return "Choose a promotion piece";
    if (boardBusy()) {
        if (transition_.kind == TransitionKind::Undo) return "Undoing move\u2026";
        return match_.position().turn == Side::Black ? "White is moving\u2026" : "Black is moving\u2026";
    }
    switch (match_.outcome()) {
    case Outcome::WhiteWins: return "Checkmate  \u00b7  White wins";
    case Outcome::BlackWins: return "Checkmate  \u00b7  Black wins";
    case Outcome::Stalemate: return "Draw  \u00b7  Stalemate";
    case Outcome::MaterialDraw: return "Draw  \u00b7  Insufficient material";
    case Outcome::RepetitionDraw: return "Draw  \u00b7  Threefold repetition";
    case Outcome::QuietDraw: return "Draw  \u00b7  Fifty-move rule";
    default:
        if (thinking()) return match_.checked() ? "Black in check  \u00b7  AI thinking" : "AI is thinking\u2026";
        return match_.checked() ? "Check  \u00b7  You must respond" : "Your move";
    }
}
const char* difficultyName(Difficulty d) {
    switch (d) { case Difficulty::Easy: return "Easy"; case Difficulty::Medium: return "Medium"; default: return "Hard"; }
}
} // namespace chess
