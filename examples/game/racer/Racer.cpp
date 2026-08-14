#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

#include "wsc/wsc.h"

using namespace wsc;

namespace {

constexpr int kLaneCount = 3;

constexpr int DESIGN_W = 686;
constexpr int DESIGN_H = 960;
constexpr unsigned int kOpenGLMultisample = 0x809D;

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
// Start smoothing traffic before a three-lane group becomes an actual wall.
// The playable blocking span remains much smaller than this look-ahead band.
constexpr float kTrafficFlowLookAhead = 360.0f;
constexpr float kTrafficFlowMinSpeedPx = 8.0f;
constexpr float kTrafficSpeedResponse = 42.0f;

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
    float cruiseSpeedPx = 0.0f;
    Color bodyColor;
    bool counted = false;
    bool crashed = false;
    float crashOffsetX = 0.0f;
    float crashVelocityX = 0.0f;
    float crashVelocityY = 0.0f;
    float crashRotation = 0.0f;
    float crashAngularVelocity = 0.0f;
};

struct FuelPickup {
    int lane = 1;
    float y = 0.0f;
    float bobPhase = 0.0f;
};

struct ImpactParticle {
    float x = 0.0f;
    float y = 0.0f;
    float velocityX = 0.0f;
    float velocityY = 0.0f;
    float life = 0.0f;
    float maxLife = 0.0f;
    float size = 0.0f;
    bool smoke = false;
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

float smoothStep(float value) {
    value = clamp01(value);
    return value * value * (3.0f - 2.0f * value);
}

Color mixColor(const Color& from, const Color& to, float amount) {
    amount = clamp01(amount);
    const auto mix = [amount](int a, int b) {
        return static_cast<int>(std::lround(a + (b - a) * amount));
    };
    return Color(mix(from.getR(), to.getR()), mix(from.getG(), to.getG()),
                 mix(from.getB(), to.getB()), mix(from.getA(), to.getA()));
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

void applyGameFont(Paint& paint) {
    paint.setFont(FontSystem::kDefaultMonoFamily);
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
        boostIntensity_ = 0.0f;
        currentSpeedKmh_ = kStartSpeedKmh;
        targetSpeedKmh_ = kStartSpeedKmh;
        worldTravelPx_ = 0.0f;
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
        crashElapsed_ = -1.0f;
        crashAngle_ = 0.0f;
        playerCrashOffsetX_ = 0.0f;
        playerCrashOffsetY_ = 0.0f;
        playerCrashVelocityX_ = 0.0f;
        playerCrashVelocityY_ = 0.0f;
        playerCrashAngularVelocity_ = 0.0f;
        gameOverReason_ = "Avoid traffic. Keep moving.";
        traffic_.clear();
        fuelPickups_.clear();
        impactParticles_.clear();
        spawnTraffic(-260.0f);
        spawnTraffic(-520.0f);
    }

    void update(float dt) {
        updatePerformanceStats(dt);
        if (!std::isfinite(dt) || dt <= 0.0f) {
            return;
        }

        const float clampedDt = std::min(dt, 0.1f);
        updateImpactEffects(clampedDt);
        updateCrashVehicles(clampedDt);
        const float boostTarget = state_ == PLAYING && accelerating_ ? 1.0f : 0.0f;
        const float boostResponse = boostTarget > boostIntensity_ ? 4.8f : 2.6f;
        boostIntensity_ += (boostTarget - boostIntensity_) *
                           std::min(1.0f, clampedDt * boostResponse);
        playerVisualLane_ += (static_cast<float>(playerLane_) - playerVisualLane_) * std::min(1.0f, clampedDt * 12.0f);

        if (state_ != PLAYING) {
            return;
        }

        elapsed_ += clampedDt;

        const float difficulty = clamp01(distanceKm_ / 2.4f);
        const float cruiseSpeed = 138.0f + difficulty * 50.0f + std::min(26.0f, static_cast<float>(dodgedCars_) * 0.65f);
        float requestedSpeed = cruiseSpeed;
        if (accelerating_) {
            requestedSpeed += 118.0f;
        }
        if (braking_) {
            requestedSpeed -= 66.0f;
        }
        targetSpeedKmh_ = std::clamp(requestedSpeed, kMinSpeedKmh, kMaxSpeedKmh);
        const float speedResponse = targetSpeedKmh_ > currentSpeedKmh_ ? 5.8f : 4.1f;
        currentSpeedKmh_ += (targetSpeedKmh_ - currentSpeedKmh_) * std::min(1.0f, clampedDt * speedResponse);

        const float roadPxPerSecond = roadPixelsPerSecond();
        worldTravelPx_ += roadPxPerSecond * clampedDt;

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
        const int worldSave = canvas.save();
        if (crashElapsed_ >= 0.0f && crashElapsed_ < 0.24f) {
            const float strength = (1.0f - crashElapsed_ / 0.24f) * 3.2f;
            canvas.translate(std::sin(crashElapsed_ * 92.0f) * strength,
                             std::cos(crashElapsed_ * 77.0f) * strength * 0.55f);
        }
        drawRoad(canvas);
        drawAccelerationEffects(canvas);

        const int trafficClipCount = canvas.save();
        canvas.clipRect(roadContentRect());
        drawFuelPickups(canvas);
        drawTraffic(canvas);
        drawPlayer(canvas);
        drawImpactEffects(canvas);
        canvas.restoreToCount(trafficClipCount);
        canvas.restoreToCount(worldSave);

        drawWeather(canvas);
        drawSidebar(canvas);
        drawStatusBanner(canvas);

        if (state_ == GAME_OVER && (crashElapsed_ < 0.0f || crashElapsed_ >= 0.48f)) {
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
    float boostIntensity_ = 0.0f;
    float currentSpeedKmh_ = kStartSpeedKmh;
    float targetSpeedKmh_ = kStartSpeedKmh;
    float worldTravelPx_ = 0.0f;
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
    float crashElapsed_ = -1.0f;
    float crashAngle_ = 0.0f;
    float playerCrashOffsetX_ = 0.0f;
    float playerCrashOffsetY_ = 0.0f;
    float playerCrashVelocityX_ = 0.0f;
    float playerCrashVelocityY_ = 0.0f;
    float playerCrashAngularVelocity_ = 0.0f;
    std::string gameOverReason_;
    std::vector<TrafficCar> traffic_;
    std::vector<FuelPickup> fuelPickups_;
    std::vector<ImpactParticle> impactParticles_;

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
        const float baseSpeed = 165.0f + 110.0f * normalized + 365.0f * normalized * normalized;
        return baseSpeed * (1.0f + boostIntensity_ * 0.24f);
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
        car.cruiseSpeedPx = randomRange(20.0f, 96.0f);
        car.speedPx = car.cruiseSpeedPx;
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

        updateTrafficFlow(dt);

        for (TrafficCar& car : traffic_) {
            car.y += dt * (roadSpeed * 0.82f + car.speedPx);
            if (!car.counted && car.y > playerCenterY() + 80.0f) {
                car.counted = true;
                ++dodgedCars_;
                score_ += 35 + static_cast<int>(currentSpeedKmh_ * 0.08f);
            }
        }

        if (state_ == PLAYING) {
            const Bounds playerBox = playerBounds();
            for (TrafficCar& car : traffic_) {
                if (intersects(playerBox, trafficBounds(car))) {
                    state_ = GAME_OVER;
                    currentSpeedKmh_ *= 0.35f;
                    triggerImpact(car);
                    gameOverReason_ = "Traffic hit. Press R to restart";
                    break;
                }
            }
        }

        traffic_.erase(std::remove_if(traffic_.begin(), traffic_.end(), [](const TrafficCar& car) {
            return car.y > ROAD_Y + ROAD_H + 140.0f;
        }), traffic_.end());
    }

    void triggerImpact(TrafficCar& car) {
        crashElapsed_ = 0.0f;
        const float impactX = laneCenter(playerVisualLane_);
        const float impactY = playerCenterY() - 22.0f;
        const float side = car.lane <= playerLane_ ? 1.0f : -1.0f;
        crashAngle_ = side * randomRange(0.10f, 0.18f);
        playerCrashVelocityX_ = -side * randomRange(42.0f, 68.0f);
        playerCrashVelocityY_ = randomRange(82.0f, 118.0f);
        playerCrashAngularVelocity_ = side * randomRange(0.42f, 0.68f);
        car.crashed = true;
        car.crashVelocityX = side * randomRange(34.0f, 62.0f);
        car.crashVelocityY = -randomRange(115.0f, 175.0f);
        car.crashAngularVelocity = -side * randomRange(0.48f, 0.78f);
        impactParticles_.clear();

        for (int index = 0; index < 30; ++index) {
            const float angle = randomRange(-2.75f, -0.38f);
            const float speed = randomRange(75.0f, 245.0f);
            ImpactParticle particle;
            particle.x = impactX + randomRange(-15.0f, 15.0f);
            particle.y = impactY + randomRange(-10.0f, 15.0f);
            particle.velocityX = std::cos(angle) * speed + side * randomRange(10.0f, 60.0f);
            particle.velocityY = std::sin(angle) * speed;
            particle.maxLife = particle.life = randomRange(0.28f, 0.72f);
            particle.size = randomRange(2.0f, 5.5f);
            impactParticles_.push_back(particle);
        }
        for (int index = 0; index < 9; ++index) {
            ImpactParticle smoke;
            smoke.x = impactX + randomRange(-12.0f, 12.0f);
            smoke.y = impactY + randomRange(-8.0f, 8.0f);
            smoke.velocityX = randomRange(-22.0f, 22.0f);
            smoke.velocityY = randomRange(-62.0f, -24.0f);
            smoke.maxLife = smoke.life = randomRange(0.75f, 1.35f);
            smoke.size = randomRange(9.0f, 17.0f);
            smoke.smoke = true;
            impactParticles_.push_back(smoke);
        }
    }

    void updateImpactEffects(float dt) {
        if (crashElapsed_ < 0.0f) {
            return;
        }
        crashElapsed_ += dt;
        for (ImpactParticle& particle : impactParticles_) {
            particle.life -= dt;
            particle.x += particle.velocityX * dt;
            particle.y += particle.velocityY * dt;
            if (particle.smoke) {
                particle.velocityX *= std::max(0.0f, 1.0f - dt * 1.8f);
                particle.velocityY *= std::max(0.0f, 1.0f - dt * 0.8f);
                particle.size += dt * 13.0f;
            } else {
                particle.velocityY += 310.0f * dt;
                particle.velocityX *= std::max(0.0f, 1.0f - dt * 2.2f);
            }
        }
        impactParticles_.erase(std::remove_if(impactParticles_.begin(), impactParticles_.end(),
            [](const ImpactParticle& particle) { return particle.life <= 0.0f; }),
            impactParticles_.end());
    }

    void updateCrashVehicles(float dt) {
        if (crashElapsed_ < 0.0f) {
            return;
        }

        playerCrashOffsetX_ += playerCrashVelocityX_ * dt;
        playerCrashOffsetY_ += playerCrashVelocityY_ * dt;
        crashAngle_ += playerCrashAngularVelocity_ * dt;
        const float playerDrag = std::max(0.0f, 1.0f - dt * 3.4f);
        playerCrashVelocityX_ *= playerDrag;
        playerCrashVelocityY_ *= playerDrag;
        playerCrashAngularVelocity_ *= std::max(0.0f, 1.0f - dt * 4.2f);

        for (TrafficCar& car : traffic_) {
            if (!car.crashed) {
                continue;
            }
            car.y += car.crashVelocityY * dt;
            car.crashOffsetX += car.crashVelocityX * dt;
            car.crashRotation += car.crashAngularVelocity * dt;
            // Lateral motion is stored separately from lanes by converting it
            // into a small visual lane offset during rendering.
            car.crashVelocityX *= std::max(0.0f, 1.0f - dt * 2.7f);
            car.crashVelocityY *= std::max(0.0f, 1.0f - dt * 2.9f);
            car.crashAngularVelocity *= std::max(0.0f, 1.0f - dt * 3.7f);
        }
    }

    void updateTrafficFlow(float dt) {
        std::vector<float> desiredSpeeds;
        desiredSpeeds.reserve(traffic_.size());
        for (const TrafficCar& car : traffic_) {
            desiredSpeeds.push_back(car.cruiseSpeedPx);
        }

        // A safe spawn can compress later because every car has its own cruise
        // speed. Detect that trend early and let the rearmost member ease off,
        // like ordinary traffic, instead of changing any car's position.
        for (int anchor = 0; anchor < static_cast<int>(traffic_.size()); ++anchor) {
            std::array<int, kLaneCount> laneIndices{};
            laneIndices.fill(-1);
            const float bandTop = traffic_[anchor].y;
            const float bandBottom = bandTop + kTrafficFlowLookAhead;

            for (int index = 0; index < static_cast<int>(traffic_.size()); ++index) {
                const TrafficCar& car = traffic_[index];
                if (car.y < bandTop || car.y > bandBottom) {
                    continue;
                }
                if (laneIndices[car.lane] < 0 ||
                    car.y < traffic_[laneIndices[car.lane]].y) {
                    laneIndices[car.lane] = index;
                }
            }

            bool allLanesPresent = true;
            for (int lane = 0; lane < kLaneCount; ++lane) {
                allLanesPresent = allLanesPresent && laneIndices[lane] >= 0;
            }
            if (!allLanesPresent) {
                continue;
            }

            int rearmostIndex = laneIndices[0];
            for (int lane = 1; lane < kLaneCount; ++lane) {
                if (traffic_[laneIndices[lane]].y < traffic_[rearmostIndex].y) {
                    rearmostIndex = laneIndices[lane];
                }
            }
            desiredSpeeds[rearmostIndex] = std::min(desiredSpeeds[rearmostIndex],
                                                     kTrafficFlowMinSpeedPx);
        }

        const float maxSpeedChange = kTrafficSpeedResponse * dt;
        for (int index = 0; index < static_cast<int>(traffic_.size()); ++index) {
            TrafficCar& car = traffic_[index];
            const float delta = std::clamp(desiredSpeeds[index] - car.speedPx,
                                           -maxSpeedChange, maxSpeedChange);
            car.speedPx += delta;
        }
    }

    float stableRandom01(int index, int salt) const {
        uint32_t value = static_cast<uint32_t>(index) * 0x9e3779b9u ^
                         static_cast<uint32_t>(salt) * 0x85ebca6bu;
        value ^= value >> 16;
        value *= 0x7feb352du;
        value ^= value >> 15;
        value *= 0x846ca68bu;
        value ^= value >> 16;
        return static_cast<float>(value & 0x00ffffffu) / 16777215.0f;
    }

    float stableNormal(int index, int salt) const {
        // Sum of six uniform samples: a cheap, deterministic bell curve with
        // mean 0 and approximately unit variance.
        float sum = 0.0f;
        for (int sample = 0; sample < 6; ++sample) {
            sum += stableRandom01(index, salt + sample * 17);
        }
        return (sum - 3.0f) * std::sqrt(2.0f);
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

    struct EnvironmentStyle {
        Color ground;
        Color groundEdge;
        Color foliageDark;
        Color foliageMid;
        Color foliageLight;
        Color trunk;
        Color asphalt;
        int biome = 0;
        int season = 0;
    };

    enum WeatherType {
        WEATHER_CLEAR,
        WEATHER_RAIN,
        WEATHER_FOG,
        WEATHER_STORM,
        WEATHER_SNOW,
    };

    struct WeatherState {
        WeatherType type = WEATHER_CLEAR;
        float intensity = 0.0f;
    };

    EnvironmentStyle environmentStyleFor(int biome, int season) const {
        static const std::array<Color, 3> ground = {
            Color(108, 188, 46), Color(126, 132, 142), Color(218, 177, 91)
        };
        static const std::array<Color, 3> edge = {
            Color(184, 232, 74), Color(162, 169, 180), Color(239, 205, 128)
        };
        static const std::array<Color, 4> seasonTint = {
            Color(142, 224, 92), Color(74, 177, 54), Color(210, 124, 42), Color(220, 232, 239)
        };
        static const std::array<Color, 4> leaves = {
            Color(76, 170, 55), Color(31, 116, 32), Color(184, 82, 26), Color(173, 188, 193)
        };

        EnvironmentStyle style;
        style.biome = biome;
        style.season = season;
        const float tintAmount = biome == 2 ? 0.12f : (season == 3 ? 0.62f : 0.28f);
        style.ground = mixColor(ground[biome], seasonTint[season], tintAmount);
        style.groundEdge = mixColor(edge[biome], seasonTint[season], tintAmount * 0.75f);
        style.foliageMid = biome == 2 ? Color(71, 128, 64) : leaves[season];
        style.foliageDark = shadeColor(style.foliageMid, 0.68f);
        style.foliageLight = mixColor(style.foliageMid, Color(235, 242, 184), 0.42f);
        style.trunk = season == 3 ? Color(90, 82, 70) : Color(74, 71, 38);
        style.asphalt = biome == 1 ? Color(113, 118, 127) :
                        (biome == 2 ? Color(139, 128, 111) : Color(142, 142, 140));
        return style;
    }

    EnvironmentStyle currentEnvironmentStyle() const {
        constexpr float kStageKm = 0.8f;
        const float stagePosition = distanceKm_ / kStageKm;
        const int stage = static_cast<int>(std::floor(stagePosition));
        const int nextStage = stage + 1;
        const int biome = (stage / 4) % 3;
        const int season = stage % 4;
        const int nextBiome = (nextStage / 4) % 3;
        const int nextSeason = nextStage % 4;
        // Spend the final quarter of each stage cross-fading into the next one.
        const float blend = smoothStep((stagePosition - std::floor(stagePosition) - 0.75f) * 4.0f);
        const EnvironmentStyle from = environmentStyleFor(biome, season);
        const EnvironmentStyle to = environmentStyleFor(nextBiome, nextSeason);
        EnvironmentStyle result = from;
        result.ground = mixColor(from.ground, to.ground, blend);
        result.groundEdge = mixColor(from.groundEdge, to.groundEdge, blend);
        result.foliageDark = mixColor(from.foliageDark, to.foliageDark, blend);
        result.foliageMid = mixColor(from.foliageMid, to.foliageMid, blend);
        result.foliageLight = mixColor(from.foliageLight, to.foliageLight, blend);
        result.trunk = mixColor(from.trunk, to.trunk, blend);
        result.asphalt = mixColor(from.asphalt, to.asphalt, blend);
        if (blend >= 0.5f) {
            result.biome = to.biome;
            result.season = to.season;
        }
        return result;
    }

    std::string environmentLabel() const {
        static const std::array<const char*, 3> biomes = {"COUNTRY", "CITY", "DESERT"};
        static const std::array<const char*, 4> seasons = {"SPRING", "SUMMER", "AUTUMN", "WINTER"};
        const EnvironmentStyle style = currentEnvironmentStyle();
        return std::string(biomes[style.biome]) + " / " + seasons[style.season];
    }

    int currentSeasonStage() const {
        constexpr float kStageKm = 0.8f;
        const int stage = static_cast<int>(std::floor(distanceKm_ / kStageKm));
        return stage % 4;
    }

    WeatherState currentWeather() const {
        constexpr float kWeatherStageKm = 0.56f;
        const float position = distanceKm_ / kWeatherStageKm;
        const int stage = static_cast<int>(std::floor(position));
        const float progress = position - std::floor(position);
        static const std::array<WeatherType, 8> pattern = {
            WEATHER_CLEAR, WEATHER_RAIN, WEATHER_CLEAR, WEATHER_FOG,
            WEATHER_CLEAR, WEATHER_STORM, WEATHER_CLEAR, WEATHER_RAIN
        };

        WeatherType type = pattern[stage % static_cast<int>(pattern.size())];
        // Use the actual season stage rather than the cross-faded visual style.
        // This keeps the complete winter interval free of rain and lightning.
        if (currentSeasonStage() == 3 &&
            (type == WEATHER_RAIN || type == WEATHER_STORM)) {
            type = WEATHER_SNOW;
        }

        float intensity = 1.0f;
        if (progress < 0.14f) {
            intensity = smoothStep(progress / 0.14f);
        } else if (progress > 0.84f) {
            intensity = smoothStep((1.0f - progress) / 0.16f);
        }
        if (type == WEATHER_CLEAR) {
            intensity = 0.0f;
        }
        return WeatherState{type, intensity};
    }

    std::string weatherLabel() const {
        switch (currentWeather().type) {
            case WEATHER_RAIN: return "RAIN";
            case WEATHER_FOG: return "FOG";
            case WEATHER_STORM: return "STORM";
            case WEATHER_SNOW: return "SNOW";
            default: return "CLEAR";
        }
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
        applyGameFont(label);
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
        applyGameFont(speedReadout);
        canvas.drawText(std::to_string(std::max(0, static_cast<int>(std::lround(currentSpeedKmh_)))), panel.getCenter().getX(), panel.getY() + panel.getHeight() - 34.0f, speedReadout);

        Paint speedUnit = speedReadout;
        speedUnit.setTextSize(9.0f);
        speedUnit.setColor(Color(155, 176, 222));
        canvas.drawText("KMH", panel.getCenter().getX(), panel.getY() + panel.getHeight() - 16.0f, speedUnit);
        speedReadout.setTextAlign(Paint::TextAlign::LEFT);
    }

    void drawRoad(Canvas& canvas) {
        const EnvironmentStyle environment = currentEnvironmentStyle();
        const RectF leftVerge(ROAD_X - VERGE_W, ROAD_Y, VERGE_W, ROAD_H);
        const RectF rightVerge(ROAD_X + ROAD_W, ROAD_Y, VERGE_W, ROAD_H);
        Paint verge;
        verge.setStyle(Paint::Style::FILL);
        verge.setLinearGradient(leftVerge.getX(), leftVerge.getY(), leftVerge.getX() + leftVerge.getWidth(), leftVerge.getY(), {
            Paint::ColorStop(0.0f, environment.ground),
            Paint::ColorStop(1.0f, environment.groundEdge)
        });
        canvas.drawRect(leftVerge, verge);
        canvas.drawRect(rightVerge, verge);

        const int saveCount = canvas.save();
        canvas.clipRect(roadWithVergeRect());

        constexpr float kRoadsideMeanGap = 168.0f;
        const int firstObject = static_cast<int>(std::floor(
            (-worldTravelPx_ - 150.0f) / kRoadsideMeanGap)) - 1;
        for (int index = firstObject; index < firstObject + 11; ++index) {
            for (int side = 0; side < 2; ++side) {
                const int salt = 100 + side * 1000;
                // Empty cells create natural clearings. Position, setback and
                // scale are independent stable samples, so the landscape is
                // varied but never changes beneath the player.
                if (stableRandom01(index, salt) > 0.76f) {
                    continue;
                }
                const float y = ROAD_Y + static_cast<float>(index) * kRoadsideMeanGap +
                                worldTravelPx_ + stableNormal(index, salt + 1) * 42.0f;
                if (y < ROAD_Y - 100.0f || y > ROAD_Y + ROAD_H + 100.0f) {
                    continue;
                }
                const float direction = side == 0 ? -1.0f : 1.0f;
                const float setback = 25.0f + stableRandom01(index, salt + 2) * 27.0f;
                const float x = side == 0 ? ROAD_X - setback : ROAD_X + ROAD_W + setback;
                const float scale = 0.72f + stableRandom01(index, salt + 3) * 0.52f;
                const int objectSave = canvas.save();
                canvas.translate(x, y);
                canvas.scale(scale, scale);
                drawRoadsideObject(canvas, 0.0f, 0.0f, direction,
                                   index + side * 37, environment);
                canvas.restoreToCount(objectSave);
            }
        }

        Paint asphalt;
        asphalt.setStyle(Paint::Style::FILL);
        asphalt.setLinearGradient(ROAD_X, ROAD_Y, ROAD_X + ROAD_W, ROAD_Y, {
            Paint::ColorStop(0.0f, shadeColor(environment.asphalt, 0.86f)),
            Paint::ColorStop(0.5f, shadeColor(environment.asphalt, 1.08f)),
            Paint::ColorStop(1.0f, shadeColor(environment.asphalt, 0.9f))
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

        Paint laneMarker;
        laneMarker.setStyle(Paint::Style::FILL);
        laneMarker.setFillColor(Color(248, 248, 248));
        const float groundPhase = wrapValue(worldTravelPx_, 132.0f);

        for (int lane = 1; lane < kLaneCount; ++lane) {
            const float x = roadInnerX() + laneWidth() * lane - 4.0f;
            for (float y = ROAD_Y - 132.0f + groundPhase;
                 y < ROAD_Y + ROAD_H; y += 132.0f) {
                canvas.drawRoundRect(RectF(x, y, 8.0f, 66.0f), 4.0f, laneMarker);
            }
        }

        canvas.restoreToCount(saveCount);
    }

    void drawAccelerationEffects(Canvas& canvas) {
        if (boostIntensity_ < 0.02f) {
            return;
        }

        const int saveCount = canvas.save();
        canvas.clipRect(roadWithVergeRect());
        const int alpha = static_cast<int>(110.0f * boostIntensity_);

        Paint streak;
        streak.setStyle(Paint::Style::FILL);
        streak.setLinearGradient(0.0f, ROAD_Y, 0.0f, ROAD_Y + 92.0f, {
            Paint::ColorStop(0.0f, Color(164, 226, 255, 0)),
            Paint::ColorStop(1.0f, Color(210, 244, 255, alpha))
        });
        const float phase = wrapValue(worldTravelPx_ * 1.38f, 118.0f);
        for (int row = -1; row < 9; ++row) {
            const float y = ROAD_Y + row * 118.0f + phase;
            const float length = 36.0f + boostIntensity_ * 58.0f;
            canvas.drawRoundRect(RectF(ROAD_X - 45.0f, y - length, 3.0f, length), 2.0f, streak);
            canvas.drawRoundRect(RectF(ROAD_X - 19.0f, y - length * 0.72f, 2.0f,
                                       length * 0.72f), 1.0f, streak);
            canvas.drawRoundRect(RectF(ROAD_X + ROAD_W + 42.0f, y - length, 3.0f,
                                       length), 2.0f, streak);
            canvas.drawRoundRect(RectF(ROAD_X + ROAD_W + 17.0f, y - length * 0.72f,
                                       2.0f, length * 0.72f), 1.0f, streak);
        }

        canvas.restoreToCount(saveCount);
    }

    void drawWeather(Canvas& canvas) {
        const WeatherState weather = currentWeather();
        if (weather.intensity <= 0.01f) {
            return;
        }

        const int saveCount = canvas.save();
        canvas.clipRect(roadWithVergeRect());

        if (weather.type == WEATHER_FOG) {
            const float areaLeft = ROAD_X - VERGE_W;
            const float areaWidth = ROAD_W + VERGE_W * 2.0f;

            // A light atmospheric veil lowers contrast without washing the
            // road out.  Most of the density comes from broad, independently
            // drifting wisps below, avoiding the old regular scan-line look.
            Paint haze;
            haze.setStyle(Paint::Style::FILL);
            const int alpha = static_cast<int>(48.0f * weather.intensity);
            haze.setLinearGradient(0.0f, ROAD_Y, 0.0f, ROAD_Y + ROAD_H, {
                Paint::ColorStop(0.0f, Color(214, 224, 228, alpha / 2)),
                Paint::ColorStop(0.42f, Color(226, 233, 234, alpha)),
                Paint::ColorStop(1.0f, Color(205, 216, 220, alpha / 2))
            });
            canvas.drawRect(roadWithVergeRect(), haze);

            for (int wisp = 0; wisp < 9; ++wisp) {
                const float width = 170.0f + stableRandom01(wisp, 801) * 190.0f;
                const float height = 72.0f + stableRandom01(wisp, 802) * 105.0f;
                const float speed = 5.0f + stableRandom01(wisp, 803) * 11.0f;
                const float travel = areaWidth + width * 1.4f;
                const float start = stableRandom01(wisp, 804) * travel;
                float x = areaLeft - width * 0.7f + wrapValue(start + elapsed_ * speed,
                                                              travel);
                if ((wisp & 1) != 0) {
                    x = areaLeft + areaWidth - (x - areaLeft) - width;
                }
                const float y = ROAD_Y + stableRandom01(wisp, 805) * ROAD_H -
                                height * 0.5f;

                Paint wispPaint;
                wispPaint.setStyle(Paint::Style::FILL);
                const int wispAlpha = static_cast<int>(
                    (18.0f + stableRandom01(wisp, 806) * 22.0f) * weather.intensity);
                wispPaint.setFillColor(Color(239, 244, 242, wispAlpha));
                canvas.drawOval(RectF(x, y, width, height), wispPaint);
            }
        } else if (weather.type == WEATHER_SNOW) {
            Paint snow;
            snow.setStyle(Paint::Style::FILL);
            snow.setFillColor(Color(250, 253, 255,
                static_cast<int>(220.0f * weather.intensity)));
            for (int particle = 0; particle < 42; ++particle) {
                const float x = ROAD_X - VERGE_W +
                    stableRandom01(particle, 710) * (ROAD_W + VERGE_W * 2.0f);
                const float speed = 42.0f + stableRandom01(particle, 711) * 76.0f;
                const float drift = std::sin(elapsed_ * 1.8f + particle * 0.73f) * 13.0f;
                const float y = ROAD_Y + wrapValue(stableRandom01(particle, 712) * ROAD_H +
                                                   elapsed_ * speed, ROAD_H);
                const float radius = 1.5f + stableRandom01(particle, 713) * 2.8f;
                canvas.drawCircle(PointF(x + drift, y), radius, snow);
            }
        } else {
            const bool storm = weather.type == WEATHER_STORM;
            Paint rain;
            rain.setStyle(Paint::Style::STROKE);
            rain.setStrokeWidth(storm ? 2.0f : 1.4f);
            rain.setStrokeColor(Color(174, 218, 255,
                static_cast<int>((storm ? 205.0f : 158.0f) * weather.intensity)));
            const int dropCount = storm ? 48 : 32;
            for (int drop = 0; drop < dropCount; ++drop) {
                const float x = ROAD_X - VERGE_W +
                    stableRandom01(drop, 620) * (ROAD_W + VERGE_W * 2.0f);
                const float speed = (storm ? 760.0f : 570.0f) +
                                    stableRandom01(drop, 621) * 180.0f;
                const float y = ROAD_Y + wrapValue(stableRandom01(drop, 622) * ROAD_H +
                                                   elapsed_ * speed, ROAD_H);
                const float length = storm ? 31.0f : 22.0f;
                canvas.drawLine(x, y, x - length * 0.28f, y + length, rain);
            }

            if (storm) {
                const float flash = std::sin(elapsed_ * 2.7f) * std::sin(elapsed_ * 7.9f);
                if (flash > 0.86f) {
                    Paint lightning;
                    lightning.setStyle(Paint::Style::FILL);
                    lightning.setFillColor(Color(226, 236, 255,
                        static_cast<int>((flash - 0.86f) * 900.0f * weather.intensity)));
                    canvas.drawRect(roadWithVergeRect(), lightning);
                }
            }
        }

        canvas.restoreToCount(saveCount);
    }

    void drawRoadsideObject(Canvas& canvas, float centerX, float centerY, float direction,
                            int variant, const EnvironmentStyle& environment) {
        if (environment.biome == 1) {
            Paint shadow;
            shadow.setStyle(Paint::Style::FILL);
            shadow.setFillColor(Color(0, 0, 0, 44));
            canvas.drawRect(RectF(centerX - 23.0f, centerY - 35.0f, 48.0f, 76.0f), shadow);

            Paint building;
            building.setStyle(Paint::Style::FILL);
            const Color facade = (variant & 1) == 0 ? Color(74, 88, 108) : Color(112, 94, 88);
            building.setFillColor(mixColor(facade, environment.ground, 0.16f));
            const float height = 58.0f + static_cast<float>(std::abs(variant) % 3) * 12.0f;
            canvas.drawRoundRect(RectF(centerX - 20.0f, centerY - height * 0.5f, 40.0f, height),
                                 3.0f, building);

            Paint window;
            window.setStyle(Paint::Style::FILL);
            window.setFillColor(environment.season == 3 ? Color(255, 210, 104, 190) : Color(119, 205, 232, 180));
            for (int floor = 0; floor < 3; ++floor) {
                canvas.drawRect(RectF(centerX - 12.0f, centerY - height * 0.34f + floor * 18.0f,
                                      8.0f, 8.0f), window);
                canvas.drawRect(RectF(centerX + 4.0f, centerY - height * 0.34f + floor * 18.0f,
                                      8.0f, 8.0f), window);
            }
            return;
        }

        if (environment.biome == 2) {
            Paint shadow;
            shadow.setStyle(Paint::Style::FILL);
            shadow.setFillColor(Color(80, 49, 24, 48));
            canvas.drawCircle(PointF(centerX + 7.0f, centerY + 12.0f), 17.0f, shadow);

            Paint cactus;
            cactus.setStyle(Paint::Style::FILL);
            cactus.setFillColor(environment.foliageMid);
            canvas.drawRoundRect(RectF(centerX - 6.0f, centerY - 25.0f, 12.0f, 54.0f), 6.0f, cactus);
            canvas.drawRoundRect(RectF(centerX - 17.0f, centerY - 8.0f, 14.0f, 9.0f), 5.0f, cactus);
            canvas.drawRoundRect(RectF(centerX - 17.0f, centerY - 18.0f, 8.0f, 18.0f), 4.0f, cactus);
            canvas.drawRoundRect(RectF(centerX + 3.0f, centerY + 2.0f, 16.0f, 9.0f), 5.0f, cactus);
            canvas.drawRoundRect(RectF(centerX + 11.0f, centerY - 8.0f, 8.0f, 18.0f), 4.0f, cactus);
            return;
        }

        Paint shadow;
        shadow.setStyle(Paint::Style::FILL);
        shadow.setFillColor(Color(0, 0, 0, 48));
        canvas.drawCircle(PointF(centerX + 6.0f, centerY + 8.0f), 18.0f, shadow);

        Paint trunk;
        trunk.setStyle(Paint::Style::FILL);
        trunk.setFillColor(environment.trunk);
        canvas.drawRoundRect(RectF(centerX - 5.0f, centerY + 10.0f, 10.0f, 20.0f), 4.0f, trunk);

        Paint leaf;
        leaf.setStyle(Paint::Style::FILL);
        leaf.setFillColor(environment.foliageDark);
        canvas.drawCircle(PointF(centerX - 10.0f * direction, centerY), 16.0f, leaf);
        leaf.setFillColor(environment.foliageMid);
        canvas.drawCircle(PointF(centerX + 10.0f * direction, centerY + 4.0f), 14.0f, leaf);
        leaf.setFillColor(environment.foliageLight);
        canvas.drawCircle(PointF(centerX, centerY - 12.0f), 14.0f, leaf);

        Paint highlight;
        highlight.setStyle(Paint::Style::FILL);
        highlight.setFillColor(mixColor(environment.foliageLight, Color(255, 255, 255, 90), 0.3f));
        canvas.drawCircle(PointF(centerX - 6.0f * direction, centerY - 10.0f), 6.0f, highlight);
    }

    void drawTraffic(Canvas& canvas) {
        for (const TrafficCar& car : traffic_) {
            const float centerX = laneCenter(static_cast<float>(car.lane)) + car.crashOffsetX;
            if (car.crashed) {
                const int saveCount = canvas.save();
                canvas.translate(centerX, car.y + TRAFFIC_CAR_H * 0.5f);
                canvas.rotate(car.crashRotation);
                drawCar(canvas, 0.0f, -TRAFFIC_CAR_H * 0.5f,
                        TRAFFIC_CAR_W, TRAFFIC_CAR_H, car.bodyColor, false);
                canvas.restoreToCount(saveCount);
            } else {
                drawCar(canvas, centerX, car.y, TRAFFIC_CAR_W, TRAFFIC_CAR_H,
                        car.bodyColor, false);
            }
        }
    }

    void drawPlayer(Canvas& canvas) {
        if (boostIntensity_ > 0.02f) {
            const float centerX = laneCenter(playerVisualLane_);
            const float carBottom = playerCenterY() + PLAYER_CAR_H * 0.5f;
            Paint trail;
            trail.setStyle(Paint::Style::FILL);
            trail.setLinearGradient(centerX, carBottom - 4.0f, centerX, carBottom + 54.0f, {
                Paint::ColorStop(0.0f, Color(104, 232, 255,
                    static_cast<int>(155.0f * boostIntensity_))),
                Paint::ColorStop(0.48f, Color(76, 138, 255,
                    static_cast<int>(92.0f * boostIntensity_))),
                Paint::ColorStop(1.0f, Color(66, 104, 255, 0))
            });
            const float trailLength = 18.0f + boostIntensity_ * 38.0f;
            canvas.drawRoundRect(RectF(centerX - 13.0f, carBottom - 5.0f, 7.0f,
                                       trailLength), 4.0f, trail);
            canvas.drawRoundRect(RectF(centerX + 6.0f, carBottom - 5.0f, 7.0f,
                                       trailLength), 4.0f, trail);
        }
        const float centerX = laneCenter(playerVisualLane_) + playerCrashOffsetX_;
        const float centerY = playerCenterY() + playerCrashOffsetY_;
        if (crashElapsed_ >= 0.0f) {
            const float settle = smoothStep(std::min(1.0f, crashElapsed_ / 0.32f));
            const int saveCount = canvas.save();
            canvas.translate(centerX, centerY);
            canvas.rotate(crashAngle_ * settle);
            drawCar(canvas, 0.0f, -PLAYER_CAR_H * 0.5f,
                    PLAYER_CAR_W, PLAYER_CAR_H, Color(232, 84, 52), true);
            canvas.restoreToCount(saveCount);
        } else {
            drawCar(canvas, centerX, centerY - PLAYER_CAR_H * 0.5f,
                    PLAYER_CAR_W, PLAYER_CAR_H, Color(232, 84, 52), true);
        }
    }

    void drawImpactEffects(Canvas& canvas) {
        if (crashElapsed_ < 0.0f) {
            return;
        }

        const float centerX = laneCenter(playerVisualLane_);
        const float centerY = playerCenterY() - 20.0f;
        if (crashElapsed_ < 0.32f) {
            const float progress = crashElapsed_ / 0.32f;
            Paint ring;
            ring.setStyle(Paint::Style::STROKE);
            ring.setStrokeWidth(5.0f * (1.0f - progress) + 1.0f);
            ring.setStrokeColor(Color(255, 204, 86,
                static_cast<int>(220.0f * (1.0f - progress))));
            canvas.drawCircle(PointF(centerX, centerY), 18.0f + progress * 58.0f, ring);

            Paint flash;
            flash.setStyle(Paint::Style::FILL);
            flash.setFillColor(Color(255, 245, 210,
                static_cast<int>(185.0f * (1.0f - progress))));
            canvas.drawCircle(PointF(centerX, centerY), 24.0f * (1.0f - progress), flash);
        }

        for (const ImpactParticle& particle : impactParticles_) {
            const float lifeRatio = clamp01(particle.life / particle.maxLife);
            Paint paint;
            paint.setStyle(Paint::Style::FILL);
            if (particle.smoke) {
                paint.setFillColor(Color(55, 60, 66,
                    static_cast<int>(92.0f * lifeRatio)));
                canvas.drawCircle(PointF(particle.x, particle.y), particle.size, paint);
            } else {
                paint.setFillColor(lifeRatio > 0.55f
                    ? Color(255, 232, 122, static_cast<int>(245.0f * lifeRatio))
                    : Color(255, 104, 45, static_cast<int>(220.0f * lifeRatio)));
                canvas.drawRoundRect(RectF(particle.x - particle.size * 0.5f,
                                           particle.y - particle.size * 0.5f,
                                           particle.size, particle.size * 1.8f),
                                     particle.size * 0.4f, paint);
            }
        }
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
        applyGameFont(text);

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
        drawMetric("DIST", std::to_string(std::max(0, static_cast<int>(std::lround(distanceKm_ * 1000.0f)))) + " m", 8.0f);

        Paint environmentValue = label;
        environmentValue.setTextSize(11.0f);
        environmentValue.setColor(Color(174, 205, 255));
        canvas.drawText("REGION", x, y, label);
        advanceY("REGION", label, 3.0f);
        const std::string regionText = environmentLabel();
        canvas.drawText(regionText, x, y, environmentValue);
        advanceY(regionText, environmentValue, 5.0f);
        const std::string weatherText = "WEATHER / " + weatherLabel();
        canvas.drawText(weatherText, x, y, environmentValue);
        advanceY(weatherText, environmentValue, 10.0f);

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
        applyGameFont(text);
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
        applyGameFont(titlePaint);
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
        // HiDPI: lay the game out in logical units, render at physical resolution.
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        glfwGetWindowContentScale(window, &scaleX, &scaleY);
        const float dpr = scaleX > 0.0f ? scaleX : 1.0f;
        ctx->canvas->setDevicePixelRatio(dpr);
        ctx->windowW = static_cast<int>(width / dpr);
        ctx->windowH = static_cast<int>(height / dpr);
    }
}

} // namespace

int main() {
    std::cout << "Starting Racer..." << std::endl;

    const bool disableMsaa = !getEnvironmentValue("WHATSCANVAS_DISABLE_MSAA").empty();
    const std::string capturePath = getEnvironmentValue("WHATSCANVAS_CAPTURE_PPM");
    const bool exitAfterFirstFrame = !getEnvironmentValue("WHATSCANVAS_EXIT_AFTER_FIRST_FRAME").empty();
    const std::string fixedTimeText = getEnvironmentValue("WHATSCANVAS_FIXED_TIME_SECONDS");
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

    if (!Canvas::loadOpenGL(reinterpret_cast<Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
        std::cerr << "Failed to load OpenGL functions" << std::endl;
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
        glEnable(kOpenGLMultisample);
    }

    auto canvasOwner = Canvas::create(Canvas::Backend::OpenGL, 0, 0);
    Canvas &canvas = *canvasOwner;
    canvas.setSize(fbw, fbh);
    for (const FontFace &face : FontSystem::defaultSystemFontFaces()) {
        canvas.registerFontFace(face);
    }
    canvas.setFontFallbackChain(FontSystem::defaultFallbackChain());
    float contentScaleX = 1.0f;
    float contentScaleY = 1.0f;
    glfwGetWindowContentScale(window, &contentScaleX, &contentScaleY);
    const float devicePixelRatio = contentScaleX > 0.0f ? contentScaleX : 1.0f;
    canvas.setDevicePixelRatio(devicePixelRatio);

    RacerGame game;
    GameContext ctx;
    ctx.game = &game;
    ctx.canvas = &canvas;
    ctx.windowW = static_cast<int>(fbw / devicePixelRatio);
    ctx.windowH = static_cast<int>(fbh / devicePixelRatio);

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
