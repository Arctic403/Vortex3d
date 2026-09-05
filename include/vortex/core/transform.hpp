#pragma once

#include "vortex/mesh/attribute.hpp"

#include <array>
#include <optional>

namespace vortex {

struct Quaternion final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;

    [[nodiscard]] bool operator==(const Quaternion&) const noexcept = default;
};

struct ObjectTransform final {
    Vec3 translation{};
    Vec3 rotationRadians{};
    Vec3 scale{1.0F, 1.0F, 1.0F};

    [[nodiscard]] bool operator==(const ObjectTransform&) const noexcept = default;
};

struct TransformMatrix final {
    // Column-major, column-vector convention. Translation occupies elements 12..14.
    std::array<float, 16> values{};

    [[nodiscard]] bool operator==(const TransformMatrix&) const noexcept = default;
};

[[nodiscard]] bool isFiniteQuaternion(const Quaternion& quaternion) noexcept;
[[nodiscard]] std::optional<Quaternion> quaternionFromEulerRadians(Vec3 eulerRadians) noexcept;
[[nodiscard]] std::optional<Quaternion> quaternionFromAxisAngle(
    Vec3 axis,
    float radians) noexcept;
[[nodiscard]] std::optional<Quaternion> multiplyQuaternions(
    const Quaternion& a,
    const Quaternion& b) noexcept;
[[nodiscard]] std::optional<Vec3> eulerRadiansFromQuaternionNearest(
    const Quaternion& quaternion,
    Vec3 referenceEulerRadians) noexcept;
[[nodiscard]] bool isFiniteObjectTransform(const ObjectTransform& transform) noexcept;
[[nodiscard]] TransformMatrix identityTransformMatrix() noexcept;
// Local transform order is T * Rz * Ry * Rx * S. Parent composition is parentWorld * local.
[[nodiscard]] TransformMatrix objectTransformMatrix(const ObjectTransform& transform) noexcept;
[[nodiscard]] TransformMatrix multiplyTransformMatrices(
    const TransformMatrix& a,
    const TransformMatrix& b) noexcept;
[[nodiscard]] Vec3 transformPoint(const TransformMatrix& matrix, Vec3 point) noexcept;
[[nodiscard]] Vec3 transformVector(const TransformMatrix& matrix, Vec3 vector) noexcept;

} // namespace vortex
