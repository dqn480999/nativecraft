#pragma once

#include "core/Math.hpp"

namespace nc {

struct Environment {
    Vec3 sunDirection{0.0f, 1.0f, 0.0f};
    Vec3 sunColor{1.0f, 1.0f, 1.0f};
    Vec3 skyZenith{0.22f, 0.45f, 0.85f};
    Vec3 skyHorizon{0.62f, 0.78f, 0.98f};
    Vec3 fogColor{0.62f, 0.78f, 0.98f};
    Vec3 ambientSky{0.45f, 0.55f, 0.7f};
    Vec3 ambientGround{0.22f, 0.2f, 0.18f};
    float dayFactor = 1.0f;
};

Environment computeEnvironment(float timeOfDay);

}
