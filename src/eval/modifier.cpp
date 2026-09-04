#include "vortex/eval/modifier.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

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

[[nodiscard]] std::uint64_t mixByte(std::uint64_t hash, const std::uint8_t value) noexcept {
    constexpr std::uint64_t fnvPrime = 1099511628211ULL;
    hash ^= value;
    hash *= fnvPrime;
    return hash;
}

[[nodiscard]] bool validMirrorAxis(const MirrorAxis axis) noexcept {
    switch (axis) {
    case MirrorAxis::X:
    case MirrorAxis::Y:
    case MirrorAxis::Z:
        return true;
    }
    return false;
}

[[nodiscard]] bool canDoubleGeneratedCount(const std::size_t count) noexcept {
    constexpr auto maxIndex = std::numeric_limits<EvaluatedMesh::Index>::max();
    return count <= static_cast<std::size_t>(maxIndex) / 2U;
}

[[nodiscard]] bool duplicateAttributeRows(
    AttributeSet& attributes,
    const AttributeDomain domain,
    const std::size_t sourceCount) {
    for (std::size_t index = 0; index < sourceCount; ++index) {
        const auto row = attributes.captureDomainIndex(domain, index);
        if (!row || !attributes.appendDomainRow(domain, *row)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] float mirroredCoordinate(const float value, const float planeOffset) noexcept {
    return (2.0F * planeOffset) - value;
}

void mirrorVector(Vec3& value, const MirrorAxis axis, const float planeOffset, const bool isNormal) noexcept {
    switch (axis) {
    case MirrorAxis::X:
        value.x = isNormal ? -value.x : mirroredCoordinate(value.x, planeOffset);
        break;
    case MirrorAxis::Y:
        value.y = isNormal ? -value.y : mirroredCoordinate(value.y, planeOffset);
        break;
    case MirrorAxis::Z:
        value.z = isNormal ? -value.z : mirroredCoordinate(value.z, planeOffset);
        break;
    }
}

void mirrorNormals(
    AttributeSet& attributes,
    const AttributeDomain domain,
    const std::size_t sourceCount,
    const MirrorAxis axis) noexcept {
    auto* normals = attributes.values<Vec3>("normal", domain);
    if (normals == nullptr || normals->size() < sourceCount * 2U) {
        return;
    }

    for (std::size_t index = 0; index < sourceCount; ++index) {
        mirrorVector((*normals)[sourceCount + index], axis, 0.0F, true);
    }
}

} // namespace

AttributeSet& MeshModifier::mutableAttributes(EvaluatedMesh& mesh) noexcept { return mesh.attributes_; }
std::vector<EvaluatedVertex>& MeshModifier::mutableVertices(EvaluatedMesh& mesh) noexcept { return mesh.vertices_; }
std::vector<EvaluatedEdge>& MeshModifier::mutableEdges(EvaluatedMesh& mesh) noexcept { return mesh.edges_; }
std::vector<EvaluatedFace>& MeshModifier::mutableFaces(EvaluatedMesh& mesh) noexcept { return mesh.faces_; }
std::vector<EvaluatedCorner>& MeshModifier::mutableCorners(EvaluatedMesh& mesh) noexcept { return mesh.corners_; }

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

std::uint64_t MirrorModifier::revisionToken() const noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    hash = mixByte(hash, static_cast<std::uint8_t>(axis_));
    return mixFloat(hash, planeOffset_);
}

ModifierApplyResult MirrorModifier::apply(EvaluatedMesh& mesh) const {
    if (!validMirrorAxis(axis_) || !std::isfinite(planeOffset_)) {
        return {ModifierApplyError::InvalidMirror};
    }

    const std::size_t sourceVertexCount = mesh.vertexCount();
    const std::size_t sourceEdgeCount = mesh.edgeCount();
    const std::size_t sourceFaceCount = mesh.faceCount();
    const std::size_t sourceCornerCount = mesh.cornerCount();

    if (!canDoubleGeneratedCount(sourceVertexCount) || !canDoubleGeneratedCount(sourceEdgeCount) ||
        !canDoubleGeneratedCount(sourceFaceCount) || !canDoubleGeneratedCount(sourceCornerCount)) {
        return {ModifierApplyError::GeneratedTopologyOverflow};
    }

    AttributeSet& attributes = mutableAttributes(mesh);
    auto* positions = attributes.values<Vec3>("position", AttributeDomain::Vertex);
    if (positions == nullptr || positions->size() != sourceVertexCount) {
        return {ModifierApplyError::MissingPositionAttribute};
    }

    std::vector<EvaluatedVertex>& vertices = mutableVertices(mesh);
    std::vector<EvaluatedEdge>& edges = mutableEdges(mesh);
    std::vector<EvaluatedFace>& faces = mutableFaces(mesh);
    std::vector<EvaluatedCorner>& corners = mutableCorners(mesh);

    vertices.reserve(sourceVertexCount * 2U);
    edges.reserve(sourceEdgeCount * 2U);
    faces.reserve(sourceFaceCount * 2U);
    corners.reserve(sourceCornerCount * 2U);

    for (std::size_t index = 0; index < sourceVertexCount; ++index) {
        const EvaluatedVertex source = vertices[index];
        vertices.push_back(source);
    }

    const EvaluatedMesh::Index vertexOffset = static_cast<EvaluatedMesh::Index>(sourceVertexCount);
    for (std::size_t index = 0; index < sourceEdgeCount; ++index) {
        const EvaluatedEdge source = edges[index];
        edges.push_back(EvaluatedEdge{
            static_cast<EvaluatedMesh::Index>(vertexOffset + source.vertexA),
            static_cast<EvaluatedMesh::Index>(vertexOffset + source.vertexB),
            source.sourceId});
    }

    const EvaluatedMesh::Index cornerOffset = static_cast<EvaluatedMesh::Index>(sourceCornerCount);
    for (std::size_t index = 0; index < sourceFaceCount; ++index) {
        const EvaluatedFace source = faces[index];
        faces.push_back(EvaluatedFace{
            static_cast<EvaluatedMesh::Index>(cornerOffset + source.firstCorner),
            source.cornerCount,
            source.sourceId});
    }

    const EvaluatedMesh::Index edgeOffset = static_cast<EvaluatedMesh::Index>(sourceEdgeCount);
    std::vector<std::vector<EvaluatedMesh::Index>> mirroredRadialUses(sourceEdgeCount);
    for (std::size_t index = 0; index < sourceCornerCount; ++index) {
        const EvaluatedCorner source = corners[index];
        const EvaluatedCorner sourcePrevious = corners[source.prev];
        const EvaluatedMesh::Index mirroredEdge =
            static_cast<EvaluatedMesh::Index>(edgeOffset + sourcePrevious.edge);
        const EvaluatedMesh::Index mirroredCorner =
            static_cast<EvaluatedMesh::Index>(cornerOffset + static_cast<EvaluatedMesh::Index>(index));

        corners.push_back(EvaluatedCorner{
            static_cast<EvaluatedMesh::Index>(vertexOffset + source.vertex),
            mirroredEdge,
            static_cast<EvaluatedMesh::Index>(cornerOffset + source.prev),
            static_cast<EvaluatedMesh::Index>(cornerOffset + source.next),
            mirroredCorner,
            mirroredCorner,
            source.sourceId});

        mirroredRadialUses[static_cast<std::size_t>(mirroredEdge - edgeOffset)].push_back(mirroredCorner);
    }

    for (const auto& uses : mirroredRadialUses) {
        if (uses.empty()) {
            continue;
        }
        for (std::size_t index = 0; index < uses.size(); ++index) {
            EvaluatedCorner& corner = corners[uses[index]];
            corner.radialNext = uses[(index + 1U) % uses.size()];
            corner.radialPrev = uses[(index + uses.size() - 1U) % uses.size()];
        }
    }

    if (!duplicateAttributeRows(attributes, AttributeDomain::Vertex, sourceVertexCount) ||
        !duplicateAttributeRows(attributes, AttributeDomain::Edge, sourceEdgeCount) ||
        !duplicateAttributeRows(attributes, AttributeDomain::Face, sourceFaceCount) ||
        !duplicateAttributeRows(attributes, AttributeDomain::Corner, sourceCornerCount)) {
        return {ModifierApplyError::AttributeCopyFailed};
    }

    positions = attributes.values<Vec3>("position", AttributeDomain::Vertex);
    if (positions == nullptr || positions->size() != sourceVertexCount * 2U) {
        return {ModifierApplyError::AttributeCopyFailed};
    }
    for (std::size_t index = 0; index < sourceVertexCount; ++index) {
        mirrorVector((*positions)[sourceVertexCount + index], axis_, planeOffset_, false);
    }

    mirrorNormals(attributes, AttributeDomain::Vertex, sourceVertexCount, axis_);
    mirrorNormals(attributes, AttributeDomain::Corner, sourceCornerCount, axis_);
    return {};
}

} // namespace vortex
