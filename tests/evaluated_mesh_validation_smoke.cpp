#include "vortex/core/document.hpp"
#include "vortex/eval/evaluation_cache.hpp"
#include "vortex/eval/evaluator.hpp"
#include "vortex/eval/modifier.hpp"
#include "vortex/eval/validator.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace {

enum class Corruption : std::uint8_t {
    InvalidEdgeEndpoint,
    DuplicateEdge,
    BrokenFaceCycle,
    BrokenRadialCycle,
    AttributeSize,
    OrphanCorner,
};

class CorruptEvaluatedMeshModifier final : public vortex::MeshModifier {
public:
    explicit CorruptEvaluatedMeshModifier(const Corruption corruption) noexcept : corruption_(corruption) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Phase2Corrupt"; }
    [[nodiscard]] vortex::MeshModifierType type() const noexcept override {
        return vortex::MeshModifierType::Transform;
    }
    [[nodiscard]] std::uint64_t revisionToken() const noexcept override {
        return 10'000U + static_cast<std::uint64_t>(corruption_);
    }

    [[nodiscard]] vortex::ModifierApplyResult apply(vortex::EvaluatedMesh& mesh) const override {
        using Index = vortex::EvaluatedMesh::Index;

        switch (corruption_) {
        case Corruption::InvalidEdgeEndpoint: {
            auto& edges = mutableEdges(mesh);
            if (edges.empty()) {
                return {vortex::ModifierApplyError::GeneratedTopologyInvalid};
            }
            edges.front().vertexA = static_cast<Index>(mesh.vertexCount());
            return {};
        }
        case Corruption::DuplicateEdge: {
            auto& edges = mutableEdges(mesh);
            if (edges.size() < 2U) {
                return {vortex::ModifierApplyError::GeneratedTopologyInvalid};
            }
            edges[1].vertexA = edges[0].vertexA;
            edges[1].vertexB = edges[0].vertexB;
            return {};
        }
        case Corruption::BrokenFaceCycle: {
            auto& faces = mutableFaces(mesh);
            auto& corners = mutableCorners(mesh);
            if (faces.empty() || corners.empty() ||
                static_cast<std::size_t>(faces.front().firstCorner) >= corners.size()) {
                return {vortex::ModifierApplyError::GeneratedTopologyInvalid};
            }
            const Index first = faces.front().firstCorner;
            corners[first].next = first;
            return {};
        }
        case Corruption::BrokenRadialCycle: {
            auto& corners = mutableCorners(mesh);
            if (corners.empty()) {
                return {vortex::ModifierApplyError::GeneratedTopologyInvalid};
            }
            corners.front().radialNext = static_cast<Index>(corners.size());
            return {};
        }
        case Corruption::AttributeSize: {
            mutableAttributes(mesh).setDomainSize(vortex::AttributeDomain::Vertex, mesh.vertexCount() + 1U);
            return {};
        }
        case Corruption::OrphanCorner: {
            auto& corners = mutableCorners(mesh);
            if (corners.empty()) {
                return {vortex::ModifierApplyError::GeneratedTopologyInvalid};
            }
            if (corners.size() >= static_cast<std::size_t>(std::numeric_limits<Index>::max())) {
                return {vortex::ModifierApplyError::GeneratedTopologyOverflow};
            }
            const Index orphanIndex = static_cast<Index>(corners.size());
            vortex::EvaluatedCorner orphan = corners.front();
            orphan.next = orphanIndex;
            orphan.prev = orphanIndex;
            orphan.radialNext = orphanIndex;
            orphan.radialPrev = orphanIndex;
            corners.push_back(orphan);
            mutableAttributes(mesh).setDomainSize(vortex::AttributeDomain::Corner, corners.size());
            return {};
        }
        }
        return {vortex::ModifierApplyError::GeneratedTopologyInvalid};
    }

private:
    Corruption corruption_;
};

[[nodiscard]] vortex::MeshId createQuad(vortex::Document& document) {
    vortex::EditableMesh mesh;
    const vortex::VertexId a = mesh.addVertex({0.0F, 0.0F, 0.0F});
    const vortex::VertexId b = mesh.addVertex({1.0F, 0.0F, 0.0F});
    const vortex::VertexId c = mesh.addVertex({1.0F, 1.0F, 0.0F});
    const vortex::VertexId d = mesh.addVertex({0.0F, 1.0F, 0.0F});
    const vortex::FaceId face = mesh.addFace({a, b, c, d});
    assert(face);
    assert(mesh.validateStrict());
    return document.createMesh("Validation Quad", std::move(mesh));
}

[[nodiscard]] bool hasIssue(
    const vortex::EvaluatedMeshValidationResult& result,
    const vortex::EvaluatedMeshValidationCode code) {
    return std::any_of(
        result.issues.begin(),
        result.issues.end(),
        [code](const vortex::EvaluatedMeshValidationIssue& issue) { return issue.code == code; });
}

void testValidEvaluationAndModifierStack() {
    vortex::Document document;
    const vortex::MeshId meshId = createQuad(document);
    const vortex::MeshBlock* source = document.mesh(meshId);
    assert(source != nullptr);

    const vortex::MeshEvaluationResult baseline = vortex::MeshEvaluator::evaluate(*source);
    assert(baseline && baseline.mesh.has_value());
    assert(vortex::EvaluatedMeshValidator::validate(*baseline.mesh));

    vortex::TransformModifier transform({0.5F, 0.0F, 0.0F});
    vortex::MirrorModifier mirror(vortex::MirrorAxis::X, 0.0F);
    vortex::TriangulateModifier triangulate;
    const std::vector<const vortex::MeshModifier*> stack{&transform, &mirror, &triangulate};
    const vortex::MeshEvaluationResult generated = vortex::MeshEvaluator::evaluate(*source, stack);
    assert(generated && generated.mesh.has_value());
    assert(vortex::EvaluatedMeshValidator::validate(*generated.mesh));
}

void testDirectDiagnostics() {
    vortex::Document document;
    const vortex::MeshId meshId = createQuad(document);
    const vortex::MeshBlock* source = document.mesh(meshId);
    assert(source != nullptr);
    const vortex::MeshEvaluationResult baseline = vortex::MeshEvaluator::evaluate(*source);
    assert(baseline && baseline.mesh.has_value());

    {
        vortex::EvaluatedMesh mesh = *baseline.mesh;
        CorruptEvaluatedMeshModifier modifier(Corruption::InvalidEdgeEndpoint);
        assert(modifier.apply(mesh));
        const auto validation = vortex::EvaluatedMeshValidator::validate(mesh);
        assert(!validation);
        assert(hasIssue(validation, vortex::EvaluatedMeshValidationCode::InvalidEdgeEndpoints));
    }
    {
        vortex::EvaluatedMesh mesh = *baseline.mesh;
        CorruptEvaluatedMeshModifier modifier(Corruption::DuplicateEdge);
        assert(modifier.apply(mesh));
        const auto validation = vortex::EvaluatedMeshValidator::validate(mesh);
        assert(!validation);
        assert(hasIssue(validation, vortex::EvaluatedMeshValidationCode::DuplicateEdge));
    }
    {
        vortex::EvaluatedMesh mesh = *baseline.mesh;
        CorruptEvaluatedMeshModifier modifier(Corruption::BrokenFaceCycle);
        assert(modifier.apply(mesh));
        const auto validation = vortex::EvaluatedMeshValidator::validate(mesh);
        assert(!validation);
        assert(hasIssue(validation, vortex::EvaluatedMeshValidationCode::BrokenFaceCycle));
    }
    {
        vortex::EvaluatedMesh mesh = *baseline.mesh;
        CorruptEvaluatedMeshModifier modifier(Corruption::BrokenRadialCycle);
        assert(modifier.apply(mesh));
        const auto validation = vortex::EvaluatedMeshValidator::validate(mesh);
        assert(!validation);
        assert(hasIssue(validation, vortex::EvaluatedMeshValidationCode::InvalidCornerReference));
        assert(hasIssue(validation, vortex::EvaluatedMeshValidationCode::BrokenRadialCycle));
    }
    {
        vortex::EvaluatedMesh mesh = *baseline.mesh;
        CorruptEvaluatedMeshModifier modifier(Corruption::AttributeSize);
        assert(modifier.apply(mesh));
        const auto validation = vortex::EvaluatedMeshValidator::validate(mesh);
        assert(!validation);
        assert(hasIssue(validation, vortex::EvaluatedMeshValidationCode::AttributeSizeMismatch));
    }
    {
        vortex::EvaluatedMesh mesh = *baseline.mesh;
        CorruptEvaluatedMeshModifier modifier(Corruption::OrphanCorner);
        assert(modifier.apply(mesh));
        const auto validation = vortex::EvaluatedMeshValidator::validate(mesh);
        assert(!validation);
        assert(hasIssue(validation, vortex::EvaluatedMeshValidationCode::UnreachableCorner));
    }
}

void testEvaluatorRejectsSuccessfulButCorruptModifier() {
    vortex::Document document;
    const vortex::MeshId meshId = createQuad(document);
    const vortex::MeshBlock* source = document.mesh(meshId);
    assert(source != nullptr);

    CorruptEvaluatedMeshModifier modifier(Corruption::InvalidEdgeEndpoint);
    const std::vector<const vortex::MeshModifier*> stack{&modifier};
    const vortex::MeshEvaluationResult result = vortex::MeshEvaluator::evaluate(*source, stack);
    assert(!result);
    assert(result.error == vortex::MeshEvaluationError::ModifierFailed);
    assert(result.modifierError == vortex::ModifierApplyError::GeneratedTopologyInvalid);
    assert(result.modifierIndex.has_value() && *result.modifierIndex == 0U);
    assert(result.evaluatedValidationCode.has_value());
    assert(*result.evaluatedValidationCode == vortex::EvaluatedMeshValidationCode::InvalidEdgeEndpoints);

    vortex::EvaluationCache cache;
    const vortex::CachedEvaluationResult cached = cache.evaluate(*source, stack);
    assert(!cached);
    assert(!cached.cacheHit);
    assert(cached.error == vortex::MeshEvaluationError::ModifierFailed);
    assert(cached.modifierError == vortex::ModifierApplyError::GeneratedTopologyInvalid);
    assert(cached.modifierIndex.has_value() && *cached.modifierIndex == 0U);
    assert(cached.evaluatedValidationCode.has_value());
    assert(*cached.evaluatedValidationCode == vortex::EvaluatedMeshValidationCode::InvalidEdgeEndpoints);
}

} // namespace

int main() {
    testValidEvaluationAndModifierStack();
    testDirectDiagnostics();
    testEvaluatorRejectsSuccessfulButCorruptModifier();

    std::cout << "Vortex3D evaluated mesh validation passed\n";
    return 0;
}
