#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

namespace {

template <typename T>
const T& choose(const std::vector<T>& values, std::mt19937& rng) {
    assert(!values.empty());
    std::uniform_int_distribution<std::size_t> distribution(0, values.size() - 1);
    return values[distribution(rng)];
}

} // namespace

int main() {
    vortex::EditableMesh mesh;
    std::mt19937 rng(0x56334431U);

    std::vector<vortex::VertexId> knownVertices;
    std::vector<vortex::EdgeId> knownEdges;
    std::vector<vortex::FaceId> knownFaces;
    std::uint32_t island = 0;

    const auto addIsland = [&]() {
        const float x = static_cast<float>(island++) * 4.0F;
        const bool makeQuad = (rng() & 1U) != 0U;
        std::vector<vortex::VertexId> vertices;
        vertices.push_back(mesh.addVertex({x + 0.0F, 0.0F, 0.0F}));
        vertices.push_back(mesh.addVertex({x + 1.0F, 0.0F, 0.0F}));
        vertices.push_back(mesh.addVertex({x + 1.0F, 0.0F, 1.0F}));
        if (makeQuad) {
            vertices.push_back(mesh.addVertex({x + 0.0F, 0.0F, 1.0F}));
        }

        for (const auto vertexId : vertices) {
            assert(vertexId);
            knownVertices.push_back(vertexId);
        }

        const auto faceId = mesh.addFace(vertices);
        assert(faceId);
        knownFaces.push_back(faceId);

        for (std::size_t index = 0; index < vertices.size(); ++index) {
            const auto edgeId = mesh.edgeBetween(vertices[index], vertices[(index + 1) % vertices.size()]);
            assert(edgeId);
            knownEdges.push_back(edgeId);
        }
    };

    for (std::uint32_t step = 0; step < 300; ++step) {
        const std::uint32_t action = rng() % 100U;

        if (mesh.faceCount() == 0 || action < 28U) {
            addIsland();
        } else if (action < 58U && !knownEdges.empty()) {
            const auto edgeId = choose(knownEdges, rng);
            if (mesh.hasEdge(edgeId)) {
                static constexpr float factors[] = {0.25F, 0.5F, 0.75F};
                const float factor = factors[rng() % 3U];
                const auto split = mesh.splitEdge(edgeId, factor);
                if (split) {
                    assert(split->retainedEdge == edgeId);
                    knownVertices.push_back(split->newVertex);
                    knownEdges.push_back(split->newEdge);
                }
            }
        } else if (action < 78U && !knownFaces.empty()) {
            const auto faceId = choose(knownFaces, rng);
            if (mesh.hasFace(faceId)) {
                assert(mesh.removeFace(faceId, true));
            }
        } else if (action < 94U && !knownVertices.empty()) {
            const auto vertexId = choose(knownVertices, rng);
            if (mesh.hasVertex(vertexId)) {
                const auto position = mesh.position(vertexId);
                assert(position);
                const float delta = static_cast<float>(static_cast<int>(rng() % 7U) - 3) * 0.01F;
                assert(mesh.setPosition(vertexId, {position->x, position->y + delta, position->z}));
            }
        } else if (!knownVertices.empty()) {
            const auto vertexId = choose(knownVertices, rng);
            if (mesh.hasVertex(vertexId)) {
                (void)mesh.removeVertex(vertexId);
            }
        }

        const auto validation = mesh.validate();
        assert(validation);
        assert(mesh.attributes().validateSizes());
        assert(mesh.attributes().domainSize(vortex::AttributeDomain::Vertex) == mesh.vertexCount());
        assert(mesh.attributes().domainSize(vortex::AttributeDomain::Edge) == mesh.edgeCount());
        assert(mesh.attributes().domainSize(vortex::AttributeDomain::Face) == mesh.faceCount());
        assert(mesh.attributes().domainSize(vortex::AttributeDomain::Corner) == mesh.cornerCount());
    }

    assert(mesh.validate());
    std::cout << "Vortex3D deterministic randomized kernel torture test passed\n";
    return 0;
}
