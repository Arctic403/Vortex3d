#include "vortex/eval/modifier.hpp"

#include <bit>
#include <cmath>
#include <cstdint>

namespace vortex {
namespace {

[[nodiscard]] bool finite(const Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool usableScale(const Vec3 scale) noexcept {
    constexpr float minimumMagnitude = 1.0e-12F;
    return finite(scale) && std::abs(scale.x) > minimumMagnitude && std::abs(scale.y) > minimumMagnitude &&
           std::abs(scale.z) > minimumMagnitude;
}

struct RotationTerms final {
    float sinX = 0.0F;
    float cosX = 1.0F;
    float sinY = 0.0F;
    float cosY = 1.0F;
    float sinZ = 0.0F;
    float cosZ = 1.0F;
};

[[nodiscard]] RotationTerms rotationTerms(const Vec3 radians) noexcept {
    return RotationTerms{
        std::sin(radians.x),
        std::cos(radians.x),
        std::sin(radians.y),
        std::cos(radians.y),
        std::sin(radians.z),
        std::cos(radians.z)};
}

[[nodiscard]] Vec3 rotateXyz(Vec3 value, const RotationTerms& rotation) noexcept {
    value = {
        value.x,
        value.y * rotation.cosX - value.z * rotation.sinX,
        value.y * rotation.sinX + value.z * rotation.cosX};
    value = {
        value.x * rotation.cosY + value.z * rotation.sinY,
        value.y,
        -value.x * rotation.sinY + value.z * rotation.cosY};
    return {
        value.x * rotation.cosZ - value.y * rotation.sinZ,
        value.x * rotation.sinZ + value.y * rotation.cosZ,
        value.z};
}

[[nodiscard]] Vec3 normalize(const Vec3 value) noexcept {
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (lengthSquared <= 0.0F || !std::isfinite(lengthSquared)) {
        return {};
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return {value.x * inverseLength, value.y * inverseLength, value.z * inverseLength};
}

void transformNormals(
    AttributeSet& attributes,
    const AttributeDomain domain,
    const Vec3 scale,
    const RotationTerms& rotation) noexcept {
    auto* normals = attributes.values<Vec3>("normal", domain);
    if (normals == nullptr) {
        return;
    }

    for (Vec3& normal : *normals) {
        const Vec3 inverseScaled{normal.x / scale.x, normal.y / scale.y, normal.z / scale.z};
        normal = normalize(rotateXyz(inverseScaled, rotation));
    }
}

[[nodiscard]] std::uint64_t mixFloat(std::uint64_t hash, const float value) noexcept {
    constexpr std::uint64_t fnvPrime = 1099511628211ULL;
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    for (std::uint32_t shift = 0; shift < 32U; shift += 8U) {
        hash ^= static_cast<std::uint8_t>(bits >> shift);
        hash *= fnvPrime;
    }
    return hash;
}

} // namespace

AttributeSet& MeshModifier::mutableAttributes(EvaluatedMesh& mesh) noexcept { return mesh.attributes_; }

std::uint64_t TransformModifier::revisionToken() const noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    hash = mixFloat(hash, translation_.x);
    hash = mixFloat(hash, translation_.y);
    hash = mixFloat(hash, translation_.z);
    hash = mixFloat(hash, rotationRadians_.x);
    hash = mixFloat(hash, rotationRadians_.y);
    hash = mixFloat(hash, rotationRadians_.z);
    hash = mixFloat(hash, scale_.x);
    hash = mixFloat(hash, scale_.y);
    hash = mixFloat(hash, scale_.z);
    return hash;
}

ModifierApplyResult TransformModifier::apply(EvaluatedMesh& mesh) const {
    if (!finite(translation_) || !finite(rotationRadians_) || !usableScale(scale_)) {
        return {ModifierApplyError::InvalidTransform};
    }

    AttributeSet& attributes = mutableAttributes(mesh);
    auto* positions = attributes.values<Vec3>("position", AttributeDomain::Vertex);
    if (positions == nullptr || positions->size() != mesh.vertexCount()) {
        return {ModifierApplyError::MissingPositionAttribute};
    }

    const RotationTerms rotation = rotationTerms(rotationRadians_);
    for (Vec3& position : *positions) {
        Vec3 transformed{
            position.x * scale_.x,
            position.y * scale_.y,
            position.z * scale_.z};
        transformed = rotateXyz(transformed, rotation);
        position = {
            transformed.x + translation_.x,
            transformed.y + translation_.y,
            transformed.z + translation_.z};
    }

    transformNormals(attributes, AttributeDomain::Vertex, scale_, rotation);
    transformNormals(attributes, AttributeDomain::Corner, scale_, rotation);
    return {};
}

} // namespace vortex
