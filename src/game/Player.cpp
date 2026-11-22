#include "game/Player.hpp"

#include <cmath>
#include <limits>

namespace nc {
namespace {

float approach(float current, float target, float factor) {
    return current + (target - current) * factor;
}

}

Vec3 Player::forward() const {
    const float cp = std::cos(radians(pitch));
    const float sp = std::sin(radians(pitch));
    const float cy = std::cos(radians(yaw));
    const float sy = std::sin(radians(yaw));
    return normalize({cp * sy, sp, -cp * cy});
}

Vec3 Player::right() const {
    return {std::cos(radians(yaw)), 0.0f, std::sin(radians(yaw))};
}

Mat4 Player::viewMatrix() const {
    const Vec3 eyePosition = eye();
    return Mat4::lookAt(eyePosition, eyePosition + forward(), {0.0f, 1.0f, 0.0f});
}

Aabb Player::boxAt(const Vec3& feet) const {
    const float half = kWidth * 0.5f;
    return Aabb{
        {feet.x - half, feet.y, feet.z - half},
        {feet.x + half, feet.y + kHeight, feet.z + half},
    };
}

bool Player::collides(const World& world, const BlockRegistry& registry, const Vec3& feet) const {
    const Aabb box = boxAt(feet);
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - 1e-4f));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - 1e-4f));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - 1e-4f));

    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                if (registry.isSolid(world.get(x, y, z))) {
                    return true;
                }
            }
        }
    }
    return false;
}

void Player::spawn(const World& world, const BlockRegistry& registry) {
    const int centerX = 0;
    const int centerZ = 0;
    const int leaves = registry.id("oak_leaves");
    const int log = registry.id("oak_log");
    const int water = registry.id("water");
    const int grass = registry.id("grass");

    auto findGround = [&](int x, int z, int& ground, int& top) {
        for (int y = World::kHeight - 1; y >= 1; --y) {
            const int block = world.get(x, y, z);
            if (block == 0 || block == leaves || block == log || block == water) {
                continue;
            }
            if (registry.isSolid(block)) {
                ground = y;
                top = block;
                return true;
            }
        }
        return false;
    };

    auto trySpawn = [&](bool requireGrass, int minimumGround) {
        for (int radius = 0; radius < 110; ++radius) {
            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dz)) != radius) {
                        continue;
                    }
                    const int x = centerX + dx;
                    const int z = centerZ + dz;
                    int ground = -1;
                    int top = 0;
                    if (!findGround(x, z, ground, top)) {
                        continue;
                    }
                    if (ground < minimumGround || ground + 3 >= World::kHeight) {
                        continue;
                    }
                    if (requireGrass && top != grass) {
                        continue;
                    }
                    const Vec3 candidate{static_cast<float>(x) + 0.5f,
                                         static_cast<float>(ground + 1),
                                         static_cast<float>(z) + 0.5f};
                    if (!collides(world, registry, candidate)) {
                        position = candidate;
                        velocity = {};
                        onGround = true;
                        return true;
                    }
                }
            }
        }
        return false;
    };

    if (trySpawn(true, 63) || trySpawn(true, 58) || trySpawn(false, 58)) {
        return;
    }

    position = {static_cast<float>(centerX) + 0.5f,
                static_cast<float>(world.surfaceY(centerX, centerZ) + 1),
                static_cast<float>(centerZ) + 0.5f};
    velocity = {};
}

void Player::update(const World& world, const BlockRegistry& registry, const PlayerInput& input,
                    float dt) {
    dt = clampf(dt, 0.0f, 0.05f);

    const Vec3 flatForward = normalize({std::sin(radians(yaw)), 0.0f, -std::cos(radians(yaw))});
    const Vec3 flatRight = right();
    Vec3 wish{};
    if (input.forward) {
        wish += flatForward;
    }
    if (input.back) {
        wish -= flatForward;
    }
    if (input.right) {
        wish += flatRight;
    }
    if (input.left) {
        wish -= flatRight;
    }
    if (lengthSquared(wish) > 1e-6f) {
        wish = normalize(wish);
    }

    const int feetX = static_cast<int>(std::floor(position.x));
    const int feetZ = static_cast<int>(std::floor(position.z));
    const int feetY = static_cast<int>(std::floor(position.y + 0.1f));
    const int chestY = static_cast<int>(std::floor(position.y + kHeight * 0.5f));
    inWater = registry.isLiquid(world.get(feetX, feetY, feetZ))
           || registry.isLiquid(world.get(feetX, chestY, feetZ));

    if (flying) {
        inWater = false;
        const float speed = input.boost ? 26.0f : 12.0f;
        velocity.x = wish.x * speed;
        velocity.z = wish.z * speed;
        velocity.y = (input.jump ? 1.0f : 0.0f) * speed
                   - (input.descend ? 1.0f : 0.0f) * speed;
        position += velocity * dt;
        position.y = clampf(position.y, 1.0f, static_cast<float>(World::kHeight - 3));
        onGround = false;
        return;
    }

    const float targetSpeed = inWater ? (input.boost ? 4.2f : 3.0f)
                                      : (input.boost ? 6.8f : 4.3f);
    const float targetX = wish.x * targetSpeed;
    const float targetZ = wish.z * targetSpeed;
    const float acceleration = 1.0f - std::exp((inWater ? -8.0f : -14.0f) * dt);
    velocity.x = approach(velocity.x, targetX, acceleration);
    velocity.z = approach(velocity.z, targetZ, acceleration);

    if (inWater) {
        if (input.jump) {
            velocity.y += 22.0f * dt;
        }
        if (input.descend) {
            velocity.y -= 18.0f * dt;
        }
        velocity.y -= 8.0f * dt;
        velocity.y *= std::exp(-2.2f * dt);
        velocity.y = clampf(velocity.y, -4.0f, 5.0f);
    } else {
        if (input.jump && onGround) {
            velocity.y = 8.6f;
            onGround = false;
        }
        velocity.y -= 28.0f * dt;
        velocity.y = std::max(velocity.y, -60.0f);
    }

    onGround = false;

    const float deltaY = velocity.y * dt;
    position.y += deltaY;
    if (collides(world, registry, position)) {
        position.y -= deltaY;
        if (velocity.y < 0.0f) {
            onGround = true;
        }
        velocity.y = 0.0f;
    }

    const float deltaX = velocity.x * dt;
    position.x += deltaX;
    if (collides(world, registry, position)) {
        position.x -= deltaX;
        velocity.x = 0.0f;
    }

    const float deltaZ = velocity.z * dt;
    position.z += deltaZ;
    if (collides(world, registry, position)) {
        position.z -= deltaZ;
        velocity.z = 0.0f;
    }

}

RayHit Player::raycast(const World& world, const BlockRegistry& registry,
                       float maxDistance) const {
    const Vec3 origin = eye();
    const Vec3 direction = forward();

    int x = static_cast<int>(std::floor(origin.x));
    int y = static_cast<int>(std::floor(origin.y));
    int z = static_cast<int>(std::floor(origin.z));

    const int stepX = direction.x > 0.0f ? 1 : -1;
    const int stepY = direction.y > 0.0f ? 1 : -1;
    const int stepZ = direction.z > 0.0f ? 1 : -1;

    constexpr float infinity = std::numeric_limits<float>::infinity();
    const float deltaX = direction.x == 0.0f ? infinity : std::abs(1.0f / direction.x);
    const float deltaY = direction.y == 0.0f ? infinity : std::abs(1.0f / direction.y);
    const float deltaZ = direction.z == 0.0f ? infinity : std::abs(1.0f / direction.z);

    float maxX = direction.x == 0.0f ? infinity
                                     : ((direction.x > 0.0f ? (x + 1 - origin.x) : (x - origin.x))
                                        / direction.x);
    float maxY = direction.y == 0.0f ? infinity
                                     : ((direction.y > 0.0f ? (y + 1 - origin.y) : (y - origin.y))
                                        / direction.y);
    float maxZ = direction.z == 0.0f ? infinity
                                     : ((direction.z > 0.0f ? (z + 1 - origin.z) : (z - origin.z))
                                        / direction.z);

    int previousX = x;
    int previousY = y;
    int previousZ = z;
    float distance = 0.0f;

    for (int i = 0; i < 256; ++i) {
        const int block = world.get(x, y, z);
        const bool vegetation = registry.def(block).cross;
        if ((registry.isSolid(block) || vegetation) && !registry.isLiquid(block)) {
            RayHit hit;
            hit.hit = true;
            hit.x = x;
            hit.y = y;
            hit.z = z;
            hit.previousX = previousX;
            hit.previousY = previousY;
            hit.previousZ = previousZ;
            hit.distance = distance;
            return hit;
        }

        previousX = x;
        previousY = y;
        previousZ = z;

        if (maxX < maxY && maxX < maxZ) {
            if (maxX > maxDistance) {
                break;
            }
            distance = maxX;
            x += stepX;
            maxX += deltaX;
        } else if (maxY < maxZ) {
            if (maxY > maxDistance) {
                break;
            }
            distance = maxY;
            y += stepY;
            maxY += deltaY;
        } else {
            if (maxZ > maxDistance) {
                break;
            }
            distance = maxZ;
            z += stepZ;
            maxZ += deltaZ;
        }
    }

    return {};
}

}
