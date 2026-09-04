#include "vortex/geometry/operations.hpp"

#include <algorithm>
#include <unordered_set>

namespace vortex {

std::optional<FaceTopologyView> GeometryOperations::faceTopology(const EditableMesh& mesh, const FaceId faceId) {
    const MeshFace* face = mesh.face(faceId);
    if (face == nullptr || face->cornerCount < 3U || !face->firstCorner) {
        return std::nullopt;
    }

    FaceTopologyView view;
    view.faceId = faceId;
    view.corners.reserve(face->cornerCount);
    view.vertices.reserve(face->cornerCount);
    view.edges.reserve(face->cornerCount);

    CornerId current = face->firstCorner;
    for (std::uint32_t index = 0; index < face->cornerCount; ++index) {
        const MeshCorner* corner = mesh.corner(current);
        if (corner == nullptr || corner->faceId != faceId || !corner->vertexId || !corner->edgeId) {
            return std::nullopt;
        }
        view.corners.push_back(current);
        view.vertices.push_back(corner->vertexId);
        view.edges.push_back(corner->edgeId);
        current = corner->next;
    }
    if (current != face->firstCorner) {
        return std::nullopt;
    }
    return view;
}

std::vector<EdgeId> GeometryOperations::incidentEdges(const EditableMesh& mesh, const VertexId vertexId) {
    std::vector<EdgeId> result;
    if (!mesh.hasVertex(vertexId)) {
        return result;
    }
    for (const EdgeId edgeId : mesh.edgeIds()) {
        const MeshEdge* edge = mesh.edge(edgeId);
        if (edge != nullptr && (edge->vertexA == vertexId || edge->vertexB == vertexId)) {
            result.push_back(edgeId);
        }
    }
    return result;
}

std::vector<FaceId> GeometryOperations::incidentFaces(const EditableMesh& mesh, const VertexId vertexId) {
    std::vector<FaceId> result;
    if (!mesh.hasVertex(vertexId)) {
        return result;
    }
    for (const FaceId faceId : mesh.faceIds()) {
        const auto topology = faceTopology(mesh, faceId);
        if (topology && std::find(topology->vertices.begin(), topology->vertices.end(), vertexId) != topology->vertices.end()) {
            result.push_back(faceId);
        }
    }
    return result;
}

std::vector<VertexId> GeometryOperations::neighboringVertices(const EditableMesh& mesh, const VertexId vertexId) {
    std::vector<VertexId> result;
    for (const EdgeId edgeId : incidentEdges(mesh, vertexId)) {
        const MeshEdge* edge = mesh.edge(edgeId);
        if (edge == nullptr) {
            continue;
        }
        const VertexId other = edge->vertexA == vertexId ? edge->vertexB : edge->vertexA;
        if (std::find(result.begin(), result.end(), other) == result.end()) {
            result.push_back(other);
        }
    }
    return result;
}

std::optional<Vec3> GeometryOperations::faceCentroid(const EditableMesh& mesh, const FaceId faceId) {
    const auto topology = faceTopology(mesh, faceId);
    if (!topology || topology->vertices.empty()) {
        return std::nullopt;
    }
    Vec3 centroid{};
    for (const VertexId vertexId : topology->vertices) {
        const auto position = mesh.position(vertexId);
        if (!position) {
            return std::nullopt;
        }
        centroid.x += position->x;
        centroid.y += position->y;
        centroid.z += position->z;
    }
    const float inverse = 1.0F / static_cast<float>(topology->vertices.size());
    centroid.x *= inverse;
    centroid.y *= inverse;
    centroid.z *= inverse;
    return centroid;
}

bool GeometryOperations::translateVertices(EditableMesh& mesh, const std::span<const VertexId> vertices, const Vec3 offset) {
    std::unordered_set<VertexId, IdHash<VertexId>> unique;
    for (const VertexId vertexId : vertices) {
        if (!mesh.hasVertex(vertexId) || !unique.insert(vertexId).second) {
            if (!mesh.hasVertex(vertexId)) {
                return false;
            }
            continue;
        }
        const auto position = mesh.position(vertexId);
        if (!position) {
            return false;
        }
    }

    EditableMesh snapshot = mesh;
    for (const VertexId vertexId : unique) {
        const Vec3 position = *mesh.position(vertexId);
        if (!mesh.setPosition(vertexId, {position.x + offset.x, position.y + offset.y, position.z + offset.z})) {
            mesh = std::move(snapshot);
            return false;
        }
    }
    if (!mesh.validateStrict()) {
        mesh = std::move(snapshot);
        return false;
    }
    return true;
}

std::optional<EdgeSplitResult> GeometryOperations::splitEdge(EditableMesh& mesh, const EdgeId edgeId, const float factor) {
    return mesh.splitEdge(edgeId, factor);
}

std::optional<FaceExtrudeResult> GeometryOperations::extrudeFace(EditableMesh& mesh, const FaceId faceId, const Vec3 offset) {
    return mesh.extrudeFace(faceId, offset);
}

std::optional<InsetFaceResult> GeometryOperations::insetFace(EditableMesh& mesh, const FaceId faceId, const float factor) {
    if (!(factor > 0.0F && factor < 1.0F)) {
        return std::nullopt;
    }
    const auto topology = faceTopology(mesh, faceId);
    const auto centroid = faceCentroid(mesh, faceId);
    if (!topology || !centroid) {
        return std::nullopt;
    }

    EditableMesh snapshot = mesh;
    std::vector<VertexId> inner;
    inner.reserve(topology->vertices.size());
    for (const VertexId sourceVertex : topology->vertices) {
        const Vec3 source = *mesh.position(sourceVertex);
        const Vec3 position{
            source.x + (centroid->x - source.x) * factor,
            source.y + (centroid->y - source.y) * factor,
            source.z + (centroid->z - source.z) * factor};
        const VertexId vertex = mesh.addVertex(position);
        if (!vertex) {
            mesh = std::move(snapshot);
            return std::nullopt;
        }
        inner.push_back(vertex);
    }

    if (!mesh.removeFace(faceId, false)) {
        mesh = std::move(snapshot);
        return std::nullopt;
    }

    InsetFaceResult result;
    result.sourceFace = faceId;
    result.innerVertices = inner;
    result.innerFace = mesh.addFace(inner);
    if (!result.innerFace) {
        mesh = std::move(snapshot);
        return std::nullopt;
    }

    const std::size_t count = inner.size();
    result.rimFaces.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t next = (index + 1U) % count;
        const FaceId rim = mesh.addFace({topology->vertices[index], topology->vertices[next], inner[next], inner[index]});
        if (!rim) {
            mesh = std::move(snapshot);
            return std::nullopt;
        }
        result.rimFaces.push_back(rim);
    }

    if (!mesh.validateStrict()) {
        mesh = std::move(snapshot);
        return std::nullopt;
    }
    return result;
}

} // namespace vortex
