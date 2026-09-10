#pragma once
#include "AI.h"
#include "Motion.h"
#include <functional>
#include <future>
#include <memory>
#include <string>

namespace chess {
enum class Outcome { Playing, WhiteWins, BlackWins, Stalemate, MaterialDraw, RepetitionDraw, QuietDraw };
struct DrawClaim {
    Outcome reason = Outcome::Playing;
    Move intended; // Empty for a claim on the current position.
    bool available() const { return reason != Outcome::Playing; }
};

// Match owns history and adjudication; it has no rendering or threading dependencies.
class Match {
public:
    explicit Match(Difficulty difficulty = Difficulty::Medium, Position position = Position::initial());
    void reset(Difficulty difficulty);
    bool play(Move move);
    bool undo();
    bool canUndo() const;
    const Position& position() const { return history_.back(); }
    const Position& positionBefore(std::size_t plies) const { return history_.at(history_.size() - 1 - plies); }
    const std::vector<Move>& available() const { return legal_; }
    const std::vector<Move>& moves() const { return moves_; }
    Difficulty difficulty() const { return difficulty_; }
    Outcome outcome() const { return outcome_; }
    bool checked() const { return checked_; }
    const DrawClaim& drawClaim() const { return claim_; }
    bool claimDraw();
private:
    void adjudicate();
    Difficulty difficulty_;
    std::vector<Position> history_;
    std::vector<Move> moves_;
    std::vector<Move> legal_;
    Outcome outcome_ = Outcome::Playing;
    bool checked_ = false;
    DrawClaim claim_;
};

struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
namespace layout {
constexpr float Width = 1120, Height = 820;
constexpr float BoardX = 98, BoardY = 170, Cell = 72;
constexpr Rect DifficultyButtons[] = {{732, 282, 98, 44}, {840, 282, 98, 44}, {948, 282, 98, 44}};
constexpr Rect Undo{732, 572, 314, 46};
constexpr Rect Restart{732, 632, 314, 46};
constexpr Rect Confirm{732, 694, 148, 44};
constexpr Rect Cancel{898, 694, 148, 44};
constexpr Rect Promotion[] = {{732, 520, 70, 40}, {813, 520, 70, 40}, {894, 520, 70, 40}, {975, 520, 70, 40}};
constexpr Rect Claim{732, 692, 314, 44};
constexpr Kind Promotions[] = {Kind::Queen, Kind::Rook, Kind::Bishop, Kind::Knight};
int hitSquare(float x, float y);
struct Viewport {
    float scale, x, y;
    static Viewport fit(float width, float height);
    std::pair<float, float> toDesign(float px, float py) const { return {(px - x) / scale, (py - y) / scale}; }
};
} // namespace layout

// UI-thread controller. The worker receives only a copied Position and cancellation
// flag. A generation check prevents stale replies after undo/restart/difficulty changes.
class Game {
public:
    using Clock = std::function<double()>;
    static double steadyTime();
    explicit Game(Difficulty difficulty = Difficulty::Medium, std::uint32_t seed = 2026,
                  Position initialPosition = Position::initial(), Clock clock = steadyTime);
    ~Game();
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    void click(float x, float y);
    void update();
    void beginPresentation();
    void undo();
    void requestNewGame(Difficulty difficulty);
    void confirmNewGame();
    void cancelDialog();
    void clearSelection();
    bool promoting() const { return promotionMove_.valid(); }
    void choosePromotion(Kind kind);
    void cancelPromotion();
    void claimDraw();
    const Match& match() const { return match_; }
    int selected() const { return selected_; }
    const std::vector<int>& destinations() const { return destinations_; }
    std::uint64_t revision() const { return revision_; }
    bool thinking() const { return match_.outcome() == Outcome::Playing && match_.position().turn == Side::Black; }
    bool dialog() const { return pendingDifficulty_ >= 0; }
    Difficulty pendingDifficulty() const { return static_cast<Difficulty>(pendingDifficulty_); }
    const SearchResult& lastSearch() const { return lastSearch_; }
    std::string status() const;
    double now() const { return clock_(); }
    const Transition& transition() const { return transition_; }
    bool boardBusy() const { return transition_.kind != TransitionKind::Opening && transition_.active(now()); }
    bool canUndo() const { return match_.canUndo() && !(transition_.kind == TransitionKind::Undo && transition_.active(now())); }
    int controlAt(float x, float y) const;
    double rejectionTime() const { return rejectionTime_; }
    int rejectedSquare() const { return rejectedSquare_; }
private:
    void animateMove(Move move);
    void invalidateSearch();
    void startNewGame(Difficulty difficulty);
    Match match_;
    int selected_ = -1;
    std::vector<int> destinations_;
    std::uint32_t seed_;
    std::uint64_t generation_ = 0, jobGeneration_ = 0, revision_ = 1;
    int pendingDifficulty_ = -1;
    std::shared_ptr<std::atomic_bool> cancel_;
    std::future<SearchResult> job_;
    SearchResult lastSearch_;
    Clock clock_;
    Transition transition_;
    bool transitionPending_ = false;
    bool openingPresented_ = false;
    double replyAt_ = 0, rejectionTime_ = -100;
    int rejectedSquare_ = -1;
    Move promotionMove_;
};
const char* difficultyName(Difficulty difficulty);
} // namespace chess
