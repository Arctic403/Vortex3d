#include "vortex/core/transform.hpp"

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

} // namespace

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
