#pragma once

#include "vortex/eval/evaluated_mesh.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace vortex {

struct ViewportVertex final {
    Vec3 position{};
    Vec3 normal{};
    VertexId sourceVertex;
};

struct ViewportTriangle final {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
    FaceId sourceFace;
};

struct ViewportMesh final {
    RuntimeDocumentId sourceDocumentRuntimeId;
    MeshId sourceMeshId;
    std::vector<ViewportVertex> vertices;
    std::vector<ViewportTriangle> triangles;
};

enum class RenderExtractError : std::uint8_t {
    None,
    InvalidEvaluatedMesh,
    TriangulationFailed,
    MissingPosition,
    InvalidCornerCycle,
};

struct RenderExtractResult final {
    std::optional<ViewportMesh> mesh;
    RenderExtractError error = RenderExtractError::None;
    [[nodiscard]] bool ok() const noexcept { return mesh.has_value() && error == RenderExtractError::None; }
    explicit operator bool() const noexcept { return ok(); }
};

class RenderExtractor final {
public:
    [[nodiscard]] static RenderExtractResult extract(const EvaluatedMesh& source);
};

} // namespace vortex
