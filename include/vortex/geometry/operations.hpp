#pragma once

#include "vortex/mesh/editable_mesh.hpp"

#include <optional>
#include <span>
#include <vector>

namespace vortex {

struct FaceTopologyView final {
    FaceId faceId;
    std::vector<CornerId> corners;
    std::vector<VertexId> vertices;
    std::vector<EdgeId> edges;
};

struct InsetFaceResult final {
    FaceId sourceFace;
    FaceId innerFace;
    std::vector<FaceId> rimFaces;
    std::vector<VertexId> innerVertices;
};

class GeometryOperations final {
public:
    [[nodiscard]] static std::optional<FaceTopologyView> faceTopology(const EditableMesh& mesh, FaceId faceId);
    [[nodiscard]] static std::vector<EdgeId> incidentEdges(const EditableMesh& mesh, VertexId vertexId);
    [[nodiscard]] static std::vector<FaceId> incidentFaces(const EditableMesh& mesh, VertexId vertexId);
    [[nodiscard]] static std::vector<VertexId> neighboringVertices(const EditableMesh& mesh, VertexId vertexId);
    [[nodiscard]] static std::optional<Vec3> faceCentroid(const EditableMesh& mesh, FaceId faceId);
    [[nodiscard]] static bool translateVertices(EditableMesh& mesh, std::span<const VertexId> vertices, Vec3 offset);
    [[nodiscard]] static std::optional<EdgeSplitResult> splitEdge(EditableMesh& mesh, EdgeId edgeId, float factor = 0.5F);
    [[nodiscard]] static std::optional<FaceExtrudeResult> extrudeFace(EditableMesh& mesh, FaceId faceId, Vec3 offset);
    [[nodiscard]] static std::optional<InsetFaceResult> insetFace(EditableMesh& mesh, FaceId faceId, float factor);
};

} // namespace vortex
