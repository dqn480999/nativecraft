#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace nc {

constexpr float kPi = 3.14159265358979323846f;

inline float radians(float degrees) {
    return degrees * (kPi / 180.0f);
}

inline float clampf(float value, float lo, float hi) {
    return std::max(lo, std::min(hi, value));
}

inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    Vec2() = default;
    Vec2(float x_, float y_)
        : x(x_)
        , y(y_) {}
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float x_, float y_, float z_)
        : x(x_)
        , y(y_)
        , z(z_) {}

    Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
    Vec3 operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
    Vec3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    Vec3 operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar}; }
    Vec3& operator+=(const Vec3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }
    Vec3& operator-=(const Vec3& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }
};

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float lengthSquared(const Vec3& v) {
    return dot(v, v);
}

inline float length(const Vec3& v) {
    return std::sqrt(lengthSquared(v));
}

inline Vec3 normalize(const Vec3& v) {
    const float len = length(v);
    if (len <= 1e-8f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return v / len;
}

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    Vec4() = default;
    Vec4(float x_, float y_, float z_, float w_)
        : x(x_)
        , y(y_)
        , z(z_)
        , w(w_) {}
};

struct Mat4 {
    std::array<float, 16> m{};

    static Mat4 identity() {
        Mat4 out;
        out.m = {1.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 1.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 1.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 1.0f};
        return out;
    }

    static Mat4 perspective(float fovYRadians, float aspect, float nearPlane, float farPlane) {
        Mat4 out;
        const float f = 1.0f / std::tan(fovYRadians * 0.5f);
        out.m[0] = f / aspect;
        out.m[5] = f;
        out.m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
        out.m[11] = -1.0f;
        out.m[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
        return out;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        const Vec3 f = normalize(center - eye);
        const Vec3 s = normalize(cross(f, up));
        const Vec3 u = cross(s, f);

        Mat4 out = identity();
        out.m[0] = s.x;
        out.m[4] = s.y;
        out.m[8] = s.z;
        out.m[12] = -dot(s, eye);

        out.m[1] = u.x;
        out.m[5] = u.y;
        out.m[9] = u.z;
        out.m[13] = -dot(u, eye);

        out.m[2] = -f.x;
        out.m[6] = -f.y;
        out.m[10] = -f.z;
        out.m[14] = dot(f, eye);
        return out;
    }

    static Mat4 orthographic(float left, float right, float bottom, float top,
                             float nearPlane, float farPlane) {
        Mat4 out = identity();
        out.m[0] = 2.0f / (right - left);
        out.m[5] = 2.0f / (top - bottom);
        out.m[10] = -2.0f / (farPlane - nearPlane);
        out.m[12] = -(right + left) / (right - left);
        out.m[13] = -(top + bottom) / (top - bottom);
        out.m[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
        return out;
    }

    Mat4 operator*(const Mat4& other) const {
        Mat4 out;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += m[k * 4 + row] * other.m[col * 4 + k];
                }
                out.m[col * 4 + row] = sum;
            }
        }
        return out;
    }

    const float* data() const { return m.data(); }
};

struct Aabb {
    Vec3 min;
    Vec3 max;

    bool intersects(const Aabb& other) const {
        return min.x < other.max.x && max.x > other.min.x
            && min.y < other.max.y && max.y > other.min.y
            && min.z < other.max.z && max.z > other.min.z;
    }
};

}
