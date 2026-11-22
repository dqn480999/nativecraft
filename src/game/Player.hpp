#pragma once

#include "core/Math.hpp"
#include "world/Blocks.hpp"
#include "world/World.hpp"

namespace nc {

struct PlayerInput {
    bool forward = false;
    bool back = false;
    bool left = false;
    bool right = false;
    bool jump = false;
    bool descend = false;
    bool boost = false;
};

struct RayHit {
    bool hit = false;
    int x = 0;
    int y = 0;
    int z = 0;
    int previousX = 0;
    int previousY = 0;
    int previousZ = 0;
    float distance = 0.0f;
};

class Player {
public:
    Vec3 position{0.0f, 80.0f, 0.0f};
    Vec3 velocity{};
    float yaw = 0.0f;
    float pitch = -12.0f;
    bool flying = false;
    bool onGround = false;
    bool inWater = false;

    static constexpr float kWidth = 0.6f;
    static constexpr float kHeight = 1.8f;
    static constexpr float kEyeHeight = 1.62f;

    void spawn(const World& world, const BlockRegistry& registry);
    void update(const World& world, const BlockRegistry& registry, const PlayerInput& input,
                float dt);

    Vec3 eye() const { return position + Vec3{0.0f, kEyeHeight, 0.0f}; }
    Vec3 forward() const;
    Vec3 right() const;
    Mat4 viewMatrix() const;
    Aabb boxAt(const Vec3& feet) const;

    RayHit raycast(const World& world, const BlockRegistry& registry, float maxDistance) const;

private:
    bool collides(const World& world, const BlockRegistry& registry, const Vec3& feet) const;
    void moveWalking(const World& world, const BlockRegistry& registry, const Vec3& wish,
                     const PlayerInput& input, float dt);
};

}
