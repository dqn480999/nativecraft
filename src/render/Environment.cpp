#include "render/Environment.hpp"

#include <cmath>

namespace nc {
namespace {

float smoothstep01(float edge0, float edge1, float value) {
    const float t = clampf((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vec3 mixColor(const Vec3& a, const Vec3& b, float t) {
    return a + (b - a) * t;
}

}

Environment computeEnvironment(float timeOfDay) {
    const float angle = timeOfDay * 2.0f * kPi;
    const Vec3 sunDirection = normalize(
        {std::cos(angle) * 0.55f, std::sin(angle), 0.35f});

    const float elevation = sunDirection.y;
    const float day = clampf(elevation * 2.2f, 0.0f, 1.0f);
    const float sunset = clampf(1.0f - std::abs(elevation) * 4.0f, 0.0f, 1.0f);

    Environment env;
    env.sunDirection = sunDirection;
    env.dayFactor = day;

    const Vec3 nightZenith{0.01f, 0.015f, 0.05f};
    const Vec3 dayZenith{0.22f, 0.45f, 0.85f};
    const Vec3 nightHorizon{0.03f, 0.04f, 0.09f};
    const Vec3 dayHorizon{0.62f, 0.78f, 0.98f};

    env.skyZenith = mixColor(nightZenith, dayZenith, day);
    env.skyHorizon = mixColor(nightHorizon, dayHorizon, day);
    env.skyHorizon = mixColor(env.skyHorizon, Vec3{0.95f, 0.45f, 0.2f}, sunset * 0.55f);
    env.fogColor = env.skyHorizon;

    const Vec3 nightSun{0.15f, 0.18f, 0.28f};
    const Vec3 warmSun{1.0f, 0.45f, 0.18f};
    const Vec3 whiteSun{1.0f, 0.95f, 0.85f};
    env.sunColor = mixColor(warmSun, whiteSun, smoothstep01(0.0f, 0.5f, elevation));
    env.sunColor = mixColor(nightSun, env.sunColor, clampf(day * 1.2f, 0.0f, 1.0f));

    env.ambientSky = mixColor(Vec3{0.08f, 0.10f, 0.18f}, Vec3{0.55f, 0.65f, 0.8f}, day);
    env.ambientGround =
        mixColor(Vec3{0.035f, 0.04f, 0.06f}, Vec3{0.3f, 0.28f, 0.25f}, day);

    return env;
}

}
