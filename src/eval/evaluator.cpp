#include "vortex/eval/evaluator.hpp"

#include "vortex/mesh/editable_mesh.hpp"

#include <cstddef>
#include <cstdint>
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
    MeshEvaluationResult result;
    result.error = error;
    return result;
}

[[nodiscard]] MeshEvaluationResult failModifier(
    const MeshEvaluationError error,
    const ModifierApplyError modifierError,
    const std::size_t modifierIndex) {
    MeshEvaluationResult result;
    result.error = error;
    result.modifierError = modifierError;
    result.modifierIndex = modifierIndex;
    return result;
}

[[nodiscard]] MeshEvaluationResult failNormals(const NormalGenerationError normalError) {
    MeshEvaluationResult result;
    result.error = MeshEvaluationError::NormalGenerationFailed;
    result.normalError = normalError;
    return result;
}

void mixRevisionByte(std::uint64_t& hash, const std::uint8_t value) noexcept {
    constexpr std::uint64_t fnvPrime = 1099511628211ULL;
    hash ^= value;
    hash *= fnvPrime;
}

void mixRevisionValue(std::uint64_t& hash, const std::uint64_t value) noexcept {
    for (std::uint32_t shift = 0; shift < 64U; shift += 8U) {
        mixRevisionByte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

[[nodiscard]] std::optional<std::uint64_t> modifierStackRevision(
    const std::span<const MeshModifier* const> modifiers,
    std::size_t& nullModifierIndex) noexcept {
    if (modifiers.empty()) {
        return std::uint64_t{0};
    }

    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < modifiers.size(); ++index) {
        const MeshModifier* modifier = modifiers[index];
        if (modifier == nullptr) {
            nullModifierIndex = index;
            return std::nullopt;
        }
        mixRevisionByte(hash, static_cast<std::uint8_t>(modifier->type()));
        mixRevisionValue(hash, modifier->revisionToken());
    }
    return hash;
}

} // namespace

MeshEvaluationKeyResult MeshEvaluator::cacheKeyFor(
    const MeshBlock& source,
    const std::span<const MeshModifier* const> modifiers) noexcept {
    MeshEvaluationKeyResult result;
    if (source.authoredMesh() == nullptr) {
        result.error = MeshEvaluationError::MissingAuthoredMesh;
        return result;
    }

    std::size_t nullModifierIndex = 0;
    const auto stackRevision = modifierStackRevision(modifiers, nullModifierIndex);
    if (!stackRevision) {
        result.error = MeshEvaluationError::NullModifier;
        result.modifierIndex = nullModifierIndex;
        return result;
    }

    result.key = EvaluationCacheKey{source.id, source.revision, *stackRevision};
    return result;
}

MeshEvaluationResult MeshEvaluator::evaluate(
    const MeshBlock& source,
    const std::span<const MeshModifier* const> modifiers) {
    const MeshEvaluationKeyResult keyResult = cacheKeyFor(source, modifiers);
    if (!keyResult) {
        MeshEvaluationResult result;
        result.error = keyResult.error;
        result.modifierIndex = keyResult.modifierIndex;
        return result;
    }

    const EditableMesh* authoredMesh = source.authoredMesh();
    if (authoredMesh == nullptr) {
        return fail(MeshEvaluationError::MissingAuthoredMesh);
    }
    const EditableMesh& authored = *authoredMesh;
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
    evaluated.cacheKey_ = *keyResult.key;
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

    for (std::size_t index = 0; index < modifiers.size(); ++index) {
        const ModifierApplyResult modifierResult = modifiers[index]->apply(evaluated);
        if (!modifierResult) {
            return failModifier(MeshEvaluationError::ModifierFailed, modifierResult.error, index);
        }
    }

    const NormalGenerationResult normalResult = DerivedNormalsGenerator::generate(evaluated);
    if (!normalResult) {
        return failNormals(normalResult.error);
    }

    MeshEvaluationResult result;
    result.mesh = std::move(evaluated);
    return result;
}

} // namespace vortex
