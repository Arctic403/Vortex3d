#pragma once

#include "vortex/core/id.hpp"
#include "vortex/mesh/attribute.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace vortex {

class ExtrudeFaceCommand;
class MeshHistory;

struct MeshVertex final {
    VertexId id;
};

struct MeshEdge final {
    EdgeId id;
    VertexId vertexA;
    VertexId vertexB;
    CornerId anyCorner;
};

struct MeshFace final {
    FaceId id;
    CornerId firstCorner;
    std::uint32_t cornerCount = 0;
};

struct MeshCorner final {
    CornerId id;
    FaceId faceId;
    VertexId vertexId;
    EdgeId edgeId;
    CornerId next;
    CornerId prev;
    CornerId radialNext;
    CornerId radialPrev;
};

struct EdgeSplitResult final {
    VertexId newVertex;
    EdgeId retainedEdge;
    EdgeId newEdge;
    std::uint32_t insertedCornerCount = 0;
};

struct FaceExtrudeResult final {
    FaceId sourceFace;
    FaceId capFace;
    std::vector<FaceId> sideFaces;
    std::vector<VertexId> newVertices;
};

enum class MeshValidationCode : std::uint8_t {
    MissingElement,
    InvalidFaceSize,
    BrokenFaceCycle,
    BrokenRadialCycle,
    InvalidEdgeEndpoints,
    CornerEdgeMismatch,
    AttributeSizeMismatch,
    UnreachableCorner,
    StorageSizeMismatch,
    DuplicateElementId,
    IndexMapMismatch,
    ElementIdentityMismatch,
    InvalidAllocatorState,
    DuplicateEdge,
};

struct MeshValidationIssue final {
    MeshValidationCode code = MeshValidationCode::MissingElement;
    std::uint64_t entityId = 0;
    std::string message;
};

struct MeshValidationResult final {
    std::vector<MeshValidationIssue> issues;
    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
    explicit operator bool() const noexcept { return ok(); }
};

class EditableMesh final {
public:
    EditableMesh();

    [[nodiscard]] VertexId addVertex(Vec3 position);
    [[nodiscard]] EdgeId addEdge(VertexId vertexA, VertexId vertexB);
    [[nodiscard]] FaceId addFace(const std::vector<VertexId>& vertices);

    [[nodiscard]] bool removeFace(FaceId id, bool removeUnusedEdges = true);
    [[nodiscard]] bool removeEdge(EdgeId id);
    [[nodiscard]] bool removeVertex(VertexId id);

    [[nodiscard]] std::optional<EdgeSplitResult> splitEdge(EdgeId id, float factor = 0.5F);
    [[nodiscard]] std::optional<FaceExtrudeResult> extrudeFace(FaceId id, Vec3 offset);

    [[nodiscard]] bool hasVertex(VertexId id) const noexcept;
    [[nodiscard]] bool hasEdge(EdgeId id) const noexcept;
    [[nodiscard]] bool hasFace(FaceId id) const noexcept;
    [[nodiscard]] bool hasCorner(CornerId id) const noexcept;

    [[nodiscard]] const MeshVertex* vertex(VertexId id) const noexcept;
    [[nodiscard]] const MeshEdge* edge(EdgeId id) const noexcept;
    [[nodiscard]] const MeshFace* face(FaceId id) const noexcept;
    [[nodiscard]] const MeshCorner* corner(CornerId id) const noexcept;

    [[nodiscard]] EdgeId edgeBetween(VertexId vertexA, VertexId vertexB) const noexcept;
    [[nodiscard]] std::size_t radialCornerCount(EdgeId id) const noexcept;

    [[nodiscard]] std::optional<Vec3> position(VertexId id) const noexcept;
    [[nodiscard]] bool setPosition(VertexId id, Vec3 position) noexcept;

    [[nodiscard]] std::size_t vertexCount() const noexcept { return vertexOrder_.size(); }
    [[nodiscard]] std::size_t edgeCount() const noexcept { return edgeOrder_.size(); }
    [[nodiscard]] std::size_t faceCount() const noexcept { return faceOrder_.size(); }
    [[nodiscard]] std::size_t cornerCount() const noexcept { return cornerOrder_.size(); }

    [[nodiscard]] std::span<const VertexId> vertexIds() const noexcept { return vertexOrder_; }
    [[nodiscard]] std::span<const EdgeId> edgeIds() const noexcept { return edgeOrder_; }
    [[nodiscard]] std::span<const FaceId> faceIds() const noexcept { return faceOrder_; }
    [[nodiscard]] std::span<const CornerId> cornerIds() const noexcept { return cornerOrder_; }

    [[nodiscard]] const AttributeSet& attributes() const noexcept { return attributes_; }
    [[nodiscard]] AttributeSet& attributes() noexcept { return attributes_; }

    // Topology/attribute validation retained for compatibility with the existing mutation paths.
    [[nodiscard]] MeshValidationResult validate() const;

    // Full hardening gate: validate() plus order/index/registry identity, allocator monotonicity,
    // and duplicate undirected-edge detection. Evaluation/import boundaries should use this form.
    [[nodiscard]] MeshValidationResult validateStrict() const;

private:
    friend class ExtrudeFaceCommand;
    friend class MeshHistory;
#ifdef VORTEX_ENABLE_TEST_HOOKS
    friend struct MeshValidationTestAccess;
#endif

    template <typename IdType>
    [[nodiscard]] IdType allocateId() noexcept { return IdType{nextElementId_++}; }

    [[nodiscard]] EdgeId findEdge(VertexId vertexA, VertexId vertexB) const noexcept;
    [[nodiscard]] std::vector<CornerId> faceCorners(FaceId faceId) const;
    void attachCornerToRadialCycle(EdgeId edgeId, CornerId cornerId);
    void rebuildRadialCycle(EdgeId edgeId);
    void rebuildVertexIndex();
    void rebuildEdgeIndex();
    void rebuildFaceIndex();
    void rebuildCornerIndex();

    std::uint64_t nextElementId_ = 1;
    AttributeSet attributes_;

    std::vector<VertexId> vertexOrder_;
    std::vector<EdgeId> edgeOrder_;
    std::vector<FaceId> faceOrder_;
    std::vector<CornerId> cornerOrder_;

    std::unordered_map<VertexId, std::size_t, IdHash<VertexId>> vertexIndex_;
    std::unordered_map<EdgeId, std::size_t, IdHash<EdgeId>> edgeIndex_;
    std::unordered_map<FaceId, std::size_t, IdHash<FaceId>> faceIndex_;
    std::unordered_map<CornerId, std::size_t, IdHash<CornerId>> cornerIndex_;

    std::unordered_map<VertexId, MeshVertex, IdHash<VertexId>> vertices_;
    std::unordered_map<EdgeId, MeshEdge, IdHash<EdgeId>> edges_;
    std::unordered_map<FaceId, MeshFace, IdHash<FaceId>> faces_;
    std::unordered_map<CornerId, MeshCorner, IdHash<CornerId>> corners_;
};

} // namespace vortex
