#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "canvas/Canvas.h"
#include "canvas/Paint.h"
#include "canvas/base.h"

namespace {

constexpr int kLaneCount = 3;

constexpr int DESIGN_W = 686;
constexpr int DESIGN_H = 960;

constexpr float MINI_PANEL_X = 28.0f;
constexpr float MINI_PANEL_Y = 112.0f;
constexpr float MINI_PANEL_W = 52.0f;
constexpr float MINI_PANEL_H = 760.0f;

constexpr float ROAD_X = 170.0f;
constexpr float ROAD_Y = 36.0f;
constexpr float ROAD_W = 238.0f;
constexpr float ROAD_H = 888.0f;
constexpr float VERGE_W = 62.0f;

constexpr float SIDEBAR_X = 498.0f;
constexpr float SIDEBAR_Y = 56.0f;
constexpr float SIDEBAR_W = 156.0f;
constexpr float SIDEBAR_H = 848.0f;

constexpr float PLAYER_CAR_W = 46.0f;
constexpr float PLAYER_CAR_H = 84.0f;
constexpr float TRAFFIC_CAR_W = 44.0f;
constexpr float TRAFFIC_CAR_H = 78.0f;
constexpr float FUEL_PICKUP_W = 32.0f;
constexpr float FUEL_PICKUP_H = 36.0f;
constexpr float kMinSpeedKmh = 92.0f;
constexpr float kMaxSpeedKmh = 286.0f;
constexpr float kStartSpeedKmh = 156.0f;
constexpr float kTrafficSameLaneGap = 292.0f;
constexpr float kTrafficCrossLaneGap = 214.0f;
constexpr float kTrafficDangerZoneTopInset = 250.0f;
constexpr float kTrafficDangerZoneBottomInset = 54.0f;

struct Bounds {
    float x;
    float y;
    float width;
    float height;
};

struct TrafficCar {
    int lane = 1;
    float y = 0.0f;
    float speedPx = 0.0f;
    Color bodyColor;
    bool counted = false;
};

struct FuelPickup {
    int lane = 1;
    float y = 0.0f;
    float bobPhase = 0.0f;
};

static const std::array<Color, 6> kTrafficPalette = {
    Color(242, 182, 38),
    Color(72, 178, 232),
    Color(242, 91, 68),
    Color(124, 219, 124),
    Color(244, 241, 236),
    Color(176, 126, 242),
};

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float wrapValue(float value, float period) {
    if (period <= 0.0f) {
        return 0.0f;
    }
    value = std::fmod(value, period);
    if (value < 0.0f) {
        value += period;
    }
    return value;
}

bool intersects(const Bounds& lhs, const Bounds& rhs) {
    return !(lhs.x > rhs.x + rhs.width ||
             lhs.x + lhs.width < rhs.x ||
             lhs.y > rhs.y + rhs.height ||
             lhs.y + lhs.height < rhs.y);
}

Color shadeColor(const Color& color, float factor, int alpha = 255) {
    const int r = std::clamp(static_cast<int>(std::lround(color.getR() * factor)), 0, 255);
    const int g = std::clamp(static_cast<int>(std::lround(color.getG() * factor)), 0, 255);
    const int b = std::clamp(static_cast<int>(std::lround(color.getB() * factor)), 0, 255);
    return Color(r, g, b, alpha);
}

std::string getEnvironmentValue(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, name) != 0 || value == nullptr) {
        return std::string();
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

bool parseFloat(const std::string& text, float& value) {
    if (text.empty()) {
        return false;
    }

    char* end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }

    value = parsed;
    return true;
}

class RacerGame {
public:
    RacerGame() {
        std::srand(static_cast<unsigned>(std::time(nullptr)));
        restart();
    }

    void moveLeft() {
        if (state_ != PLAYING) {
            return;
        }
        playerLane_ = std::max(0, playerLane_ - 1);
    }

    void moveRight() {
        if (state_ != PLAYING) {
            return;
        }
        playerLane_ = std::min(kLaneCount - 1, playerLane_ + 1);
    }

    void setAccelerating(bool active) {
        accelerating_ = active;
    }

    void setBraking(bool active) {
        braking_ = active;
    }

    void togglePause() {
        if (state_ == PLAYING) {
            state_ = PAUSED;
        } else if (state_ == PAUSED) {
            state_ = PLAYING;
        }
    }

    void restart() {
        state_ = PLAYING;
        playerLane_ = 1;
        playerVisualLane_ = 1.0f;
        accelerating_ = false;
        braking_ = false;
        currentSpeedKmh_ = kStartSpeedKmh;
        targetSpeedKmh_ = kStartSpeedKmh;
        roadScroll_ = 0.0f;
        sceneryScroll_ = 0.0f;
        distanceKm_ = 0.0f;
        score_ = 0;
        scoreRemainder_ = 0.0f;
        fuel_ = 100.0f;
        trafficSpawnTimer_ = 0.55f;
        fuelSpawnTimer_ = 5.5f;
        displayedFps_ = 0.0f;
        latestFrameMs_ = 0.0f;
        fpsAccumulatedTime_ = 0.0f;
        fpsAccumulatedFrames_ = 0;
        elapsed_ = 0.0f;
        dodgedCars_ = 0;
        gameOverReason_ = "Avoid traffic. Keep moving.";
        traffic_.clear();
        fuelPickups_.clear();
        spawnTraffic(-260.0f);
        spawnTraffic(-520.0f);
    }

    void update(float dt) {
        updatePerformanceStats(dt);
        if (!std::isfinite(dt) || dt <= 0.0f) {
            return;
        }

        const float clampedDt = std::min(dt, 0.1f);
        playerVisualLane_ += (static_cast<float>(playerLane_) - playerVisualLane_) * std::min(1.0f, clampedDt * 12.0f);

        if (state_ != PLAYING) {
            return;
        }

        elapsed_ += clampedDt;

        const float difficulty = clamp01(distanceKm_ / 2.4f);
        const float cruiseSpeed = 138.0f + difficulty * 50.0f + std::min(26.0f, static_cast<float>(dodgedCars_) * 0.65f);
        float requestedSpeed = cruiseSpeed;
        if (accelerating_) {
            requestedSpeed += 82.0f;
        }
        if (braking_) {
            requestedSpeed -= 66.0f;
        }
        targetSpeedKmh_ = std::clamp(requestedSpeed, kMinSpeedKmh, kMaxSpeedKmh);
        const float speedResponse = targetSpeedKmh_ > currentSpeedKmh_ ? 5.8f : 4.1f;
        currentSpeedKmh_ += (targetSpeedKmh_ - currentSpeedKmh_) * std::min(1.0f, clampedDt * speedResponse);

        const float roadPxPerSecond = roadPixelsPerSecond();
        roadScroll_ = wrapValue(roadScroll_ + roadPxPerSecond * clampedDt, 132.0f);
        sceneryScroll_ = wrapValue(sceneryScroll_ + roadPxPerSecond * 0.42f * clampedDt, 154.0f);

        distanceKm_ += currentSpeedKmh_ * clampedDt / 3600.0f;

        scoreRemainder_ += currentSpeedKmh_ * clampedDt * 1.75f;
        while (scoreRemainder_ >= 1.0f) {
            ++score_;
            scoreRemainder_ -= 1.0f;
        }

        fuel_ -= clampedDt * (1.2f + currentSpeedKmh_ * 0.0065f + (accelerating_ ? 0.42f : 0.0f));
        if (fuel_ <= 0.0f) {
            fuel_ = 0.0f;
            currentSpeedKmh_ = 0.0f;
            state_ = GAME_OVER;
            gameOverReason_ = "Out of fuel. Press R to restart";
            return;
        }

        trafficSpawnTimer_ -= clampedDt;
        if (trafficSpawnTimer_ <= 0.0f) {
            if (spawnTraffic()) {
                const float baseGap = 0.98f - difficulty * 0.30f;
                trafficSpawnTimer_ = std::clamp(baseGap + randomRange(-0.12f, 0.16f), 0.38f, 1.05f);
            } else {
                trafficSpawnTimer_ = 0.18f;
            }
        }

        fuelSpawnTimer_ -= clampedDt;
        if (fuelSpawnTimer_ <= 0.0f && fuel_ < 92.0f) {
            spawnFuel();
            fuelSpawnTimer_ = randomRange(5.2f, 8.0f);
        }

        updateTraffic(clampedDt);
        updateFuelPickups(clampedDt);
    }

    void render(Canvas& canvas, int windowW, int windowH) {
        drawBackdrop(canvas);

        const float scale = std::min(static_cast<float>(windowW) / DESIGN_W,
                                     static_cast<float>(windowH) / DESIGN_H);
        const float offsetX = (windowW - DESIGN_W * scale) * 0.5f;
        const float offsetY = (windowH - DESIGN_H * scale) * 0.5f;

        canvas.save();
        canvas.translate(offsetX, offsetY);
        canvas.scale(scale, scale);

        drawSpeedStrip(canvas);
        drawRoad(canvas);

        const int trafficClipCount = canvas.save();
        canvas.clipRect(roadContentRect());
        drawFuelPickups(canvas);
        drawTraffic(canvas);
        drawPlayer(canvas);
        canvas.restoreToCount(trafficClipCount);

        drawSidebar(canvas);
        drawStatusBanner(canvas);

        if (state_ == GAME_OVER) {
            drawOverlay(canvas, "WRECKED", gameOverReason_);
        } else if (state_ == PAUSED) {
            drawOverlay(canvas, "PAUSED", "Press P to resume");
        }

        canvas.restore();
    }

    int getScore() const {
        return score_;
    }

private:
    enum State {
        PLAYING,
        PAUSED,
        GAME_OVER,
    };

    State state_ = PLAYING;
    int playerLane_ = 1;
    float playerVisualLane_ = 1.0f;
    bool accelerating_ = false;
    bool braking_ = false;
    float currentSpeedKmh_ = kStartSpeedKmh;
    float targetSpeedKmh_ = kStartSpeedKmh;
    float roadScroll_ = 0.0f;
    float sceneryScroll_ = 0.0f;
    float distanceKm_ = 0.0f;
    int score_ = 0;
    float scoreRemainder_ = 0.0f;
    float fuel_ = 100.0f;
    float trafficSpawnTimer_ = 0.55f;
    float fuelSpawnTimer_ = 5.5f;
    float displayedFps_ = 0.0f;
    float latestFrameMs_ = 0.0f;
    float fpsAccumulatedTime_ = 0.0f;
    int fpsAccumulatedFrames_ = 0;
    float elapsed_ = 0.0f;
    int dodgedCars_ = 0;
    std::string gameOverReason_;
    std::vector<TrafficCar> traffic_;
    std::vector<FuelPickup> fuelPickups_;

    float randomRange(float minValue, float maxValue) const {
        const float t = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        return minValue + (maxValue - minValue) * t;
    }

    void updatePerformanceStats(float dt) {
        if (!std::isfinite(dt) || dt <= 0.0f) {
            return;
        }

        const float clampedDt = std::min(dt, 0.25f);
        latestFrameMs_ = clampedDt * 1000.0f;
        fpsAccumulatedTime_ += clampedDt;
        ++fpsAccumulatedFrames_;

        if (fpsAccumulatedTime_ >= 0.25f) {
            displayedFps_ = static_cast<float>(fpsAccumulatedFrames_) / fpsAccumulatedTime_;
            fpsAccumulatedTime_ = 0.0f;
            fpsAccumulatedFrames_ = 0;
        }
    }

    float roadInnerX() const {
        return ROAD_X + 22.0f;
    }

    float roadInnerWidth() const {
        return ROAD_W - 44.0f;
    }

    float laneWidth() const {
        return roadInnerWidth() / static_cast<float>(kLaneCount);
    }

    float laneCenter(float laneValue) const {
        return roadInnerX() + laneWidth() * (laneValue + 0.5f);
    }

    float playerCenterY() const {
        return ROAD_Y + ROAD_H - 124.0f;
    }

    RectF roadContentRect() const {
        return RectF(ROAD_X, ROAD_Y, ROAD_W, ROAD_H);
    }

    RectF roadWithVergeRect() const {
        return RectF(ROAD_X - VERGE_W, ROAD_Y, ROAD_W + VERGE_W * 2.0f, ROAD_H);
    }

    float speedRatio() const {
        return clamp01((currentSpeedKmh_ - kMinSpeedKmh) / (kMaxSpeedKmh - kMinSpeedKmh));
    }

    float roadPixelsPerSecond() const {
        const float normalized = speedRatio();
        return 165.0f + 110.0f * normalized + 365.0f * normalized * normalized;
    }

    Bounds playerBounds() const {
        const float centerX = laneCenter(playerVisualLane_);
        const float centerY = playerCenterY();
        return Bounds{centerX - 18.0f, centerY - 34.0f, 36.0f, 68.0f};
    }

    Bounds trafficBounds(const TrafficCar& car) const {
        const float centerX = laneCenter(static_cast<float>(car.lane));
        return Bounds{centerX - 18.0f, car.y + 8.0f, 36.0f, 64.0f};
    }

    Bounds fuelBounds(const FuelPickup& pickup) const {
        const float centerX = laneCenter(static_cast<float>(pickup.lane));
        return Bounds{centerX - 14.0f, pickup.y + 8.0f, 28.0f, 28.0f};
    }

    bool overlapsVerticalBand(const Bounds& bounds, float topY, float bottomY) const {
        return bounds.y < bottomY && bounds.y + bounds.height > topY;
    }

    bool canSpawnTrafficInLane(int lane, float spawnY) const {
        for (const TrafficCar& car : traffic_) {
            const float gap = std::fabs(car.y - spawnY);
            if (car.lane == lane) {
                if (gap < kTrafficSameLaneGap) {
                    return false;
                }
            } else if (gap < kTrafficCrossLaneGap) {
                return false;
            }
        }

        return true;
    }

    int chooseTrafficLane(float spawnY) const {
        std::array<int, kLaneCount> available{};
        int availableCount = 0;
        for (int lane = 0; lane < kLaneCount; ++lane) {
            if (canSpawnTrafficInLane(lane, spawnY)) {
                available[availableCount++] = lane;
            }
        }

        if (availableCount == 0) {
            return -1;
        }

        return available[std::rand() % availableCount];
    }

    int chooseFuelLane(float spawnY) const {
        std::array<int, kLaneCount> available{};
        int availableCount = 0;
        for (int lane = 0; lane < kLaneCount; ++lane) {
            bool blocked = false;
            for (const TrafficCar& car : traffic_) {
                if (car.lane == lane && std::fabs(car.y - spawnY) < 160.0f) {
                    blocked = true;
                    break;
                }
            }
            for (const FuelPickup& pickup : fuelPickups_) {
                if (pickup.lane == lane && std::fabs(pickup.y - spawnY) < 120.0f) {
                    blocked = true;
                    break;
                }
            }
            if (!blocked) {
                available[availableCount++] = lane;
            }
        }

        if (availableCount == 0) {
            return std::rand() % kLaneCount;
        }

        return available[std::rand() % availableCount];
    }

    bool spawnTraffic(float spawnY = ROAD_Y - 110.0f) {
        if (traffic_.size() >= 8) {
            return false;
        }

        const int lane = chooseTrafficLane(spawnY);
        if (lane < 0) {
            return false;
        }

        TrafficCar car;
        car.lane = lane;
        car.y = spawnY;
        car.speedPx = randomRange(20.0f, 96.0f);
        car.bodyColor = kTrafficPalette[std::rand() % static_cast<int>(kTrafficPalette.size())];
        car.counted = false;
        traffic_.push_back(car);
        return true;
    }

    void spawnFuel(float spawnY = ROAD_Y - 74.0f) {
        if (fuelPickups_.size() >= 2) {
            return;
        }

        FuelPickup pickup;
        pickup.lane = chooseFuelLane(spawnY);
        pickup.y = spawnY;
        pickup.bobPhase = randomRange(0.0f, 6.28318f);
        fuelPickups_.push_back(pickup);
    }

    void updateTraffic(float dt) {
        const float roadSpeed = roadPixelsPerSecond();

        for (TrafficCar& car : traffic_) {
            car.y += dt * (roadSpeed * 0.82f + car.speedPx);
            if (!car.counted && car.y > playerCenterY() + 80.0f) {
                car.counted = true;
                ++dodgedCars_;
                score_ += 35 + static_cast<int>(currentSpeedKmh_ * 0.08f);
            }
        }

        enforceTrafficFairness();

        if (state_ == PLAYING) {
            const Bounds playerBox = playerBounds();
            for (const TrafficCar& car : traffic_) {
                if (intersects(playerBox, trafficBounds(car))) {
                    state_ = GAME_OVER;
                    currentSpeedKmh_ *= 0.35f;
                    gameOverReason_ = "Traffic hit. Press R to restart";
                    break;
                }
            }
        }

        traffic_.erase(std::remove_if(traffic_.begin(), traffic_.end(), [](const TrafficCar& car) {
            return car.y > ROAD_Y + ROAD_H + 140.0f;
        }), traffic_.end());
    }

    void enforceTrafficFairness() {
        std::array<int, kLaneCount> laneIndices{};
        laneIndices.fill(-1);

        const float zoneTop = playerCenterY() - kTrafficDangerZoneTopInset;
        const float zoneBottom = playerCenterY() + kTrafficDangerZoneBottomInset;
        int occupiedLanes = 0;

        for (int index = 0; index < static_cast<int>(traffic_.size()); ++index) {
            const TrafficCar& car = traffic_[index];
            if (laneIndices[car.lane] >= 0) {
                continue;
            }

            if (overlapsVerticalBand(trafficBounds(car), zoneTop, zoneBottom)) {
                laneIndices[car.lane] = index;
                ++occupiedLanes;
            }
        }

        if (occupiedLanes < kLaneCount) {
            return;
        }

        int indexToMove = laneIndices[0];
        for (int lane = 1; lane < kLaneCount; ++lane) {
            if (traffic_[laneIndices[lane]].y < traffic_[indexToMove].y) {
                indexToMove = laneIndices[lane];
            }
        }

        traffic_[indexToMove].y = zoneTop - TRAFFIC_CAR_H - randomRange(36.0f, 92.0f);
        traffic_[indexToMove].counted = false;
    }

    void updateFuelPickups(float dt) {
        const Bounds playerBox = playerBounds();
        const float roadSpeed = roadPixelsPerSecond();

        for (FuelPickup& pickup : fuelPickups_) {
            pickup.bobPhase += dt * 4.5f;
            pickup.y += dt * (roadSpeed * 0.88f + 24.0f);
        }

        fuelPickups_.erase(std::remove_if(fuelPickups_.begin(), fuelPickups_.end(), [&](const FuelPickup& pickup) {
            if (state_ == PLAYING && intersects(playerBox, fuelBounds(pickup))) {
                fuel_ = std::min(100.0f, fuel_ + 28.0f);
                score_ += 120;
                return true;
            }
            return pickup.y > ROAD_Y + ROAD_H + 100.0f;
        }), fuelPickups_.end());
    }

    void drawBackdrop(Canvas& canvas) {
        Paint backdrop;
        backdrop.setStyle(Paint::Style::FILL);
        backdrop.setLinearGradient(0.0f, 0.0f, 0.0f, static_cast<float>(DESIGN_H), {
            Paint::ColorStop(0.0f, Color(7, 10, 17)),
            Paint::ColorStop(0.48f, Color(10, 14, 22)),
            Paint::ColorStop(1.0f, Color(3, 5, 10))
        });
        canvas.drawRect(RectF(0.0f, 0.0f, static_cast<float>(DESIGN_W), static_cast<float>(DESIGN_H)), backdrop);

        Paint glow;
        glow.setStyle(Paint::Style::FILL);
        glow.setFillColor(Color(255, 178, 62, 22));
        canvas.drawCircle(PointF(116.0f, 122.0f), 104.0f, glow);
        glow.setFillColor(Color(82, 140, 255, 18));
        canvas.drawCircle(PointF(static_cast<float>(DESIGN_W) - 104.0f, 158.0f), 124.0f, glow);
        glow.setFillColor(Color(105, 255, 173, 16));
        canvas.drawCircle(PointF(static_cast<float>(DESIGN_W) - 72.0f, static_cast<float>(DESIGN_H) - 102.0f), 146.0f, glow);
    }

    void drawSpeedStrip(Canvas& canvas) {
        const RectF panel(MINI_PANEL_X, MINI_PANEL_Y, MINI_PANEL_W, MINI_PANEL_H);

        Paint fill;
        fill.setStyle(Paint::Style::FILL);
        fill.setLinearGradient(panel.getX(), panel.getY(), panel.getX(), panel.getY() + panel.getHeight(), {
            Paint::ColorStop(0.0f, Color(30, 42, 74, 235)),
            Paint::ColorStop(0.38f, Color(18, 27, 48, 232)),
            Paint::ColorStop(1.0f, Color(8, 13, 24, 235))
        });
        canvas.drawRoundRect(panel, 22.0f, fill);

        Paint glow;
        glow.setStyle(Paint::Style::FILL);
        glow.setFillColor(Color(64, 120, 255, 24));
        canvas.drawRoundRect(RectF(panel.getX() + 5.0f, panel.getY() + 8.0f, panel.getWidth() - 10.0f, 160.0f), 18.0f, glow);
        glow.setFillColor(Color(255, 176, 82, 18));
        canvas.drawRoundRect(RectF(panel.getX() + 8.0f, panel.getY() + panel.getHeight() - 180.0f, panel.getWidth() - 16.0f, 140.0f), 18.0f, glow);

        Paint stroke;
        stroke.setStyle(Paint::Style::STROKE);
        stroke.setStrokeWidth(2.5f);
        stroke.setStrokeColor(Color(96, 126, 186, 186));
        canvas.drawRoundRect(panel, 22.0f, stroke);

        Paint panelGlass;
        panelGlass.setStyle(Paint::Style::FILL);
        panelGlass.setLinearGradient(panel.getX(), panel.getY() + 12.0f, panel.getX() + panel.getWidth(), panel.getY() + 120.0f, {
            Paint::ColorStop(0.0f, Color(255, 255, 255, 52)),
            Paint::ColorStop(1.0f, Color(255, 255, 255, 0))
        });
        canvas.drawRoundRect(RectF(panel.getX() + 4.0f, panel.getY() + 4.0f, panel.getWidth() - 8.0f, 148.0f), 18.0f, panelGlass);

        Paint label;
        label.setStyle(Paint::Style::FILL);
        label.setColor(Color(226, 234, 252));
        label.setTextSize(11.0f);
        label.setLetterSpacing(1.0f);
        canvas.drawText("SPD", panel.getX() + 8.0f, panel.getY() + 16.0f, label);

        const RectF track(panel.getX() + 16.0f, panel.getY() + 48.0f, panel.getWidth() - 32.0f, panel.getHeight() - 96.0f);
        Paint trackFill;
        trackFill.setStyle(Paint::Style::FILL);
        trackFill.setLinearGradient(track.getX(), track.getY(), track.getX(), track.getY() + track.getHeight(), {
            Paint::ColorStop(0.0f, Color(15, 22, 38, 240)),
            Paint::ColorStop(0.45f, Color(10, 16, 30, 236)),
            Paint::ColorStop(1.0f, Color(6, 10, 22, 242))
        });
        canvas.drawRoundRect(track, 12.0f, trackFill);

        Paint trackInset;
        trackInset.setStyle(Paint::Style::STROKE);
        trackInset.setStrokeWidth(1.0f);
        trackInset.setStrokeColor(Color(160, 190, 255, 34));
        canvas.drawRoundRect(RectF(track.getX() + 1.0f, track.getY() + 1.0f, track.getWidth() - 2.0f, track.getHeight() - 2.0f), 11.0f, trackInset);

        Paint trackStroke;
        trackStroke.setStyle(Paint::Style::STROKE);
        trackStroke.setStrokeWidth(2.0f);
        trackStroke.setStrokeColor(Color(66, 86, 132));
        canvas.drawRoundRect(track, 12.0f, trackStroke);

        Paint tick;
        tick.setStyle(Paint::Style::FILL);
        tick.setFillColor(Color(142, 168, 220, 44));
        for (int index = 1; index < 8; ++index) {
            const float tickY = track.getY() + track.getHeight() * (static_cast<float>(index) / 8.0f);
            canvas.drawRoundRect(RectF(track.getX() + 4.0f, tickY - 1.0f, track.getWidth() - 8.0f, 2.0f), 1.0f, tick);
        }

        const float speedRatio = this->speedRatio();
        const float fillHeight = (track.getHeight() - 8.0f) * speedRatio;
        const RectF speedFill(track.getX() + 4.0f,
                              track.getY() + track.getHeight() - 4.0f - fillHeight,
                              track.getWidth() - 8.0f,
                              fillHeight);
        if (speedFill.getHeight() > 0.0f) {
            const int clipCount = canvas.save();
            canvas.clipRect(track);

            Paint speedGlow;
            speedGlow.setStyle(Paint::Style::FILL);
            speedGlow.setLinearGradient(speedFill.getX(), speedFill.getY() + speedFill.getHeight(), speedFill.getX(), speedFill.getY(), {
                Paint::ColorStop(0.0f, Color(55, 129, 255, 54)),
                Paint::ColorStop(0.48f, Color(68, 242, 210, 70)),
                Paint::ColorStop(1.0f, Color(255, 197, 84, 92))
            });
            canvas.drawRoundRect(RectF(speedFill.getX() - 5.0f, speedFill.getY() - 6.0f, speedFill.getWidth() + 10.0f, speedFill.getHeight() + 10.0f), 12.0f, speedGlow);

            Paint speedPaint;
            speedPaint.setStyle(Paint::Style::FILL);
            speedPaint.setLinearGradient(speedFill.getX(), speedFill.getY() + speedFill.getHeight(), speedFill.getX(), speedFill.getY(), {
                Paint::ColorStop(0.0f, Color(36, 98, 255, 214)),
                Paint::ColorStop(0.34f, Color(60, 214, 255, 222)),
                Paint::ColorStop(0.7f, Color(112, 237, 163, 228)),
                Paint::ColorStop(1.0f, Color(255, 202, 90, 238))
            });
            canvas.drawRoundRect(speedFill, 10.0f, speedPaint);

            Paint speedCore;
            speedCore.setStyle(Paint::Style::FILL);
            speedCore.setLinearGradient(speedFill.getX(), speedFill.getY(), speedFill.getX() + speedFill.getWidth(), speedFill.getY(), {
                Paint::ColorStop(0.0f, Color(255, 255, 255, 0)),
                Paint::ColorStop(0.5f, Color(255, 255, 255, 110)),
                Paint::ColorStop(1.0f, Color(255, 255, 255, 0))
            });
            canvas.drawRoundRect(RectF(speedFill.getX() + 3.0f, speedFill.getY() + 8.0f, speedFill.getWidth() - 6.0f, std::max(0.0f, speedFill.getHeight() - 16.0f)), 8.0f, speedCore);

            canvas.restoreToCount(clipCount);
        }

        const float markerY = track.getY() + track.getHeight() - 4.0f - fillHeight;
        Paint markerShadow;
        markerShadow.setStyle(Paint::Style::FILL);
        markerShadow.setFillColor(Color(0, 0, 0, 80));
        canvas.drawRoundRect(RectF(panel.getX() + 7.0f, markerY - 10.0f, panel.getWidth() - 14.0f, 24.0f), 10.0f, markerShadow);

        Paint marker;
        marker.setStyle(Paint::Style::FILL);
        marker.setLinearGradient(panel.getX() + 5.0f, markerY - 12.0f, panel.getX() + panel.getWidth() - 9.0f, markerY + 12.0f, {
            Paint::ColorStop(0.0f, Color(255, 198, 94)),
            Paint::ColorStop(1.0f, Color(255, 122, 68))
        });
        canvas.drawRoundRect(RectF(panel.getX() + 5.0f, markerY - 12.0f, panel.getWidth() - 14.0f, 24.0f), 10.0f, marker);

        Paint markerGlass;
        markerGlass.setStyle(Paint::Style::FILL);
        markerGlass.setFillColor(Color(232, 241, 255, 170));
        canvas.drawRoundRect(RectF(panel.getX() + 14.0f, markerY - 8.0f, panel.getWidth() - 32.0f, 8.0f), 4.0f, markerGlass);

        Paint speedReadout;
        speedReadout.setStyle(Paint::Style::FILL);
        speedReadout.setColor(Color(244, 248, 255));
        speedReadout.setTextSize(15.0f);
        speedReadout.setTextAlign(Paint::TextAlign::CENTER);
        canvas.drawText(std::to_string(std::max(0, static_cast<int>(std::lround(currentSpeedKmh_)))), panel.getCenter().getX(), panel.getY() + panel.getHeight() - 34.0f, speedReadout);

        Paint speedUnit = speedReadout;
        speedUnit.setTextSize(9.0f);
        speedUnit.setColor(Color(155, 176, 222));
        canvas.drawText("KMH", panel.getCenter().getX(), panel.getY() + panel.getHeight() - 16.0f, speedUnit);
        speedReadout.setTextAlign(Paint::TextAlign::LEFT);
    }

    void drawRoad(Canvas& canvas) {
        const RectF leftVerge(ROAD_X - VERGE_W, ROAD_Y, VERGE_W, ROAD_H);
        const RectF rightVerge(ROAD_X + ROAD_W, ROAD_Y, VERGE_W, ROAD_H);
        Paint verge;
        verge.setStyle(Paint::Style::FILL);
        verge.setLinearGradient(leftVerge.getX(), leftVerge.getY(), leftVerge.getX() + leftVerge.getWidth(), leftVerge.getY(), {
            Paint::ColorStop(0.0f, Color(124, 215, 37)),
            Paint::ColorStop(1.0f, Color(193, 247, 64))
        });
        canvas.drawRect(leftVerge, verge);
        canvas.drawRect(rightVerge, verge);

        const int saveCount = canvas.save();
        canvas.clipRect(roadWithVergeRect());

        for (int row = -2; row < 9; ++row) {
            const float clusterY = ROAD_Y + row * 154.0f + sceneryScroll_;
            drawShrubCluster(canvas, ROAD_X - 34.0f, clusterY, -1.0f);
            drawShrubCluster(canvas, ROAD_X + ROAD_W + 34.0f, clusterY + 24.0f, 1.0f);
        }

        Paint asphalt;
        asphalt.setStyle(Paint::Style::FILL);
        asphalt.setLinearGradient(ROAD_X, ROAD_Y, ROAD_X + ROAD_W, ROAD_Y, {
            Paint::ColorStop(0.0f, Color(129, 129, 129)),
            Paint::ColorStop(0.5f, Color(160, 160, 160)),
            Paint::ColorStop(1.0f, Color(136, 136, 136))
        });
        canvas.drawRect(RectF(ROAD_X, ROAD_Y, ROAD_W, ROAD_H), asphalt);

        Paint shoulder;
        shoulder.setStyle(Paint::Style::FILL);
        shoulder.setFillColor(Color(78, 78, 78));
        canvas.drawRect(RectF(ROAD_X, ROAD_Y, 18.0f, ROAD_H), shoulder);
        canvas.drawRect(RectF(ROAD_X + ROAD_W - 18.0f, ROAD_Y, 18.0f, ROAD_H), shoulder);

        Paint seam;
        seam.setStyle(Paint::Style::FILL);
        seam.setFillColor(Color(101, 101, 101, 180));
        canvas.drawRect(RectF(ROAD_X + 18.0f, ROAD_Y, 6.0f, ROAD_H), seam);
        canvas.drawRect(RectF(ROAD_X + ROAD_W - 24.0f, ROAD_Y, 6.0f, ROAD_H), seam);

        Paint rumbleWhite;
        rumbleWhite.setStyle(Paint::Style::FILL);
        rumbleWhite.setFillColor(Color(240, 240, 240));
        Paint rumbleDark;
        rumbleDark.setStyle(Paint::Style::FILL);
        rumbleDark.setFillColor(Color(30, 30, 30));

        for (int segment = -2; segment < 38; ++segment) {
            const float y = ROAD_Y + segment * 24.0f + wrapValue(-roadScroll_ * 0.2f, 24.0f);
            const bool white = (segment & 1) == 0;
            const Paint& paint = white ? rumbleWhite : rumbleDark;
            canvas.drawRect(RectF(ROAD_X + 12.0f, y, 6.0f, 16.0f), paint);
            canvas.drawRect(RectF(ROAD_X + ROAD_W - 18.0f, y, 6.0f, 16.0f), paint);
        }

        Paint laneMarker;
        laneMarker.setStyle(Paint::Style::FILL);
        laneMarker.setFillColor(Color(248, 248, 248));
        for (int lane = 1; lane < kLaneCount; ++lane) {
            const float x = roadInnerX() + laneWidth() * lane - 4.0f;
            for (float y = ROAD_Y - 132.0f + roadScroll_; y < ROAD_Y + ROAD_H; y += 132.0f) {
                canvas.drawRoundRect(RectF(x, y, 8.0f, 78.0f), 4.0f, laneMarker);
            }
        }

        canvas.restoreToCount(saveCount);
    }

    void drawShrubCluster(Canvas& canvas, float centerX, float centerY, float direction) {
        Paint shadow;
        shadow.setStyle(Paint::Style::FILL);
        shadow.setFillColor(Color(0, 0, 0, 48));
        canvas.drawCircle(PointF(centerX + 6.0f, centerY + 8.0f), 18.0f, shadow);

        Paint trunk;
        trunk.setStyle(Paint::Style::FILL);
        trunk.setFillColor(Color(52, 80, 18));
        canvas.drawRoundRect(RectF(centerX - 5.0f, centerY + 10.0f, 10.0f, 20.0f), 4.0f, trunk);

        Paint leaf;
        leaf.setStyle(Paint::Style::FILL);
        leaf.setFillColor(Color(22, 84, 20));
        canvas.drawCircle(PointF(centerX - 10.0f * direction, centerY), 16.0f, leaf);
        leaf.setFillColor(Color(31, 104, 24));
        canvas.drawCircle(PointF(centerX + 10.0f * direction, centerY + 4.0f), 14.0f, leaf);
        leaf.setFillColor(Color(45, 132, 28));
        canvas.drawCircle(PointF(centerX, centerY - 12.0f), 14.0f, leaf);

        Paint highlight;
        highlight.setStyle(Paint::Style::FILL);
        highlight.setFillColor(Color(164, 220, 98, 90));
        canvas.drawCircle(PointF(centerX - 6.0f * direction, centerY - 10.0f), 6.0f, highlight);
    }

    void drawTraffic(Canvas& canvas) {
        for (const TrafficCar& car : traffic_) {
            const float centerX = laneCenter(static_cast<float>(car.lane));
            drawCar(canvas, centerX, car.y, TRAFFIC_CAR_W, TRAFFIC_CAR_H, car.bodyColor, false);
        }
    }

    void drawPlayer(Canvas& canvas) {
        drawCar(canvas, laneCenter(playerVisualLane_), playerCenterY() - PLAYER_CAR_H * 0.5f,
                PLAYER_CAR_W, PLAYER_CAR_H, Color(232, 84, 52), true);
    }

    void drawCar(Canvas& canvas, float centerX, float topY, float width, float height, const Color& body, bool isPlayer) {
        Paint shadow;
        shadow.setStyle(Paint::Style::FILL);
        shadow.setFillColor(Color(0, 0, 0, isPlayer ? 90 : 72));
        canvas.drawRoundRect(RectF(centerX - width * 0.5f + 4.0f, topY + 8.0f, width, height), 14.0f, shadow);

        Paint tire;
        tire.setStyle(Paint::Style::FILL);
        tire.setFillColor(Color(22, 22, 26));
        canvas.drawRoundRect(RectF(centerX - width * 0.5f - 5.0f, topY + 14.0f, 8.0f, 18.0f), 3.0f, tire);
        canvas.drawRoundRect(RectF(centerX + width * 0.5f - 3.0f, topY + 14.0f, 8.0f, 18.0f), 3.0f, tire);
        canvas.drawRoundRect(RectF(centerX - width * 0.5f - 5.0f, topY + height - 32.0f, 8.0f, 18.0f), 3.0f, tire);
        canvas.drawRoundRect(RectF(centerX + width * 0.5f - 3.0f, topY + height - 32.0f, 8.0f, 18.0f), 3.0f, tire);

        Paint bodyFill;
        bodyFill.setStyle(Paint::Style::FILL);
        bodyFill.setLinearGradient(centerX, topY, centerX, topY + height, {
            Paint::ColorStop(0.0f, shadeColor(body, 1.15f)),
            Paint::ColorStop(0.58f, body),
            Paint::ColorStop(1.0f, shadeColor(body, 0.72f))
        });
        canvas.drawRoundRect(RectF(centerX - width * 0.5f, topY, width, height), 16.0f, bodyFill);

        Paint outline;
        outline.setStyle(Paint::Style::STROKE);
        outline.setStrokeWidth(2.0f);
        outline.setStrokeColor(Color(17, 18, 25, 150));
        canvas.drawRoundRect(RectF(centerX - width * 0.5f, topY, width, height), 16.0f, outline);

        Paint stripe;
        stripe.setStyle(Paint::Style::FILL);
        stripe.setFillColor(isPlayer ? Color(255, 203, 96, 180) : Color(255, 255, 255, 105));
        canvas.drawRoundRect(RectF(centerX - 5.0f, topY + 8.0f, 10.0f, height - 16.0f), 5.0f, stripe);

        Paint glass;
        glass.setStyle(Paint::Style::FILL);
        glass.setLinearGradient(centerX, topY + 10.0f, centerX, topY + height * 0.52f, {
            Paint::ColorStop(0.0f, Color(248, 251, 255, 190)),
            Paint::ColorStop(1.0f, Color(52, 68, 98, 220))
        });
        canvas.drawRoundRect(RectF(centerX - width * 0.24f, topY + 12.0f, width * 0.48f, height * 0.28f), 8.0f, glass);

        Paint midGlass;
        midGlass.setStyle(Paint::Style::FILL);
        midGlass.setFillColor(Color(32, 42, 62, 205));
        canvas.drawRoundRect(RectF(centerX - width * 0.22f, topY + height * 0.48f, width * 0.44f, height * 0.22f), 8.0f, midGlass);

        Paint lights;
        lights.setStyle(Paint::Style::FILL);
        lights.setFillColor(Color(255, 244, 198, 205));
        canvas.drawRoundRect(RectF(centerX - width * 0.29f, topY + 4.0f, 9.0f, 8.0f), 3.0f, lights);
        canvas.drawRoundRect(RectF(centerX + width * 0.29f - 9.0f, topY + 4.0f, 9.0f, 8.0f), 3.0f, lights);

        Paint taillights;
        taillights.setStyle(Paint::Style::FILL);
        taillights.setFillColor(Color(255, 78, 66, isPlayer ? 210 : 170));
        canvas.drawRoundRect(RectF(centerX - width * 0.29f, topY + height - 12.0f, 9.0f, 7.0f), 3.0f, taillights);
        canvas.drawRoundRect(RectF(centerX + width * 0.29f - 9.0f, topY + height - 12.0f, 9.0f, 7.0f), 3.0f, taillights);
    }

    void drawFuelPickups(Canvas& canvas) {
        for (const FuelPickup& pickup : fuelPickups_) {
            const float centerX = laneCenter(static_cast<float>(pickup.lane));
            const float bobOffset = std::sin(pickup.bobPhase) * 4.0f;
            drawFuelPickup(canvas, centerX, pickup.y + bobOffset);
        }
    }

    void drawFuelPickup(Canvas& canvas, float centerX, float topY) {
        Paint shadow;
        shadow.setStyle(Paint::Style::FILL);
        shadow.setFillColor(Color(0, 0, 0, 72));
        canvas.drawRoundRect(RectF(centerX - FUEL_PICKUP_W * 0.5f + 3.0f, topY + 4.0f, FUEL_PICKUP_W, FUEL_PICKUP_H), 8.0f, shadow);

        Paint can;
        can.setStyle(Paint::Style::FILL);
        can.setLinearGradient(centerX, topY, centerX, topY + FUEL_PICKUP_H, {
            Paint::ColorStop(0.0f, Color(255, 228, 92)),
            Paint::ColorStop(1.0f, Color(224, 76, 52))
        });
        canvas.drawRoundRect(RectF(centerX - FUEL_PICKUP_W * 0.5f, topY, FUEL_PICKUP_W, FUEL_PICKUP_H), 8.0f, can);

        Paint cap;
        cap.setStyle(Paint::Style::FILL);
        cap.setFillColor(Color(255, 247, 210));
        canvas.drawRoundRect(RectF(centerX - 5.0f, topY - 4.0f, 10.0f, 8.0f), 3.0f, cap);

        Paint plus;
        plus.setStyle(Paint::Style::FILL);
        plus.setFillColor(Color(255, 248, 232, 220));
        canvas.drawRoundRect(RectF(centerX - 4.0f, topY + 8.0f, 8.0f, 18.0f), 3.0f, plus);
        canvas.drawRoundRect(RectF(centerX - 9.0f, topY + 13.0f, 18.0f, 8.0f), 3.0f, plus);
    }

    void drawSidebar(Canvas& canvas) {
        const RectF rect(SIDEBAR_X, SIDEBAR_Y, SIDEBAR_W, SIDEBAR_H);

        Paint panelFill;
        panelFill.setStyle(Paint::Style::FILL);
        panelFill.setLinearGradient(rect.getX(), rect.getY(), rect.getX(), rect.getY() + rect.getHeight(), {
            Paint::ColorStop(0.0f, Color(22, 31, 56, 234)),
            Paint::ColorStop(1.0f, Color(11, 16, 28, 234))
        });
        canvas.drawRoundRect(rect, 24.0f, panelFill);

        Paint panelStroke;
        panelStroke.setStyle(Paint::Style::STROKE);
        panelStroke.setStrokeWidth(3.0f);
        panelStroke.setStrokeColor(Color(76, 102, 158, 170));
        canvas.drawRoundRect(rect, 24.0f, panelStroke);

        const int saveCount = canvas.save();
        canvas.clipRect(RectF(rect.getX() + 8.0f, rect.getY() + 8.0f, rect.getWidth() - 16.0f, rect.getHeight() - 16.0f));

        float x = rect.getX() + 18.0f;
        float y = rect.getY() + 18.0f;

        Paint text;
        text.setStyle(Paint::Style::FILL);
        text.setColor(Color(198, 210, 240));

        auto advanceY = [&](const std::string& content, const Paint& paint, float gap) {
            y += canvas.measureTextMetrics(content, paint).height + gap;
        };

        Paint title = text;
        title.setColor(Color(244, 247, 255));
        title.setTextSize(28.0f);
        title.setLetterSpacing(1.0f);
        canvas.drawText("RACER", x, y, title);
        advanceY("RACER", title, 8.0f);

        Paint subtitle = text;
        subtitle.setColor(Color(150, 172, 214));
        subtitle.setTextSize(12.0f);
        canvas.drawText("Thread traffic. Stay fueled.", x, y, subtitle);
        advanceY("Thread traffic. Stay fueled.", subtitle, 18.0f);

        Paint label = text;
        label.setTextSize(13.0f);
        label.setColor(Color(197, 207, 236));

        Paint value = label;
        value.setTextSize(22.0f);
        value.setColor(Color(248, 250, 255));

        auto drawMetric = [&](const std::string& heading, const std::string& content, float gap) {
            canvas.drawText(heading, x, y, label);
            advanceY(heading, label, 4.0f);
            canvas.drawText(content, x, y, value);
            advanceY(content, value, gap);
        };

        drawMetric("SCORE", std::to_string(score_), 10.0f);
        drawMetric("SPEED", std::to_string(std::max(0, static_cast<int>(std::lround(currentSpeedKmh_)))) + " km/h", 10.0f);
        drawMetric("DODGED", std::to_string(dodgedCars_), 10.0f);
        drawMetric("DIST", std::to_string(std::max(0, static_cast<int>(std::lround(distanceKm_ * 1000.0f)))) + " m", 14.0f);

        canvas.drawText("FUEL", x, y, label);
        advanceY("FUEL", label, 6.0f);

        const RectF fuelOuter(x, y, rect.getWidth() - 36.0f, 22.0f);
        Paint fuelBack;
        fuelBack.setStyle(Paint::Style::FILL);
        fuelBack.setFillColor(Color(10, 16, 28));
        canvas.drawRoundRect(fuelOuter, 11.0f, fuelBack);

        Paint fuelBorder;
        fuelBorder.setStyle(Paint::Style::STROKE);
        fuelBorder.setStrokeWidth(2.0f);
        fuelBorder.setStrokeColor(Color(60, 78, 121));
        canvas.drawRoundRect(fuelOuter, 11.0f, fuelBorder);

        const float fuelRatio = clamp01(fuel_ / 100.0f);
        const RectF fuelInner(fuelOuter.getX() + 4.0f,
                              fuelOuter.getY() + 4.0f,
                              (fuelOuter.getWidth() - 8.0f) * fuelRatio,
                              fuelOuter.getHeight() - 8.0f);
        if (fuelInner.getWidth() > 0.0f) {
            Paint fuelFill;
            fuelFill.setStyle(Paint::Style::FILL);
            if (fuelRatio < 0.25f) {
                fuelFill.setLinearGradient(fuelInner.getX(), fuelInner.getY(), fuelInner.getX() + fuelInner.getWidth(), fuelInner.getY(), {
                    Paint::ColorStop(0.0f, Color(255, 108, 74)),
                    Paint::ColorStop(1.0f, Color(255, 192, 78))
                });
            } else {
                fuelFill.setLinearGradient(fuelInner.getX(), fuelInner.getY(), fuelInner.getX() + fuelInner.getWidth(), fuelInner.getY(), {
                    Paint::ColorStop(0.0f, Color(100, 236, 176)),
                    Paint::ColorStop(1.0f, Color(76, 178, 255))
                });
            }
            canvas.drawRoundRect(fuelInner, 8.0f, fuelFill);
        }

        Paint fuelValue = label;
        fuelValue.setColor(fuelRatio < 0.25f ? Color(255, 190, 116) : Color(233, 240, 255));
        fuelValue.setTextSize(18.0f);
        const std::string fuelText = std::to_string(static_cast<int>(std::lround(fuel_))) + "%";
        y += fuelOuter.getHeight() + 8.0f;
        canvas.drawText(fuelText, x, y, fuelValue);
        advanceY(fuelText, fuelValue, 16.0f);

        const int fps = std::max(0, static_cast<int>(std::lround(displayedFps_)));
        const int frameMs = std::max(0, static_cast<int>(std::lround(latestFrameMs_)));
        canvas.drawText("PERF", x, y, label);
        advanceY("PERF", label, 4.0f);
        Paint perf = label;
        perf.setTextSize(15.0f);
        perf.setColor(Color(244, 248, 255));
        const std::string perfText = std::to_string(fps) + " FPS / " + std::to_string(frameMs) + " ms";
        canvas.drawText(perfText, x, y, perf);
        advanceY(perfText, perf, 20.0f);

        Paint controls = label;
        controls.setColor(Color(124, 137, 170));
        controls.setTextSize(10.5f);
        canvas.drawText("Controls", x, y, controls);
        advanceY("Controls", controls, 5.0f);
        canvas.drawText("Left/Right lane", x, y, controls);
        advanceY("Left/Right lane", controls, 2.0f);
        canvas.drawText("Up accelerate", x, y, controls);
        advanceY("Up accelerate", controls, 2.0f);
        canvas.drawText("Down brake", x, y, controls);
        advanceY("Down brake", controls, 2.0f);
        canvas.drawText("P pause  R restart", x, y, controls);

        canvas.restoreToCount(saveCount);
    }

    void drawStatusBanner(Canvas& canvas) {
        if (state_ != PLAYING) {
            return;
        }

        if (fuel_ >= 25.0f) {
            return;
        }

        if (std::fmod(elapsed_ * 3.5f, 1.0f) > 0.52f) {
            return;
        }

        const RectF alert(ROAD_X + 22.0f, ROAD_Y + 18.0f, ROAD_W - 44.0f, 40.0f);
        Paint fill;
        fill.setStyle(Paint::Style::FILL);
        fill.setFillColor(Color(147, 24, 14, 198));
        canvas.drawRoundRect(alert, 14.0f, fill);

        Paint stroke;
        stroke.setStyle(Paint::Style::STROKE);
        stroke.setStrokeWidth(2.0f);
        stroke.setStrokeColor(Color(255, 191, 132, 180));
        canvas.drawRoundRect(alert, 14.0f, stroke);

        Paint text;
        text.setStyle(Paint::Style::FILL);
        text.setColor(Color(255, 242, 224));
        text.setTextSize(18.0f);
        text.setTextAlign(Paint::TextAlign::CENTER);
        text.setTextBaseline(Paint::TextBaseline::MIDDLE);
        canvas.drawText("LOW FUEL", alert.getCenter().getX(), alert.getCenter().getY(), text);
    }

    void drawOverlay(Canvas& canvas, const std::string& title, const std::string& subtitle) {
        const RectF overlay(ROAD_X - 18.0f, ROAD_Y + 220.0f, ROAD_W + VERGE_W * 2.0f + 36.0f, 220.0f);

        Paint fill;
        fill.setStyle(Paint::Style::FILL);
        fill.setFillColor(Color(4, 7, 13, 210));
        canvas.drawRoundRect(overlay, 24.0f, fill);

        Paint border;
        border.setStyle(Paint::Style::STROKE);
        border.setStrokeWidth(2.5f);
        border.setStrokeColor(Color(82, 110, 168, 180));
        canvas.drawRoundRect(overlay, 24.0f, border);

        Paint titlePaint;
        titlePaint.setStyle(Paint::Style::FILL);
        titlePaint.setColor(Color(244, 247, 255));
        titlePaint.setTextSize(32.0f);
        titlePaint.setTextAlign(Paint::TextAlign::CENTER);
        titlePaint.setTextBaseline(Paint::TextBaseline::MIDDLE);
        canvas.drawText(title, overlay.getCenter().getX(), overlay.getCenter().getY() - 26.0f, titlePaint);

        Paint subtitlePaint = titlePaint;
        subtitlePaint.setTextSize(16.0f);
        subtitlePaint.setColor(Color(183, 196, 226));
        canvas.drawText(subtitle, overlay.getCenter().getX(), overlay.getCenter().getY() + 24.0f, subtitlePaint);
    }
};

struct GameContext {
    RacerGame* game = nullptr;
    Canvas* canvas = nullptr;
    int windowW = DESIGN_W;
    int windowH = DESIGN_H;
};

static void simulateFixedTime(RacerGame& game, float totalSeconds) {
    float remaining = std::max(0.0f, totalSeconds);
    while (remaining > 0.0f) {
        const float step = std::min(1.0f / 60.0f, remaining);
        game.update(step);
        remaining -= step;
    }
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;

    auto* ctx = static_cast<GameContext*>(glfwGetWindowUserPointer(window));
    if (!ctx || !ctx->game) {
        return;
    }

    RacerGame& game = *ctx->game;

    if (key == GLFW_KEY_UP) {
        game.setAccelerating(action != GLFW_RELEASE);
        return;
    }
    if (key == GLFW_KEY_DOWN) {
        game.setBraking(action != GLFW_RELEASE);
        return;
    }

    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }

    switch (key) {
        case GLFW_KEY_LEFT:
            game.moveLeft();
            break;
        case GLFW_KEY_RIGHT:
            game.moveRight();
            break;
        case GLFW_KEY_P:
            if (action == GLFW_PRESS) {
                game.togglePause();
            }
            break;
        case GLFW_KEY_R:
            if (action == GLFW_PRESS) {
                game.restart();
            }
            break;
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
    }
}

static void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    auto* ctx = static_cast<GameContext*>(glfwGetWindowUserPointer(window));
    if (ctx && ctx->canvas && width > 0 && height > 0) {
        ctx->canvas->setSize(width, height);
        ctx->windowW = width;
        ctx->windowH = height;
    }
}

} // namespace

int main() {
    std::cout << "Starting Racer..." << std::endl;

    const bool disableMsaa = !getEnvironmentValue("CPPDEMO_DISABLE_MSAA").empty();
    const std::string capturePath = getEnvironmentValue("CPPDEMO_CAPTURE_PPM");
    const bool exitAfterFirstFrame = !getEnvironmentValue("CPPDEMO_EXIT_AFTER_FIRST_FRAME").empty();
    const std::string fixedTimeText = getEnvironmentValue("CPPDEMO_FIXED_TIME_SECONDS");
    float fixedTimeSeconds = 0.0f;
    const bool hasFixedTime = parseFloat(fixedTimeText, fixedTimeSeconds) && fixedTimeSeconds >= 0.0f;

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, disableMsaa ? 0 : 4);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);

    GLFWwindow* window = glfwCreateWindow(DESIGN_W, DESIGN_H, "Racer", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    int fbw = 0;
    int fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    if (fbw <= 0) {
        fbw = DESIGN_W;
    }
    if (fbh <= 0) {
        fbh = DESIGN_H;
    }

    glViewport(0, 0, fbw, fbh);
    if (!disableMsaa) {
        glEnable(GL_MULTISAMPLE);
    }

    Canvas canvas;
    canvas.setSize(fbw, fbh);

    RacerGame game;
    GameContext ctx;
    ctx.game = &game;
    ctx.canvas = &canvas;
    ctx.windowW = fbw;
    ctx.windowH = fbh;

    glfwSetWindowUserPointer(window, &ctx);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);

    double lastTime = glfwGetTime();
    bool fixedTimeApplied = false;
    bool captureAttempted = false;
    bool captureFailed = false;

    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        float dt = static_cast<float>(now - lastTime);
        lastTime = now;

        if (hasFixedTime) {
            if (!fixedTimeApplied) {
                simulateFixedTime(game, fixedTimeSeconds);
                fixedTimeApplied = true;
            }
            dt = 0.0f;
        }

        glClear(GL_COLOR_BUFFER_BIT);

        canvas.beginFrame();
        game.update(dt);
        game.render(canvas, ctx.windowW, ctx.windowH);
        canvas.endFrame();

        if (!captureAttempted && !capturePath.empty()) {
            captureAttempted = true;
            if (!canvas.savePixelsPPM(capturePath)) {
                std::cerr << "Failed to save capture to " << capturePath << std::endl;
                captureFailed = true;
            }
            if (exitAfterFirstFrame || captureFailed) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        } else if (exitAfterFirstFrame) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    canvas.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "Final score: " << game.getScore() << std::endl;
    return captureFailed ? 1 : 0;
}