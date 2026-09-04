#pragma once

#include "vortex/core/id.hpp"
#include "vortex/mesh/attribute.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace vortex {

struct EvaluationCacheKey final {
    MeshId sourceMeshId;
    std::uint64_t sourceRevision = 0;
    std::uint64_t modifierStackRevision = 0;

    [[nodiscard]] bool operator==(const EvaluationCacheKey&) const noexcept = default;
};

struct EvaluationCacheKeyHash final {
    [[nodiscard]] std::size_t operator()(const EvaluationCacheKey& key) const noexcept {
        std::size_t hash = IdHash<MeshId>{}(key.sourceMeshId);
        const auto mix = [&hash](const std::uint64_t value) {
            const std::size_t folded = static_cast<std::size_t>(value ^ (value >> 32U));
            hash ^= folded + std::size_t{0x9e3779b9U} + (hash << 6U) + (hash >> 2U);
        };
        mix(key.sourceRevision);
        mix(key.modifierStackRevision);
        return hash;
    }
};

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

class DerivedNormalsGenerator;
class MeshEvaluator;
class MeshModifier;

class EvaluatedMesh final {
public:
    using Index = std::uint32_t;

    [[nodiscard]] MeshId sourceMeshId() const noexcept { return cacheKey_.sourceMeshId; }
    [[nodiscard]] std::uint64_t sourceRevision() const noexcept { return cacheKey_.sourceRevision; }
    [[nodiscard]] std::uint64_t modifierStackRevision() const noexcept { return cacheKey_.modifierStackRevision; }
    [[nodiscard]] const EvaluationCacheKey& cacheKey() const noexcept { return cacheKey_; }

    [[nodiscard]] std::span<const EvaluatedVertex> vertices() const noexcept { return vertices_; }
    [[nodiscard]] std::span<const EvaluatedEdge> edges() const noexcept { return edges_; }
    [[nodiscard]] std::span<const EvaluatedFace> faces() const noexcept { return faces_; }
    [[nodiscard]] std::span<const EvaluatedCorner> corners() const noexcept { return corners_; }

    [[nodiscard]] std::size_t vertexCount() const noexcept { return vertices_.size(); }
    [[nodiscard]] std::size_t edgeCount() const noexcept { return edges_.size(); }
    [[nodiscard]] std::size_t faceCount() const noexcept { return faces_.size(); }
    [[nodiscard]] std::size_t cornerCount() const noexcept { return corners_.size(); }

    [[nodiscard]] const AttributeSet& attributes() const noexcept { return attributes_; }

    // Conservative retained-memory estimate for cache budgeting. It includes this value,
    // vector capacities, and AttributeSet's dynamic-memory estimate. It is intentionally
    // allocator-agnostic rather than pretending to be an exact heap measurement.
    [[nodiscard]] std::size_t estimatedRetainedBytes() const noexcept {
        constexpr std::size_t maximum = std::numeric_limits<std::size_t>::max();
        std::size_t bytes = sizeof(EvaluatedMesh);

        const auto addBytes = [&bytes](const std::size_t amount) noexcept {
            constexpr std::size_t maxValue = std::numeric_limits<std::size_t>::max();
            if (amount > maxValue - bytes) {
                bytes = maxValue;
                return;
            }
            bytes += amount;
        };
        const auto addCapacity = [&addBytes](const std::size_t capacity, const std::size_t elementBytes) noexcept {
            constexpr std::size_t maxValue = std::numeric_limits<std::size_t>::max();
            if (elementBytes != 0U && capacity > maxValue / elementBytes) {
                addBytes(maxValue);
                return;
            }
            addBytes(capacity * elementBytes);
        };

        addBytes(attributes_.estimatedDynamicBytes());
        if (bytes == maximum) {
            return maximum;
        }
        addCapacity(vertices_.capacity(), sizeof(EvaluatedVertex));
        addCapacity(edges_.capacity(), sizeof(EvaluatedEdge));
        addCapacity(faces_.capacity(), sizeof(EvaluatedFace));
        addCapacity(corners_.capacity(), sizeof(EvaluatedCorner));
        return bytes;
    }

    [[nodiscard]] std::optional<Vec3> position(const Index index) const noexcept {
        const auto* positions = attributes_.values<Vec3>("position", AttributeDomain::Vertex);
        if (positions == nullptr || static_cast<std::size_t>(index) >= positions->size()) {
            return std::nullopt;
        }
        return (*positions)[index];
    }

private:
    friend class DerivedNormalsGenerator;
    friend class MeshEvaluator;
    friend class MeshModifier;

    EvaluationCacheKey cacheKey_;
    AttributeSet attributes_;
    std::vector<EvaluatedVertex> vertices_;
    std::vector<EvaluatedEdge> edges_;
    std::vector<EvaluatedFace> faces_;
    std::vector<EvaluatedCorner> corners_;
};

} // namespace vortex
