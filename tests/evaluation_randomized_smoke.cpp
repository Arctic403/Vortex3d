#include "vortex/core/document.hpp"
#include "vortex/eval/evaluator.hpp"
#include "vortex/eval/modifier.hpp"
#include "vortex/eval/validator.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <span>
#include <vector>

namespace {

bool sameFloat(const float a, const float b) noexcept {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

bool sameVec3(const vortex::Vec3& a, const vortex::Vec3& b) noexcept {
    return sameFloat(a.x, b.x) && sameFloat(a.y, b.y) && sameFloat(a.z, b.z);
}

bool equivalent(const vortex::EvaluatedMesh& a, const vortex::EvaluatedMesh& b) {
    if (a.cacheKey() != b.cacheKey() || a.vertexCount() != b.vertexCount() || a.edgeCount() != b.edgeCount() ||
        a.faceCount() != b.faceCount() || a.cornerCount() != b.cornerCount()) {
        return false;
    }
    for (std::size_t i = 0; i < a.vertexCount(); ++i) {
        if (a.vertices()[i].sourceId != b.vertices()[i].sourceId) return false;
        const auto pa = a.position(static_cast<vortex::EvaluatedMesh::Index>(i));
        const auto pb = b.position(static_cast<vortex::EvaluatedMesh::Index>(i));
        if (!pa || !pb || !sameVec3(*pa, *pb)) return false;
    }
    for (std::size_t i = 0; i < a.edgeCount(); ++i) {
        const auto& x = a.edges()[i];
        const auto& y = b.edges()[i];
        if (x.vertexA != y.vertexA || x.vertexB != y.vertexB || x.sourceId != y.sourceId) return false;
    }
    for (std::size_t i = 0; i < a.faceCount(); ++i) {
        const auto& x = a.faces()[i];
        const auto& y = b.faces()[i];
        if (x.firstCorner != y.firstCorner || x.cornerCount != y.cornerCount || x.sourceId != y.sourceId) return false;
    }
    for (std::size_t i = 0; i < a.cornerCount(); ++i) {
        const auto& x = a.corners()[i];
        const auto& y = b.corners()[i];
        if (x.vertex != y.vertex || x.edge != y.edge || x.next != y.next || x.prev != y.prev ||
            x.radialNext != y.radialNext || x.radialPrev != y.radialPrev || x.sourceId != y.sourceId) return false;
    }
    const auto* na = a.attributes().values<vortex::Vec3>("normal", vortex::AttributeDomain::Corner);
    const auto* nb = b.attributes().values<vortex::Vec3>("normal", vortex::AttributeDomain::Corner);
    if ((na == nullptr) != (nb == nullptr)) return false;
    if (na != nullptr) {
        if (na->size() != nb->size()) return false;
        for (std::size_t i = 0; i < na->size(); ++i) if (!sameVec3((*na)[i], (*nb)[i])) return false;
    }
    return true;
}

vortex::EditableMesh makeSource() {
    vortex::EditableMesh mesh;
    const auto a = mesh.addVertex({-1.0F, -1.0F, 0.0F});
    const auto b = mesh.addVertex({ 1.0F, -1.0F, 0.0F});
    const auto c = mesh.addVertex({ 1.0F,  1.0F, 0.0F});
    const auto d = mesh.addVertex({-1.0F,  1.0F, 0.0F});
    assert(mesh.addFace({a, b, c, d}));
    assert(mesh.validateStrict());
    return mesh;
}

} // namespace

int main() {
    using namespace vortex;
    Document document;
    const MeshId meshId = document.createMesh("Randomized evaluation source", makeSource());
    const MeshBlock* source = document.mesh(meshId);
    assert(source != nullptr && source->authoredMesh() != nullptr);

    const std::size_t sourceVertices = source->authoredMesh()->vertexCount();
    const std::size_t sourceEdges = source->authoredMesh()->edgeCount();
    const std::size_t sourceFaces = source->authoredMesh()->faceCount();
    const std::size_t sourceCorners = source->authoredMesh()->cornerCount();
    const std::uint64_t sourceRevision = source->evaluationRevision();

    std::mt19937 rng(0x50344556U);
    for (std::size_t iteration = 0; iteration < 200U; ++iteration) {
        const float tx = static_cast<float>(static_cast<int>(rng() % 21U) - 10) * 0.05F;
        const float ty = static_cast<float>(static_cast<int>(rng() % 21U) - 10) * 0.05F;
        const float angle = static_cast<float>(rng() % 9U) * 0.1F;
        const float sx = 0.75F + static_cast<float>(rng() % 6U) * 0.1F;
        TransformModifier transform{{tx, ty, 0.0F}, {0.0F, 0.0F, angle}, {sx, 1.0F, 1.0F}};
        MirrorModifier mirror{static_cast<MirrorAxis>(rng() % 3U), 0.0F, MirrorWeldSettings{(rng() & 1U) != 0U, 0.0001F}};
        TriangulateModifier triangulate;

        std::vector<const MeshModifier*> stack;
        stack.push_back(&transform);
        if ((rng() & 1U) != 0U) stack.push_back(&mirror);
        if ((rng() & 1U) != 0U) stack.push_back(&triangulate);

        const auto first = MeshEvaluator::evaluate(*source, std::span<const MeshModifier* const>{stack.data(), stack.size()});
        const auto second = MeshEvaluator::evaluate(*source, std::span<const MeshModifier* const>{stack.data(), stack.size()});
        assert(first && second);
        assert(first.mesh && second.mesh);
        assert(EvaluatedMeshValidator::validate(*first.mesh));
        assert(EvaluatedMeshValidator::validate(*second.mesh));
        assert(equivalent(*first.mesh, *second.mesh));

        assert(source->evaluationRevision() == sourceRevision);
        assert(source->authoredMesh()->vertexCount() == sourceVertices);
        assert(source->authoredMesh()->edgeCount() == sourceEdges);
        assert(source->authoredMesh()->faceCount() == sourceFaces);
        assert(source->authoredMesh()->cornerCount() == sourceCorners);
        assert(source->authoredMesh()->validateStrict());
    }

    std::cout << "Vortex3D deterministic randomized evaluation stress test passed\n";
    return 0;
}
