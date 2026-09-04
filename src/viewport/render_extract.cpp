#include "vortex/viewport/render_extract.hpp"

#include "vortex/eval/modifier.hpp"
#include "vortex/eval/validator.hpp"

namespace vortex {

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

    ViewportMesh output;
    output.sourceDocumentRuntimeId = source.sourceDocumentRuntimeId();
    output.sourceMeshId = source.sourceMeshId();
    output.vertices.reserve(triangulated.vertexCount());

    const auto* normals = triangulated.attributes().values<Vec3>("normal", AttributeDomain::Corner);
    std::vector<Vec3> vertexNormals(triangulated.vertexCount(), Vec3{});
    std::vector<std::uint32_t> normalCounts(triangulated.vertexCount(), 0U);
    if (normals != nullptr && normals->size() == triangulated.cornerCount()) {
        for (std::size_t cornerIndex = 0; cornerIndex < triangulated.cornerCount(); ++cornerIndex) {
            const auto& corner = triangulated.corners()[cornerIndex];
            if (corner.vertex < vertexNormals.size()) {
                vertexNormals[corner.vertex].x += (*normals)[cornerIndex].x;
                vertexNormals[corner.vertex].y += (*normals)[cornerIndex].y;
                vertexNormals[corner.vertex].z += (*normals)[cornerIndex].z;
                ++normalCounts[corner.vertex];
            }
        }
    }

    for (std::uint32_t vertexIndex = 0; vertexIndex < triangulated.vertexCount(); ++vertexIndex) {
        const auto position = triangulated.position(vertexIndex);
        if (!position) {
            return {.mesh = std::nullopt, .error = RenderExtractError::MissingPosition};
        }
        Vec3 normal = vertexNormals[vertexIndex];
        if (normalCounts[vertexIndex] != 0U) {
            const float inv = 1.0F / static_cast<float>(normalCounts[vertexIndex]);
            normal = {normal.x * inv, normal.y * inv, normal.z * inv};
        }
        output.vertices.push_back({*position, normal, triangulated.vertices()[vertexIndex].sourceId});
    }

    output.triangles.reserve(triangulated.faceCount());
    for (const EvaluatedFace& face : triangulated.faces()) {
        if (face.cornerCount != 3U || face.firstCorner >= triangulated.cornerCount()) {
            return {.mesh = std::nullopt, .error = RenderExtractError::InvalidCornerCycle};
        }
        const auto& a = triangulated.corners()[face.firstCorner];
        if (a.next >= triangulated.cornerCount()) {
            return {.mesh = std::nullopt, .error = RenderExtractError::InvalidCornerCycle};
        }
        const auto& b = triangulated.corners()[a.next];
        if (b.next >= triangulated.cornerCount()) {
            return {.mesh = std::nullopt, .error = RenderExtractError::InvalidCornerCycle};
        }
        const auto& c = triangulated.corners()[b.next];
        output.triangles.push_back({a.vertex, b.vertex, c.vertex, face.sourceId});
    }

    return {.mesh = std::move(output), .error = RenderExtractError::None};
}

} // namespace vortex
