#include "vortex/eval/validator.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vortex {
namespace {

using Index = EvaluatedMesh::Index;

[[nodiscard]] std::uint64_t edgeKey(const Index a, const Index b) noexcept {
    const Index low = std::min(a, b);
    const Index high = std::max(a, b);
    return (static_cast<std::uint64_t>(low) << 32U) | static_cast<std::uint64_t>(high);
}

} // namespace

EvaluatedMeshValidationResult EvaluatedMeshValidator::validate(const EvaluatedMesh& mesh) {
    EvaluatedMeshValidationResult result;
    const auto issue = [&result](
                           const EvaluatedMeshValidationCode code,
                           const std::uint64_t index,
                           std::string message) {
        result.issues.push_back(EvaluatedMeshValidationIssue{code, index, std::move(message)});
    };

    if (!mesh.sourceDocumentRuntimeId() || !mesh.sourceMeshId()) {
        issue(
            EvaluatedMeshValidationCode::InvalidSourceIdentity,
            0,
            "Evaluated mesh is missing its runtime Document or source Mesh identity");
    }

    if (!mesh.attributes().validateSizes() ||
        mesh.attributes().domainSize(AttributeDomain::Vertex) != mesh.vertexCount() ||
        mesh.attributes().domainSize(AttributeDomain::Edge) != mesh.edgeCount() ||
        mesh.attributes().domainSize(AttributeDomain::Face) != mesh.faceCount() ||
        mesh.attributes().domainSize(AttributeDomain::Corner) != mesh.cornerCount()) {
        issue(
            EvaluatedMeshValidationCode::AttributeSizeMismatch,
            0,
            "Evaluated attribute domain sizes do not match generated topology domains");
    }

    constexpr std::size_t maximumCount = static_cast<std::size_t>(std::numeric_limits<Index>::max());
    if (mesh.vertexCount() > maximumCount || mesh.edgeCount() > maximumCount ||
        mesh.faceCount() > maximumCount || mesh.cornerCount() > maximumCount) {
        issue(
            EvaluatedMeshValidationCode::ElementCountOverflow,
            0,
            "Evaluated topology count exceeds the packed 32-bit index contract");
    }

    const std::span<const EvaluatedEdge> edges = mesh.edges();
    const std::span<const EvaluatedFace> faces = mesh.faces();
    const std::span<const EvaluatedCorner> corners = mesh.corners();

    std::unordered_map<std::uint64_t, std::size_t> uniqueEdges;
    uniqueEdges.reserve(edges.size());
    for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
        const EvaluatedEdge& edge = edges[edgeIndex];
        if (static_cast<std::size_t>(edge.vertexA) >= mesh.vertexCount() ||
            static_cast<std::size_t>(edge.vertexB) >= mesh.vertexCount() ||
            edge.vertexA == edge.vertexB) {
            issue(
                EvaluatedMeshValidationCode::InvalidEdgeEndpoints,
                edgeIndex,
                "Evaluated edge endpoints are out of range or identical");
            continue;
        }

        const auto [existing, inserted] = uniqueEdges.emplace(edgeKey(edge.vertexA, edge.vertexB), edgeIndex);
        if (!inserted && existing->second != edgeIndex) {
            issue(
                EvaluatedMeshValidationCode::DuplicateEdge,
                edgeIndex,
                "Multiple evaluated edges connect the same unordered packed vertex pair");
        }
    }

    std::vector<std::vector<Index>> edgeUses(edges.size());
    for (std::size_t cornerIndex = 0; cornerIndex < corners.size(); ++cornerIndex) {
        const EvaluatedCorner& corner = corners[cornerIndex];
        const bool validVertex = static_cast<std::size_t>(corner.vertex) < mesh.vertexCount();
        const bool validEdge = static_cast<std::size_t>(corner.edge) < edges.size();
        const bool validNext = static_cast<std::size_t>(corner.next) < corners.size();
        const bool validPrev = static_cast<std::size_t>(corner.prev) < corners.size();
        const bool validRadialNext = static_cast<std::size_t>(corner.radialNext) < corners.size();
        const bool validRadialPrev = static_cast<std::size_t>(corner.radialPrev) < corners.size();
        if (!validVertex || !validEdge || !validNext || !validPrev || !validRadialNext || !validRadialPrev) {
            issue(
                EvaluatedMeshValidationCode::InvalidCornerReference,
                cornerIndex,
                "Evaluated corner references a packed element outside its domain");
        }
        if (validEdge) {
            edgeUses[corner.edge].push_back(static_cast<Index>(cornerIndex));
        }
    }

    constexpr std::size_t noOwner = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> cornerOwner(corners.size(), noOwner);
    for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
        const EvaluatedFace& face = faces[faceIndex];
        if (face.cornerCount < 3U) {
            issue(
                EvaluatedMeshValidationCode::InvalidFaceSize,
                faceIndex,
                "Evaluated face has fewer than three corners");
            continue;
        }
        if (static_cast<std::size_t>(face.firstCorner) >= corners.size()) {
            issue(
                EvaluatedMeshValidationCode::BrokenFaceCycle,
                faceIndex,
                "Evaluated face firstCorner is out of range");
            continue;
        }

        std::unordered_set<Index> visited;
        visited.reserve(face.cornerCount);
        Index cursor = face.firstCorner;
        bool broken = false;
        for (std::uint32_t step = 0; step < face.cornerCount; ++step) {
            if (static_cast<std::size_t>(cursor) >= corners.size()) {
                issue(
                    EvaluatedMeshValidationCode::BrokenFaceCycle,
                    faceIndex,
                    "Evaluated face cycle stepped outside the corner domain");
                broken = true;
                break;
            }
            if (!visited.insert(cursor).second) {
                issue(
                    EvaluatedMeshValidationCode::BrokenFaceCycle,
                    faceIndex,
                    "Evaluated face cycle repeats a corner before cornerCount distinct corners are visited");
                broken = true;
                break;
            }

            if (cornerOwner[cursor] == noOwner) {
                cornerOwner[cursor] = faceIndex;
            } else if (cornerOwner[cursor] != faceIndex) {
                issue(
                    EvaluatedMeshValidationCode::CornerUsedByMultipleFaces,
                    cursor,
                    "Evaluated corner is reachable from more than one face cycle");
            }

            const EvaluatedCorner& current = corners[cursor];
            if (static_cast<std::size_t>(current.next) >= corners.size() ||
                static_cast<std::size_t>(current.prev) >= corners.size()) {
                issue(
                    EvaluatedMeshValidationCode::BrokenFaceCycle,
                    faceIndex,
                    "Evaluated face next/prev link is out of range");
                broken = true;
                break;
            }

            const EvaluatedCorner& next = corners[current.next];
            const EvaluatedCorner& prev = corners[current.prev];
            if (next.prev != cursor || prev.next != cursor) {
                issue(
                    EvaluatedMeshValidationCode::BrokenFaceCycle,
                    faceIndex,
                    "Evaluated face next/prev links are not mutually consistent");
                broken = true;
                break;
            }

            if (static_cast<std::size_t>(current.vertex) < mesh.vertexCount() &&
                static_cast<std::size_t>(next.vertex) < mesh.vertexCount() &&
                static_cast<std::size_t>(current.edge) < edges.size()) {
                const EvaluatedEdge& edge = edges[current.edge];
                const bool startsOnEdge = edge.vertexA == current.vertex || edge.vertexB == current.vertex;
                const bool endsOnEdge = edge.vertexA == next.vertex || edge.vertexB == next.vertex;
                if (!startsOnEdge || !endsOnEdge || current.vertex == next.vertex) {
                    issue(
                        EvaluatedMeshValidationCode::CornerEdgeMismatch,
                        cursor,
                        "Evaluated corner edge does not connect this corner to the next face corner");
                }
            }

            cursor = current.next;
        }

        if (!broken && (cursor != face.firstCorner || visited.size() != face.cornerCount)) {
            issue(
                EvaluatedMeshValidationCode::BrokenFaceCycle,
                faceIndex,
                "Evaluated face cycle did not close after exactly cornerCount distinct steps");
        }
    }

    for (std::size_t edgeIndex = 0; edgeIndex < edgeUses.size(); ++edgeIndex) {
        const std::vector<Index>& uses = edgeUses[edgeIndex];
        if (uses.empty()) {
            continue;
        }

        std::unordered_set<Index> visited;
        visited.reserve(uses.size());
        const Index start = uses.front();
        Index cursor = start;
        bool broken = false;
        for (std::size_t step = 0; step < uses.size(); ++step) {
            if (static_cast<std::size_t>(cursor) >= corners.size()) {
                broken = true;
                break;
            }
            if (!visited.insert(cursor).second) {
                broken = true;
                break;
            }

            const EvaluatedCorner& current = corners[cursor];
            if (static_cast<std::size_t>(current.edge) != edgeIndex ||
                static_cast<std::size_t>(current.radialNext) >= corners.size() ||
                static_cast<std::size_t>(current.radialPrev) >= corners.size()) {
                broken = true;
                break;
            }

            const EvaluatedCorner& next = corners[current.radialNext];
            const EvaluatedCorner& prev = corners[current.radialPrev];
            if (next.radialPrev != cursor || prev.radialNext != cursor ||
                static_cast<std::size_t>(next.edge) != edgeIndex ||
                static_cast<std::size_t>(prev.edge) != edgeIndex) {
                broken = true;
                break;
            }
            cursor = current.radialNext;
        }

        if (broken || cursor != start || visited.size() != uses.size()) {
            issue(
                EvaluatedMeshValidationCode::BrokenRadialCycle,
                edgeIndex,
                "Evaluated radial ring is invalid or does not cover every corner using the edge");
        }
    }

    for (std::size_t cornerIndex = 0; cornerIndex < cornerOwner.size(); ++cornerIndex) {
        if (cornerOwner[cornerIndex] == noOwner) {
            issue(
                EvaluatedMeshValidationCode::UnreachableCorner,
                cornerIndex,
                "Evaluated corner is not reachable from any face cycle");
        }
    }

    return result;
}

} // namespace vortex
