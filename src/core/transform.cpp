#include "vortex/core/transform.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace vortex {
namespace {

[[nodiscard]] bool finiteVec3(const Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] TransformMatrix translationMatrix(const Vec3 value) noexcept {
    TransformMatrix result = identityTransformMatrix();
    result.values[12] = value.x;
    result.values[13] = value.y;
    result.values[14] = value.z;
    return result;
}

[[nodiscard]] TransformMatrix scaleMatrix(const Vec3 value) noexcept {
    TransformMatrix result{};
    result.values[0] = value.x;
    result.values[5] = value.y;
    result.values[10] = value.z;
    result.values[15] = 1.0F;
    return result;
}

[[nodiscard]] TransformMatrix rotationX(const float radians) noexcept {
    TransformMatrix result = identityTransformMatrix();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    result.values[5] = c;
    result.values[6] = s;
    result.values[9] = -s;
    result.values[10] = c;
    return result;
}

[[nodiscard]] TransformMatrix rotationY(const float radians) noexcept {
    TransformMatrix result = identityTransformMatrix();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    result.values[0] = c;
    result.values[2] = -s;
    result.values[8] = s;
    result.values[10] = c;
    return result;
}

[[nodiscard]] TransformMatrix rotationZ(const float radians) noexcept {
    TransformMatrix result = identityTransformMatrix();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    result.values[0] = c;
    result.values[1] = s;
    result.values[4] = -s;
    result.values[5] = c;
    return result;
}


[[nodiscard]] bool finiteQuaternion(const Quaternion value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] std::optional<Quaternion> normalizedQuaternion(
    const Quaternion value) noexcept {
    if (!finiteQuaternion(value)) {
        return std::nullopt;
    }
    const float lengthSquared =
        value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12F) {
        return std::nullopt;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return Quaternion{
        value.x * inverseLength,
        value.y * inverseLength,
        value.z * inverseLength,
        value.w * inverseLength,
    };
}

[[nodiscard]] Quaternion multiplyQuaternionRaw(
    const Quaternion a,
    const Quaternion b) noexcept {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

[[nodiscard]] float nearestEquivalentAngle(
    const float angle,
    const float reference) noexcept {
    constexpr float kTwoPi = 6.2831853071795864769F;
    return reference + std::remainder(angle - reference, kTwoPi);
}

[[nodiscard]] float eulerDistanceSquared(
    const Vec3 a,
    const Vec3 b) noexcept {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

} // namespace


bool isFiniteQuaternion(const Quaternion& quaternion) noexcept {
    return finiteQuaternion(quaternion);
}

std::optional<Quaternion> quaternionFromEulerRadians(
    const Vec3 eulerRadians) noexcept {
    if (!finiteVec3(eulerRadians)) {
        return std::nullopt;
    }

    const float halfX = eulerRadians.x * 0.5F;
    const float halfY = eulerRadians.y * 0.5F;
    const float halfZ = eulerRadians.z * 0.5F;
    const Quaternion qx{std::sin(halfX), 0.0F, 0.0F, std::cos(halfX)};
    const Quaternion qy{0.0F, std::sin(halfY), 0.0F, std::cos(halfY)};
    const Quaternion qz{0.0F, 0.0F, std::sin(halfZ), std::cos(halfZ)};

    return normalizedQuaternion(
        multiplyQuaternionRaw(qz, multiplyQuaternionRaw(qy, qx)));
}

std::optional<Quaternion> quaternionFromAxisAngle(
    const Vec3 axis,
    const float radians) noexcept {
    if (!finiteVec3(axis) || !std::isfinite(radians)) {
        return std::nullopt;
    }

    const float axisLengthSquared = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
    if (!std::isfinite(axisLengthSquared) || axisLengthSquared <= 1.0e-12F) {
        return std::nullopt;
    }

    const float inverseAxisLength = 1.0F / std::sqrt(axisLengthSquared);
    const float halfAngle = radians * 0.5F;
    const float sine = std::sin(halfAngle);
    return normalizedQuaternion({
        axis.x * inverseAxisLength * sine,
        axis.y * inverseAxisLength * sine,
        axis.z * inverseAxisLength * sine,
        std::cos(halfAngle),
    });
}

std::optional<Quaternion> multiplyQuaternions(
    const Quaternion& a,
    const Quaternion& b) noexcept {
    const auto normalizedA = normalizedQuaternion(a);
    const auto normalizedB = normalizedQuaternion(b);
    if (!normalizedA || !normalizedB) {
        return std::nullopt;
    }
    return normalizedQuaternion(multiplyQuaternionRaw(*normalizedA, *normalizedB));
}

std::optional<Vec3> eulerRadiansFromQuaternionNearest(
    const Quaternion& quaternion,
    const Vec3 referenceEulerRadians) noexcept {
    const auto normalized = normalizedQuaternion(quaternion);
    if (!normalized || !finiteVec3(referenceEulerRadians)) {
        return std::nullopt;
    }

    const Quaternion q = *normalized;
    const float sinRollCosPitch = 2.0F * (q.w * q.x + q.y * q.z);
    const float cosRollCosPitch = 1.0F - 2.0F * (q.x * q.x + q.y * q.y);
    const float sinPitch = std::clamp(
        2.0F * (q.w * q.y - q.z * q.x),
        -1.0F,
        1.0F);
    const float sinYawCosPitch = 2.0F * (q.w * q.z + q.x * q.y);
    const float cosYawCosPitch = 1.0F - 2.0F * (q.y * q.y + q.z * q.z);

    const Vec3 principal{
        std::atan2(sinRollCosPitch, cosRollCosPitch),
        std::asin(sinPitch),
        std::atan2(sinYawCosPitch, cosYawCosPitch),
    };

    Vec3 primary{
        nearestEquivalentAngle(principal.x, referenceEulerRadians.x),
        nearestEquivalentAngle(principal.y, referenceEulerRadians.y),
        nearestEquivalentAngle(principal.z, referenceEulerRadians.z),
    };

    constexpr float kPi = 3.14159265358979323846F;
    const float alternatePitch =
        principal.y >= 0.0F ? kPi - principal.y : -kPi - principal.y;
    Vec3 alternate{
        nearestEquivalentAngle(principal.x + kPi, referenceEulerRadians.x),
        nearestEquivalentAngle(alternatePitch, referenceEulerRadians.y),
        nearestEquivalentAngle(principal.z + kPi, referenceEulerRadians.z),
    };

    const Vec3 result =
        eulerDistanceSquared(primary, referenceEulerRadians) <=
                eulerDistanceSquared(alternate, referenceEulerRadians)
            ? primary
            : alternate;
    return finiteVec3(result) ? std::optional<Vec3>{result} : std::nullopt;
}

bool isFiniteObjectTransform(const ObjectTransform& transform) noexcept {
    return finiteVec3(transform.translation) &&
           finiteVec3(transform.rotationRadians) &&
           finiteVec3(transform.scale);
}

TransformMatrix identityTransformMatrix() noexcept {
    TransformMatrix result{};
    result.values[0] = 1.0F;
    result.values[5] = 1.0F;
    result.values[10] = 1.0F;
    result.values[15] = 1.0F;
    return result;
}

TransformMatrix multiplyTransformMatrices(
    const TransformMatrix& a,
    const TransformMatrix& b) noexcept {
    TransformMatrix result{};
    for (std::size_t column = 0; column < 4U; ++column) {
        for (std::size_t row = 0; row < 4U; ++row) {
            float value = 0.0F;
            for (std::size_t inner = 0; inner < 4U; ++inner) {
                value += a.values[inner * 4U + row] * b.values[column * 4U + inner];
            }
            result.values[column * 4U + row] = value;
        }
    }
    return result;
}

TransformMatrix objectTransformMatrix(const ObjectTransform& transform) noexcept {
    const TransformMatrix rotation = multiplyTransformMatrices(
        rotationZ(transform.rotationRadians.z),
        multiplyTransformMatrices(
            rotationY(transform.rotationRadians.y),
            rotationX(transform.rotationRadians.x)));
    return multiplyTransformMatrices(
        translationMatrix(transform.translation),
        multiplyTransformMatrices(rotation, scaleMatrix(transform.scale)));
}

Vec3 transformPoint(const TransformMatrix& matrix, const Vec3 point) noexcept {
    return {
        matrix.values[0] * point.x + matrix.values[4] * point.y + matrix.values[8] * point.z + matrix.values[12],
        matrix.values[1] * point.x + matrix.values[5] * point.y + matrix.values[9] * point.z + matrix.values[13],
        matrix.values[2] * point.x + matrix.values[6] * point.y + matrix.values[10] * point.z + matrix.values[14],
    };
}

Vec3 transformVector(const TransformMatrix& matrix, const Vec3 vector) noexcept {
    return {
        matrix.values[0] * vector.x + matrix.values[4] * vector.y + matrix.values[8] * vector.z,
        matrix.values[1] * vector.x + matrix.values[5] * vector.y + matrix.values[9] * vector.z,
        matrix.values[2] * vector.x + matrix.values[6] * vector.y + matrix.values[10] * vector.z,
    };
}

} // namespace vortex
