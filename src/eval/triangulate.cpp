#include "vortex/eval/modifier.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace vortex {
namespace {

using Index = EvaluatedMesh::Index;

struct Point2 final {
    double x = 0.0;
    double y = 0.0;
};

struct Normal3 final {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

enum class ProjectionAxis : std::uint8_t {
    DropX,
    DropY,
    DropZ,
};

struct TriangleRecord final {
    std::array<Index, 3> corners{};
    std::size_t sourceFaceIndex = 0;
};

[[nodiscard]] bool collectFaceCycle(
    const EvaluatedFace& face,
    const std::vector<EvaluatedCorner>& corners,
    std::vector<Index>& cycle) {
    if (face.cornerCount < 3U || static_cast<std::size_t>(face.firstCorner) >= corners.size()) {
        return false;
    }

    cycle.clear();
    cycle.reserve(face.cornerCount);
    Index cursor = face.firstCorner;
    for (std::uint32_t step = 0; step < face.cornerCount; ++step) {
        if (static_cast<std::size_t>(cursor) >= corners.size()) {
            return false;
        }
        cycle.push_back(cursor);
        cursor = corners[cursor].next;
    }
    return cursor == face.firstCorner;
}

[[nodiscard]] Normal3 newellNormal(
    const std::vector<Index>& cycle,
    const std::vector<EvaluatedCorner>& corners,
    const std::vector<Vec3>& positions) noexcept {
    Normal3 normal;
    for (std::size_t index = 0; index < cycle.size(); ++index) {
        const EvaluatedCorner& currentCorner = corners[cycle[index]];
        const EvaluatedCorner& nextCorner = corners[cycle[(index + 1U) % cycle.size()]];
        if (static_cast<std::size_t>(currentCorner.vertex) >= positions.size() ||
            static_cast<std::size_t>(nextCorner.vertex) >= positions.size()) {
            return {};
        }

        const Vec3 current = positions[currentCorner.vertex];
        const Vec3 next = positions[nextCorner.vertex];
        normal.x += static_cast<double>(current.y - next.y) * static_cast<double>(current.z + next.z);
        normal.y += static_cast<double>(current.z - next.z) * static_cast<double>(current.x + next.x);
        normal.z += static_cast<double>(current.x - next.x) * static_cast<double>(current.y + next.y);
    }
    return normal;
}

[[nodiscard]] ProjectionAxis dominantProjection(const Normal3 normal) noexcept {
    const double x = std::abs(normal.x);
    const double y = std::abs(normal.y);
    const double z = std::abs(normal.z);
    if (x >= y && x >= z) {
        return ProjectionAxis::DropX;
    }
    if (y >= z) {
        return ProjectionAxis::DropY;
    }
    return ProjectionAxis::DropZ;
}

[[nodiscard]] Point2 project(const Vec3 value, const ProjectionAxis axis) noexcept {
    switch (axis) {
    case ProjectionAxis::DropX:
        return {static_cast<double>(value.y), static_cast<double>(value.z)};
    case ProjectionAxis::DropY:
        return {static_cast<double>(value.x), static_cast<double>(value.z)};
    case ProjectionAxis::DropZ:
        return {static_cast<double>(value.x), static_cast<double>(value.y)};
    }
    return {};
}

[[nodiscard]] double cross(const Point2 a, const Point2 b, const Point2 c) noexcept {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

[[nodiscard]] double signedAreaTwice(const std::vector<Point2>& points) noexcept {
    double area = 0.0;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const Point2 current = points[index];
        const Point2 next = points[(index + 1U) % points.size()];
        area += current.x * next.y - current.y * next.x;
    }
    return area;
}

[[nodiscard]] bool pointInsideOrOnTriangle(
    const Point2 point,
    const Point2 a,
    const Point2 b,
    const Point2 c,
    const double orientation) noexcept {
    return orientation * cross(a, b, point) >= 0.0 &&
           orientation * cross(b, c, point) >= 0.0 &&
           orientation * cross(c, a, point) >= 0.0;
}

[[nodiscard]] bool triangulateFace(
    const std::vector<Index>& cycle,
    const std::vector<EvaluatedCorner>& corners,
    const std::vector<Vec3>& positions,
    const std::size_t sourceFaceIndex,
    std::vector<TriangleRecord>& output) {
    if (cycle.size() < 3U) {
        return false;
    }

    if (cycle.size() == 3U) {
        output.push_back(TriangleRecord{{cycle[0], cycle[1], cycle[2]}, sourceFaceIndex});
        return true;
    }

    const Normal3 normal = newellNormal(cycle, corners, positions);
    if (normal.x == 0.0 && normal.y == 0.0 && normal.z == 0.0) {
        return false;
    }

    const ProjectionAxis projection = dominantProjection(normal);
    std::vector<Point2> points;
    points.reserve(cycle.size());
    for (const Index cornerIndex : cycle) {
        const EvaluatedCorner& corner = corners[cornerIndex];
        if (static_cast<std::size_t>(corner.vertex) >= positions.size()) {
            return false;
        }
        points.push_back(project(positions[corner.vertex], projection));
    }

    const double areaTwice = signedAreaTwice(points);
    if (areaTwice == 0.0 || !std::isfinite(areaTwice)) {
        return false;
    }
    const double orientation = areaTwice > 0.0 ? 1.0 : -1.0;

    std::vector<std::size_t> remaining(cycle.size());
    std::iota(remaining.begin(), remaining.end(), std::size_t{0});

    while (remaining.size() > 3U) {
        bool clipped = false;
        for (std::size_t slot = 0; slot < remaining.size(); ++slot) {
            const std::size_t previousSlot = (slot + remaining.size() - 1U) % remaining.size();
            const std::size_t nextSlot = (slot + 1U) % remaining.size();
            const std::size_t previous = remaining[previousSlot];
            const std::size_t current = remaining[slot];
            const std::size_t next = remaining[nextSlot];

            if (orientation * cross(points[previous], points[current], points[next]) <= 0.0) {
                continue;
            }

            bool containsOtherVertex = false;
            for (const std::size_t candidate : remaining) {
                if (candidate == previous || candidate == current || candidate == next) {
                    continue;
                }
                if (pointInsideOrOnTriangle(
                        points[candidate], points[previous], points[current], points[next], orientation)) {
                    containsOtherVertex = true;
                    break;
                }
            }
            if (containsOtherVertex) {
                continue;
            }

            output.push_back(TriangleRecord{
                {cycle[previous], cycle[current], cycle[next]},
                sourceFaceIndex});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(slot));
            clipped = true;
            break;
        }

        if (!clipped) {
            return false;
        }
    }

    output.push_back(TriangleRecord{
        {cycle[remaining[0]], cycle[remaining[1]], cycle[remaining[2]]},
        sourceFaceIndex});
    return true;
}

[[nodiscard]] std::uint64_t edgeKey(const Index vertexA, const Index vertexB) noexcept {
    const Index low = std::min(vertexA, vertexB);
    const Index high = std::max(vertexA, vertexB);
    return (static_cast<std::uint64_t>(low) << 32U) | static_cast<std::uint64_t>(high);
}

[[nodiscard]] bool rebuildRadialRings(
    const std::size_t edgeCount,
    std::vector<EvaluatedCorner>& corners) {
    std::vector<std::vector<Index>> uses(edgeCount);
    for (std::size_t index = 0; index < corners.size(); ++index) {
        const EvaluatedCorner& corner = corners[index];
        if (static_cast<std::size_t>(corner.edge) >= edgeCount) {
            return false;
        }
        uses[corner.edge].push_back(static_cast<Index>(index));
    }

    for (const auto& edgeUses : uses) {
        if (edgeUses.empty()) {
            continue;
        }
        for (std::size_t index = 0; index < edgeUses.size(); ++index) {
            EvaluatedCorner& corner = corners[edgeUses[index]];
            corner.radialNext = edgeUses[(index + 1U) % edgeUses.size()];
            corner.radialPrev = edgeUses[(index + edgeUses.size() - 1U) % edgeUses.size()];
        }
    }
    return true;
}

[[nodiscard]] bool addCountWithinGeneratedLimit(std::size_t& total, const std::size_t amount) noexcept {
    constexpr std::size_t maximum = static_cast<std::size_t>(std::numeric_limits<Index>::max());
    if (amount > maximum - total) {
        return false;
    }
    total += amount;
    return true;
}

[[nodiscard]] bool getOrCreateEdge(
    const Index vertexA,
    const Index vertexB,
    std::vector<EvaluatedEdge>& edges,
    AttributeSet& attributes,
    std::unordered_map<std::uint64_t, Index>& lookup,
    Index& result) {
    if (vertexA == vertexB) {
        return false;
    }

    const std::uint64_t key = edgeKey(vertexA, vertexB);
    const auto existing = lookup.find(key);
    if (existing != lookup.end()) {
        result = existing->second;
        return true;
    }

    constexpr std::size_t maximum = static_cast<std::size_t>(std::numeric_limits<Index>::max());
    if (edges.size() >= maximum) {
        return false;
    }

    result = static_cast<Index>(edges.size());
    edges.push_back(EvaluatedEdge{vertexA, vertexB, {}});
    AttributeRow defaultRow;
    defaultRow.domain = AttributeDomain::Edge;
    if (!attributes.appendDomainRow(AttributeDomain::Edge, defaultRow)) {
        return false;
    }
    lookup.emplace(key, result);
    return true;
}

} // namespace

std::uint64_t TriangulateModifier::revisionToken() const noexcept {
    return 1ULL;
}

ModifierApplyResult TriangulateModifier::apply(EvaluatedMesh& mesh) const {
    AttributeSet& attributes = mutableAttributes(mesh);
    const std::size_t sourceVertexCount = mesh.vertexCount();
    const std::size_t sourceEdgeCount = mesh.edgeCount();
    const std::size_t sourceFaceCount = mesh.faceCount();
    const std::size_t sourceCornerCount = mesh.cornerCount();

    const auto* positionValues = attributes.values<Vec3>("position", AttributeDomain::Vertex);
    if (positionValues == nullptr || positionValues->size() != sourceVertexCount) {
        return {ModifierApplyError::MissingPositionAttribute};
    }
    if (!attributes.validateSizes() || attributes.domainSize(AttributeDomain::Vertex) != sourceVertexCount ||
        attributes.domainSize(AttributeDomain::Edge) != sourceEdgeCount ||
        attributes.domainSize(AttributeDomain::Face) != sourceFaceCount ||
        attributes.domainSize(AttributeDomain::Corner) != sourceCornerCount) {
        return {ModifierApplyError::AttributeCopyFailed};
    }

    const std::vector<Vec3> positions(positionValues->begin(), positionValues->end());
    const std::vector<EvaluatedFace> sourceFaces(mesh.faces().begin(), mesh.faces().end());
    const std::vector<EvaluatedCorner> sourceCorners(mesh.corners().begin(), mesh.corners().end());

    std::vector<TriangleRecord> triangles;
    std::vector<Index> cycle;
    std::size_t generatedFaceCount = 0U;
    std::size_t generatedCornerCount = 0U;
    std::size_t maximumNewEdges = 0U;

    for (std::size_t faceIndex = 0; faceIndex < sourceFaces.size(); ++faceIndex) {
        const EvaluatedFace& face = sourceFaces[faceIndex];
        if (!collectFaceCycle(face, sourceCorners, cycle)) {
            return {ModifierApplyError::GeneratedTopologyInvalid};
        }

        const std::size_t triangleCount = cycle.size() - 2U;
        constexpr std::size_t maximum = static_cast<std::size_t>(std::numeric_limits<Index>::max());
        if (triangleCount > maximum / 3U ||
            !addCountWithinGeneratedLimit(generatedFaceCount, triangleCount) ||
            !addCountWithinGeneratedLimit(generatedCornerCount, triangleCount * 3U) ||
            !addCountWithinGeneratedLimit(maximumNewEdges, cycle.size() > 3U ? cycle.size() - 3U : 0U)) {
            return {ModifierApplyError::GeneratedTopologyOverflow};
        }

        if (!triangulateFace(cycle, sourceCorners, positions, faceIndex, triangles)) {
            return {ModifierApplyError::TriangulationFailed};
        }
    }

    std::size_t maximumEdgeCount = sourceEdgeCount;
    if (!addCountWithinGeneratedLimit(maximumEdgeCount, maximumNewEdges)) {
        return {ModifierApplyError::GeneratedTopologyOverflow};
    }

    std::vector<EvaluatedEdge>& edges = mutableEdges(mesh);
    std::unordered_map<std::uint64_t, Index> edgeLookup;
    edgeLookup.reserve(maximumEdgeCount);
    for (std::size_t index = 0; index < edges.size(); ++index) {
        const EvaluatedEdge& edge = edges[index];
        if (edge.vertexA == edge.vertexB || static_cast<std::size_t>(edge.vertexA) >= sourceVertexCount ||
            static_cast<std::size_t>(edge.vertexB) >= sourceVertexCount) {
            return {ModifierApplyError::GeneratedTopologyInvalid};
        }
        const auto [iterator, inserted] = edgeLookup.emplace(edgeKey(edge.vertexA, edge.vertexB), static_cast<Index>(index));
        (void)iterator;
        if (!inserted) {
            return {ModifierApplyError::GeneratedTopologyInvalid};
        }
    }

    edges.reserve(maximumEdgeCount);
    std::vector<EvaluatedFace> generatedFaces;
    std::vector<EvaluatedCorner> generatedCorners;
    std::vector<std::size_t> faceAttributeSources;
    std::vector<std::size_t> cornerAttributeSources;
    generatedFaces.reserve(generatedFaceCount);
    generatedCorners.reserve(generatedCornerCount);
    faceAttributeSources.reserve(generatedFaceCount);
    cornerAttributeSources.reserve(generatedCornerCount);

    for (const TriangleRecord& triangle : triangles) {
        if (triangle.sourceFaceIndex >= sourceFaces.size()) {
            return {ModifierApplyError::GeneratedTopologyInvalid};
        }

        const Index firstCorner = static_cast<Index>(generatedCorners.size());
        generatedFaces.push_back(EvaluatedFace{firstCorner, 3U, sourceFaces[triangle.sourceFaceIndex].sourceId});
        faceAttributeSources.push_back(triangle.sourceFaceIndex);

        for (std::size_t local = 0; local < 3U; ++local) {
            const Index sourceCornerIndex = triangle.corners[local];
            const Index nextSourceCornerIndex = triangle.corners[(local + 1U) % 3U];
            if (static_cast<std::size_t>(sourceCornerIndex) >= sourceCorners.size() ||
                static_cast<std::size_t>(nextSourceCornerIndex) >= sourceCorners.size()) {
                return {ModifierApplyError::GeneratedTopologyInvalid};
            }

            const EvaluatedCorner& sourceCorner = sourceCorners[sourceCornerIndex];
            const EvaluatedCorner& nextSourceCorner = sourceCorners[nextSourceCornerIndex];
            if (static_cast<std::size_t>(sourceCorner.vertex) >= sourceVertexCount ||
                static_cast<std::size_t>(nextSourceCorner.vertex) >= sourceVertexCount) {
                return {ModifierApplyError::GeneratedTopologyInvalid};
            }

            Index edge = 0U;
            if (!getOrCreateEdge(sourceCorner.vertex, nextSourceCorner.vertex, edges, attributes, edgeLookup, edge)) {
                return {ModifierApplyError::GeneratedTopologyOverflow};
            }

            const Index destination = static_cast<Index>(generatedCorners.size());
            const Index next = static_cast<Index>(firstCorner + static_cast<Index>((local + 1U) % 3U));
            const Index prev = static_cast<Index>(firstCorner + static_cast<Index>((local + 2U) % 3U));
            generatedCorners.push_back(EvaluatedCorner{
                sourceCorner.vertex,
                edge,
                next,
                prev,
                destination,
                destination,
                sourceCorner.sourceId});
            cornerAttributeSources.push_back(static_cast<std::size_t>(sourceCornerIndex));
        }
    }

    if (!rebuildRadialRings(edges.size(), generatedCorners)) {
        return {ModifierApplyError::GeneratedTopologyInvalid};
    }

    if (!attributes.remapDomain(AttributeDomain::Face, faceAttributeSources) ||
        !attributes.remapDomain(AttributeDomain::Corner, cornerAttributeSources)) {
        return {ModifierApplyError::AttributeCopyFailed};
    }

    std::vector<EvaluatedFace>& faces = mutableFaces(mesh);
    std::vector<EvaluatedCorner>& corners = mutableCorners(mesh);
    faces = std::move(generatedFaces);
    corners = std::move(generatedCorners);

    if (!attributes.validateSizes() || attributes.domainSize(AttributeDomain::Vertex) != mesh.vertexCount() ||
        attributes.domainSize(AttributeDomain::Edge) != mesh.edgeCount() ||
        attributes.domainSize(AttributeDomain::Face) != mesh.faceCount() ||
        attributes.domainSize(AttributeDomain::Corner) != mesh.cornerCount()) {
        return {ModifierApplyError::AttributeCopyFailed};
    }

    return {};
}

} // namespace vortex
