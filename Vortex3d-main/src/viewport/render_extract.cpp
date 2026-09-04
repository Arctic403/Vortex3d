#include "vortex/viewport/render_extract.hpp"

#include "vortex/eval/modifier.hpp"
#include "vortex/eval/validator.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>

namespace vortex {
namespace {

[[nodiscard]] bool finite(const Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

struct RenderVertexKey final {
    std::uint32_t evaluatedVertex = 0;
    std::uint32_t normalX = 0;
    std::uint32_t normalY = 0;
    std::uint32_t normalZ = 0;

    [[nodiscard]] bool operator==(const RenderVertexKey&) const noexcept = default;
};

struct RenderVertexKeyHash final {
    [[nodiscard]] std::size_t operator()(const RenderVertexKey& key) const noexcept {
        std::size_t hash = key.evaluatedVertex;
        const auto mix = [&hash](const std::uint32_t value) {
            hash ^= static_cast<std::size_t>(value) + std::size_t{0x9e3779b9U} + (hash << 6U) + (hash >> 2U);
        };
        mix(key.normalX);
        mix(key.normalY);
        mix(key.normalZ);
        return hash;
    }
};

[[nodiscard]] RenderVertexKey makeRenderVertexKey(const EvaluatedCorner& corner, const Vec3 normal) noexcept {
    return RenderVertexKey{
        corner.vertex,
        std::bit_cast<std::uint32_t>(normal.x),
        std::bit_cast<std::uint32_t>(normal.y),
        std::bit_cast<std::uint32_t>(normal.z)};
}

} // namespace

RenderExtractResult RenderExtractor::extract(const EvaluatedMesh& source) {
    if (!EvaluatedMeshValidator::validate(source)) {
        return {.mesh = std::nullopt, .error = RenderExtractError::InvalidEvaluatedMesh};
    }

    EvaluatedMesh triangulated = source;
    TriangulateModifier triangulate;
    if (!triangulate.apply(triangulated)) {
        return {.mesh = std::nullopt, .error = RenderExtractError::TriangulationFailed};
    }
    if (!EvaluatedMeshValidator::validate(triangulated)) {
        return {.mesh = std::nullopt, .error = RenderExtractError::InvalidEvaluatedMesh};
    }

    const auto* normals = triangulated.attributes().values<Vec3>("normal", AttributeDomain::Corner);
    if (normals == nullptr || normals->size() != triangulated.cornerCount()) {
        return {.mesh = std::nullopt, .error = RenderExtractError::MissingNormal};
    }

    ViewportMesh output;
    output.sourceDocumentRuntimeId = source.sourceDocumentRuntimeId();
    output.sourceMeshId = source.sourceMeshId();
    output.vertices.reserve(triangulated.vertexCount());
    output.triangles.reserve(triangulated.faceCount());

    std::unordered_map<RenderVertexKey, std::uint32_t, RenderVertexKeyHash> renderVertexIndices;
    renderVertexIndices.reserve(triangulated.cornerCount());

    RenderExtractError vertexError = RenderExtractError::None;
    const auto renderVertexForCorner = [&](const EvaluatedMesh::Index cornerIndex) -> std::optional<std::uint32_t> {
        if (cornerIndex >= triangulated.cornerCount()) {
            vertexError = RenderExtractError::InvalidCornerCycle;
            return std::nullopt;
        }

        const EvaluatedCorner& corner = triangulated.corners()[cornerIndex];
        if (corner.vertex >= triangulated.vertexCount()) {
            vertexError = RenderExtractError::InvalidCornerCycle;
            return std::nullopt;
        }

        const Vec3 normal = (*normals)[cornerIndex];
        if (!finite(normal)) {
            vertexError = RenderExtractError::InvalidNormal;
            return std::nullopt;
        }

        const RenderVertexKey key = makeRenderVertexKey(corner, normal);
        if (const auto existing = renderVertexIndices.find(key); existing != renderVertexIndices.end()) {
            return existing->second;
        }

        const auto position = triangulated.position(corner.vertex);
        if (!position) {
            vertexError = RenderExtractError::MissingPosition;
            return std::nullopt;
        }
        if (output.vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            vertexError = RenderExtractError::InvalidEvaluatedMesh;
            return std::nullopt;
        }

        const std::uint32_t renderIndex = static_cast<std::uint32_t>(output.vertices.size());
        output.vertices.push_back({*position, normal, triangulated.vertices()[corner.vertex].sourceId});
        renderVertexIndices.emplace(key, renderIndex);
        return renderIndex;
    };

    for (const EvaluatedFace& face : triangulated.faces()) {
        if (face.cornerCount != 3U || face.firstCorner >= triangulated.cornerCount()) {
            return {.mesh = std::nullopt, .error = RenderExtractError::InvalidCornerCycle};
        }

        const EvaluatedMesh::Index aCorner = face.firstCorner;
        const EvaluatedMesh::Index bCorner = triangulated.corners()[aCorner].next;
        if (bCorner >= triangulated.cornerCount()) {
            return {.mesh = std::nullopt, .error = RenderExtractError::InvalidCornerCycle};
        }
        const EvaluatedMesh::Index cCorner = triangulated.corners()[bCorner].next;
        if (cCorner >= triangulated.cornerCount() || triangulated.corners()[cCorner].next != aCorner) {
            return {.mesh = std::nullopt, .error = RenderExtractError::InvalidCornerCycle};
        }

        const auto a = renderVertexForCorner(aCorner);
        const auto b = renderVertexForCorner(bCorner);
        const auto c = renderVertexForCorner(cCorner);
        if (!a || !b || !c) {
            return {.mesh = std::nullopt, .error = vertexError};
        }
        output.triangles.push_back({*a, *b, *c, face.sourceId});
    }

    return {.mesh = std::move(output), .error = RenderExtractError::None};
}

} // namespace vortex
