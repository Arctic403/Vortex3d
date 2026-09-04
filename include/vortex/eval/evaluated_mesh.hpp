#pragma once

#include "vortex/core/id.hpp"
#include "vortex/mesh/attribute.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace vortex {

struct EvaluatedVertex final {
    VertexId sourceId;
};

struct EvaluatedEdge final {
    std::uint32_t vertexA = 0;
    std::uint32_t vertexB = 0;
    EdgeId sourceId;
};

struct EvaluatedFace final {
    std::uint32_t firstCorner = 0;
    std::uint32_t cornerCount = 0;
    FaceId sourceId;
};

struct EvaluatedCorner final {
    std::uint32_t vertex = 0;
    std::uint32_t edge = 0;
    std::uint32_t next = 0;
    std::uint32_t prev = 0;
    std::uint32_t radialNext = 0;
    std::uint32_t radialPrev = 0;
    CornerId sourceId;
};

class MeshEvaluator;

class EvaluatedMesh final {
public:
    using Index = std::uint32_t;

    [[nodiscard]] MeshId sourceMeshId() const noexcept { return sourceMeshId_; }
    [[nodiscard]] std::uint64_t sourceRevision() const noexcept { return sourceRevision_; }

    [[nodiscard]] std::span<const EvaluatedVertex> vertices() const noexcept { return vertices_; }
    [[nodiscard]] std::span<const EvaluatedEdge> edges() const noexcept { return edges_; }
    [[nodiscard]] std::span<const EvaluatedFace> faces() const noexcept { return faces_; }
    [[nodiscard]] std::span<const EvaluatedCorner> corners() const noexcept { return corners_; }

    [[nodiscard]] std::size_t vertexCount() const noexcept { return vertices_.size(); }
    [[nodiscard]] std::size_t edgeCount() const noexcept { return edges_.size(); }
    [[nodiscard]] std::size_t faceCount() const noexcept { return faces_.size(); }
    [[nodiscard]] std::size_t cornerCount() const noexcept { return corners_.size(); }

    [[nodiscard]] const AttributeSet& attributes() const noexcept { return attributes_; }

    [[nodiscard]] std::optional<Vec3> position(const Index index) const noexcept {
        const auto* positions = attributes_.values<Vec3>("position", AttributeDomain::Vertex);
        if (positions == nullptr || static_cast<std::size_t>(index) >= positions->size()) {
            return std::nullopt;
        }
        return (*positions)[index];
    }

private:
    friend class MeshEvaluator;

    MeshId sourceMeshId_;
    std::uint64_t sourceRevision_ = 0;
    AttributeSet attributes_;
    std::vector<EvaluatedVertex> vertices_;
    std::vector<EvaluatedEdge> edges_;
    std::vector<EvaluatedFace> faces_;
    std::vector<EvaluatedCorner> corners_;
};

} // namespace vortex
