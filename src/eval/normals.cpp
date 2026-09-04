#include "vortex/eval/normals.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace vortex {
namespace {

using Index = EvaluatedMesh::Index;
constexpr Index invalidIndex = std::numeric_limits<Index>::max();
constexpr double minimumLengthSquared = 1.0e-30;

[[nodiscard]] bool finite(const Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] Vec3 subtract(const Vec3 a, const Vec3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] Vec3 cross(const Vec3 a, const Vec3 b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

[[nodiscard]] double dot(const Vec3 a, const Vec3 b) noexcept {
    return static_cast<double>(a.x) * static_cast<double>(b.x) +
           static_cast<double>(a.y) * static_cast<double>(b.y) +
           static_cast<double>(a.z) * static_cast<double>(b.z);
}

[[nodiscard]] double lengthSquared(const Vec3 value) noexcept {
    return dot(value, value);
}

[[nodiscard]] bool normalize(const Vec3 value, Vec3& result) noexcept {
    const double squared = lengthSquared(value);
    if (!(squared > minimumLengthSquared) || !std::isfinite(squared)) {
        return false;
    }
    const double inverseLength = 1.0 / std::sqrt(squared);
    result = {
        static_cast<float>(static_cast<double>(value.x) * inverseLength),
        static_cast<float>(static_cast<double>(value.y) * inverseLength),
        static_cast<float>(static_cast<double>(value.z) * inverseLength)};
    return finite(result);
}

class DisjointSet final {
public:
    explicit DisjointSet(const std::size_t count) : parent_(count), rank_(count, 0U) {
        for (std::size_t index = 0; index < count; ++index) {
            parent_[index] = static_cast<Index>(index);
        }
    }

    [[nodiscard]] Index find(Index value) noexcept {
        Index root = value;
        while (parent_[root] != root) {
            root = parent_[root];
        }
        while (parent_[value] != value) {
            const Index next = parent_[value];
            parent_[value] = root;
            value = next;
        }
        return root;
    }

    void unite(const Index left, const Index right) noexcept {
        Index leftRoot = find(left);
        Index rightRoot = find(right);
        if (leftRoot == rightRoot) {
            return;
        }
        if (rank_[leftRoot] < rank_[rightRoot]) {
            const Index temporary = leftRoot;
            leftRoot = rightRoot;
            rightRoot = temporary;
        }
        parent_[rightRoot] = leftRoot;
        if (rank_[leftRoot] == rank_[rightRoot]) {
            ++rank_[leftRoot];
        }
    }

private:
    std::vector<Index> parent_;
    std::vector<std::uint8_t> rank_;
};

[[nodiscard]] Index cornerAtEndpoint(
    const std::vector<EvaluatedCorner>& corners,
    const Index edgeUseCorner,
    const Index vertex) noexcept {
    if (edgeUseCorner >= corners.size()) {
        return invalidIndex;
    }
    const EvaluatedCorner& corner = corners[edgeUseCorner];
    if (corner.vertex == vertex) {
        return edgeUseCorner;
    }
    if (corner.next >= corners.size()) {
        return invalidIndex;
    }
    return corners[corner.next].vertex == vertex ? corner.next : invalidIndex;
}

} // namespace

NormalGenerationResult DerivedNormalsGenerator::generate(EvaluatedMesh& mesh) {
    const std::size_t vertexCount = mesh.vertices_.size();
    const std::size_t edgeCount = mesh.edges_.size();
    const std::size_t faceCount = mesh.faces_.size();
    const std::size_t cornerCount = mesh.corners_.size();

    const auto* positions = mesh.attributes_.values<Vec3>("position", AttributeDomain::Vertex);
    if (positions == nullptr || positions->size() != vertexCount) {
        return {NormalGenerationError::MissingPositionAttribute};
    }

    const AttributeLayer* edgeSharpLayer = mesh.attributes_.layer("sharp", AttributeDomain::Edge);
    if (edgeSharpLayer != nullptr && edgeSharpLayer->type != AttributeType::Bool) {
        return {NormalGenerationError::InvalidShadingAttribute};
    }
    const auto* edgeSharp = mesh.attributes_.values<bool>("sharp", AttributeDomain::Edge);
    if (edgeSharp != nullptr && edgeSharp->size() != edgeCount) {
        return {NormalGenerationError::InvalidShadingAttribute};
    }

    const AttributeLayer* faceSharpLayer = mesh.attributes_.layer("sharp_face", AttributeDomain::Face);
    if (faceSharpLayer != nullptr && faceSharpLayer->type != AttributeType::Bool) {
        return {NormalGenerationError::InvalidShadingAttribute};
    }
    const auto* faceSharp = mesh.attributes_.values<bool>("sharp_face", AttributeDomain::Face);
    if (faceSharp != nullptr && faceSharp->size() != faceCount) {
        return {NormalGenerationError::InvalidShadingAttribute};
    }

    const AttributeLayer* normalLayer = mesh.attributes_.layer("normal", AttributeDomain::Corner);
    if (normalLayer != nullptr && normalLayer->type != AttributeType::Vec3) {
        return {NormalGenerationError::InvalidShadingAttribute};
    }

    if (cornerCount == 0U) {
        if (normalLayer == nullptr && !mesh.attributes_.create<Vec3>("normal", AttributeDomain::Corner, Vec3{})) {
            return {NormalGenerationError::AttributeWriteFailed};
        }
        return {};
    }

    std::vector<Index> cornerFace(cornerCount, invalidIndex);
    std::vector<Vec3> faceNormals(faceCount);
    std::vector<float> cornerAngles(cornerCount, 0.0F);

    for (std::size_t faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
        const EvaluatedFace& face = mesh.faces_[faceIndex];
        if (face.cornerCount < 3U || face.firstCorner >= cornerCount) {
            return {NormalGenerationError::InvalidTopology};
        }

        std::vector<Index> cycle;
        cycle.reserve(face.cornerCount);
        Index cursor = face.firstCorner;
        for (std::uint32_t step = 0; step < face.cornerCount; ++step) {
            if (cursor >= cornerCount || cornerFace[cursor] != invalidIndex) {
                return {NormalGenerationError::InvalidTopology};
            }
            const EvaluatedCorner& corner = mesh.corners_[cursor];
            if (corner.vertex >= vertexCount || corner.next >= cornerCount || corner.prev >= cornerCount) {
                return {NormalGenerationError::InvalidTopology};
            }
            if (!finite((*positions)[corner.vertex])) {
                return {NormalGenerationError::NonFinitePosition};
            }
            cornerFace[cursor] = static_cast<Index>(faceIndex);
            cycle.push_back(cursor);
            cursor = corner.next;
        }
        if (cursor != face.firstCorner) {
            return {NormalGenerationError::InvalidTopology};
        }

        double newellX = 0.0;
        double newellY = 0.0;
        double newellZ = 0.0;
        for (std::size_t index = 0; index < cycle.size(); ++index) {
            const Vec3 current = (*positions)[mesh.corners_[cycle[index]].vertex];
            const Vec3 next = (*positions)[mesh.corners_[cycle[(index + 1U) % cycle.size()]].vertex];
            newellX += static_cast<double>(current.y - next.y) * static_cast<double>(current.z + next.z);
            newellY += static_cast<double>(current.z - next.z) * static_cast<double>(current.x + next.x);
            newellZ += static_cast<double>(current.x - next.x) * static_cast<double>(current.y + next.y);
        }

        const Vec3 newell{
            static_cast<float>(newellX),
            static_cast<float>(newellY),
            static_cast<float>(newellZ)};
        if (!normalize(newell, faceNormals[faceIndex])) {
            return {NormalGenerationError::DegenerateFace};
        }

        for (const Index cornerIndex : cycle) {
            const EvaluatedCorner& corner = mesh.corners_[cornerIndex];
            const EvaluatedCorner& previous = mesh.corners_[corner.prev];
            const EvaluatedCorner& next = mesh.corners_[corner.next];
            if (previous.vertex >= vertexCount || next.vertex >= vertexCount) {
                return {NormalGenerationError::InvalidTopology};
            }

            const Vec3 center = (*positions)[corner.vertex];
            const Vec3 incoming = subtract((*positions)[previous.vertex], center);
            const Vec3 outgoing = subtract((*positions)[next.vertex], center);
            const double incomingLengthSquared = lengthSquared(incoming);
            const double outgoingLengthSquared = lengthSquared(outgoing);
            if (!(incomingLengthSquared > minimumLengthSquared) ||
                !(outgoingLengthSquared > minimumLengthSquared) ||
                !std::isfinite(incomingLengthSquared) || !std::isfinite(outgoingLengthSquared)) {
                return {NormalGenerationError::DegenerateFace};
            }

            const Vec3 turn = cross(incoming, outgoing);
            const double turnLength = std::sqrt(lengthSquared(turn));
            const double cosineTerm = dot(incoming, outgoing);
            const double angle = std::atan2(turnLength, cosineTerm);
            if (!std::isfinite(angle) || angle < 0.0) {
                return {NormalGenerationError::DegenerateFace};
            }
            cornerAngles[cornerIndex] = static_cast<float>(angle);
        }
    }

    for (const Index faceIndex : cornerFace) {
        if (faceIndex == invalidIndex) {
            return {NormalGenerationError::InvalidTopology};
        }
    }

    std::vector<Index> firstEdgeUse(edgeCount, invalidIndex);
    std::vector<Index> secondEdgeUse(edgeCount, invalidIndex);
    std::vector<std::uint32_t> edgeUseCount(edgeCount, 0U);
    for (std::size_t cornerIndex = 0; cornerIndex < cornerCount; ++cornerIndex) {
        const EvaluatedCorner& corner = mesh.corners_[cornerIndex];
        if (corner.edge >= edgeCount) {
            return {NormalGenerationError::InvalidTopology};
        }
        const std::size_t edgeIndex = corner.edge;
        if (edgeUseCount[edgeIndex] == 0U) {
            firstEdgeUse[edgeIndex] = static_cast<Index>(cornerIndex);
        } else if (edgeUseCount[edgeIndex] == 1U) {
            secondEdgeUse[edgeIndex] = static_cast<Index>(cornerIndex);
        }
        ++edgeUseCount[edgeIndex];
    }

    const auto isFaceSharp = [faceSharp](const std::size_t faceIndex) noexcept {
        return faceSharp == nullptr ? true : static_cast<bool>((*faceSharp)[faceIndex]);
    };
    const auto isEdgeSharp = [edgeSharp](const std::size_t edgeIndex) noexcept {
        return edgeSharp == nullptr ? false : static_cast<bool>((*edgeSharp)[edgeIndex]);
    };

    DisjointSet smoothFans(cornerCount);
    for (std::size_t edgeIndex = 0; edgeIndex < edgeCount; ++edgeIndex) {
        const EvaluatedEdge& edge = mesh.edges_[edgeIndex];
        if (edge.vertexA >= vertexCount || edge.vertexB >= vertexCount || edge.vertexA == edge.vertexB) {
            return {NormalGenerationError::InvalidTopology};
        }
        if (edgeUseCount[edgeIndex] != 2U || isEdgeSharp(edgeIndex)) {
            continue;
        }

        const Index firstUse = firstEdgeUse[edgeIndex];
        const Index secondUse = secondEdgeUse[edgeIndex];
        if (firstUse == invalidIndex || secondUse == invalidIndex || firstUse == secondUse) {
            return {NormalGenerationError::InvalidTopology};
        }
        if (mesh.corners_[firstUse].radialNext != secondUse || mesh.corners_[secondUse].radialNext != firstUse ||
            mesh.corners_[firstUse].radialPrev != secondUse || mesh.corners_[secondUse].radialPrev != firstUse) {
            return {NormalGenerationError::InvalidTopology};
        }

        const Index firstFace = cornerFace[firstUse];
        const Index secondFace = cornerFace[secondUse];
        if (firstFace >= faceCount || secondFace >= faceCount || firstFace == secondFace) {
            return {NormalGenerationError::InvalidTopology};
        }
        if (isFaceSharp(firstFace) || isFaceSharp(secondFace)) {
            continue;
        }

        const Index firstAtA = cornerAtEndpoint(mesh.corners_, firstUse, edge.vertexA);
        const Index secondAtA = cornerAtEndpoint(mesh.corners_, secondUse, edge.vertexA);
        const Index firstAtB = cornerAtEndpoint(mesh.corners_, firstUse, edge.vertexB);
        const Index secondAtB = cornerAtEndpoint(mesh.corners_, secondUse, edge.vertexB);
        if (firstAtA == invalidIndex || secondAtA == invalidIndex || firstAtB == invalidIndex ||
            secondAtB == invalidIndex) {
            return {NormalGenerationError::InvalidTopology};
        }

        smoothFans.unite(firstAtA, secondAtA);
        smoothFans.unite(firstAtB, secondAtB);
    }

    std::vector<Vec3> accumulated(cornerCount, Vec3{});
    std::vector<Vec3> generated(cornerCount, Vec3{});
    for (std::size_t cornerIndex = 0; cornerIndex < cornerCount; ++cornerIndex) {
        const Index faceIndex = cornerFace[cornerIndex];
        if (isFaceSharp(faceIndex)) {
            generated[cornerIndex] = faceNormals[faceIndex];
            continue;
        }

        const Index root = smoothFans.find(static_cast<Index>(cornerIndex));
        const float weight = cornerAngles[cornerIndex];
        accumulated[root].x += faceNormals[faceIndex].x * weight;
        accumulated[root].y += faceNormals[faceIndex].y * weight;
        accumulated[root].z += faceNormals[faceIndex].z * weight;
    }

    for (std::size_t cornerIndex = 0; cornerIndex < cornerCount; ++cornerIndex) {
        const Index faceIndex = cornerFace[cornerIndex];
        if (isFaceSharp(faceIndex)) {
            continue;
        }
        const Index root = smoothFans.find(static_cast<Index>(cornerIndex));
        if (!normalize(accumulated[root], generated[cornerIndex])) {
            return {NormalGenerationError::DegenerateSmoothFan};
        }
    }

    if (normalLayer == nullptr) {
        if (!mesh.attributes_.create<Vec3>("normal", AttributeDomain::Corner, Vec3{})) {
            return {NormalGenerationError::AttributeWriteFailed};
        }
    }
    auto* normals = mesh.attributes_.values<Vec3>("normal", AttributeDomain::Corner);
    if (normals == nullptr || normals->size() != cornerCount) {
        return {NormalGenerationError::AttributeWriteFailed};
    }
    *normals = std::move(generated);
    return {};
}

} // namespace vortex
