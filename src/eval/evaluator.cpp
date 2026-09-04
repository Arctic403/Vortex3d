#include "vortex/eval/evaluator.hpp"

#include "vortex/mesh/editable_mesh.hpp"

#include <limits>
#include <span>
#include <unordered_map>
#include <utility>

namespace vortex {
namespace {

template <typename IdType>
using IndexMap = std::unordered_map<IdType, EvaluatedMesh::Index, IdHash<IdType>>;

[[nodiscard]] bool countFitsIndex(const std::size_t count) noexcept {
    return count <= static_cast<std::size_t>(std::numeric_limits<EvaluatedMesh::Index>::max());
}

template <typename IdType>
[[nodiscard]] IndexMap<IdType> buildIndexMap(const std::span<const IdType> ids) {
    IndexMap<IdType> result;
    result.reserve(ids.size());
    for (std::size_t index = 0; index < ids.size(); ++index) {
        result.emplace(ids[index], static_cast<EvaluatedMesh::Index>(index));
    }
    return result;
}

template <typename IdType>
[[nodiscard]] std::optional<EvaluatedMesh::Index> indexFor(const IndexMap<IdType>& map, const IdType id) noexcept {
    const auto it = map.find(id);
    if (it == map.end()) {
        return std::nullopt;
    }
    return it->second;
}

[[nodiscard]] MeshEvaluationResult fail(const MeshEvaluationError error) {
    return MeshEvaluationResult{std::nullopt, error};
}

} // namespace

MeshEvaluationResult MeshEvaluator::evaluate(const MeshBlock& source) {
    if (!source.authoredMesh) {
        return fail(MeshEvaluationError::MissingAuthoredMesh);
    }

    const EditableMesh& authored = *source.authoredMesh;
    if (!authored.validate()) {
        return fail(MeshEvaluationError::InvalidSourceMesh);
    }

    if (!countFitsIndex(authored.vertexCount()) || !countFitsIndex(authored.edgeCount()) ||
        !countFitsIndex(authored.faceCount()) || !countFitsIndex(authored.cornerCount())) {
        return fail(MeshEvaluationError::ElementCountOverflow);
    }

    const IndexMap<VertexId> vertexIndex = buildIndexMap<VertexId>(authored.vertexIds());
    const IndexMap<EdgeId> edgeIndex = buildIndexMap<EdgeId>(authored.edgeIds());
    const IndexMap<CornerId> cornerIndex = buildIndexMap<CornerId>(authored.cornerIds());

    EvaluatedMesh evaluated;
    evaluated.sourceMeshId_ = source.id;
    evaluated.sourceRevision_ = source.revision;
    evaluated.attributes_ = authored.attributes();

    evaluated.vertices_.reserve(authored.vertexCount());
    for (const VertexId id : authored.vertexIds()) {
        evaluated.vertices_.push_back(EvaluatedVertex{id});
    }

    evaluated.edges_.reserve(authored.edgeCount());
    for (const EdgeId id : authored.edgeIds()) {
        const MeshEdge* edge = authored.edge(id);
        if (edge == nullptr) {
            return fail(MeshEvaluationError::MissingTopologyReference);
        }
        const auto vertexA = indexFor(vertexIndex, edge->vertexA);
        const auto vertexB = indexFor(vertexIndex, edge->vertexB);
        if (!vertexA || !vertexB) {
            return fail(MeshEvaluationError::MissingTopologyReference);
        }
        evaluated.edges_.push_back(EvaluatedEdge{*vertexA, *vertexB, id});
    }

    evaluated.faces_.reserve(authored.faceCount());
    for (const FaceId id : authored.faceIds()) {
        const MeshFace* face = authored.face(id);
        if (face == nullptr) {
            return fail(MeshEvaluationError::MissingTopologyReference);
        }
        const auto firstCorner = indexFor(cornerIndex, face->firstCorner);
        if (!firstCorner) {
            return fail(MeshEvaluationError::MissingTopologyReference);
        }
        evaluated.faces_.push_back(EvaluatedFace{*firstCorner, face->cornerCount, id});
    }

    evaluated.corners_.reserve(authored.cornerCount());
    for (const CornerId id : authored.cornerIds()) {
        const MeshCorner* corner = authored.corner(id);
        if (corner == nullptr) {
            return fail(MeshEvaluationError::MissingTopologyReference);
        }

        const auto vertex = indexFor(vertexIndex, corner->vertexId);
        const auto edge = indexFor(edgeIndex, corner->edgeId);
        const auto next = indexFor(cornerIndex, corner->next);
        const auto prev = indexFor(cornerIndex, corner->prev);
        const auto radialNext = indexFor(cornerIndex, corner->radialNext);
        const auto radialPrev = indexFor(cornerIndex, corner->radialPrev);
        if (!vertex || !edge || !next || !prev || !radialNext || !radialPrev) {
            return fail(MeshEvaluationError::MissingTopologyReference);
        }

        evaluated.corners_.push_back(EvaluatedCorner{
            *vertex,
            *edge,
            *next,
            *prev,
            *radialNext,
            *radialPrev,
            id});
    }

    return MeshEvaluationResult{std::move(evaluated), MeshEvaluationError::None};
}

} // namespace vortex
