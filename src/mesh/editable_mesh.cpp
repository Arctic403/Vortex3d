#include "vortex/mesh/editable_mesh.hpp"

#include <unordered_set>
#include <utility>

namespace vortex {

EditableMesh::EditableMesh() {
    (void)attributes_.create<Vec3>("position", AttributeDomain::Vertex, Vec3{});
    (void)attributes_.create<float>("crease", AttributeDomain::Edge, 0.0F);
    (void)attributes_.create<bool>("sharp", AttributeDomain::Edge, false);
    (void)attributes_.create<bool>("seam", AttributeDomain::Edge, false);
    (void)attributes_.create<std::int32_t>("material_index", AttributeDomain::Face, 0);
    (void)attributes_.create<Vec2>("uv:Map", AttributeDomain::Corner, Vec2{});
    (void)attributes_.create<Vec3>("normal", AttributeDomain::Corner, Vec3{});
}

VertexId EditableMesh::addVertex(const Vec3 positionValue) {
    const VertexId id = allocateId<VertexId>();
    const std::size_t index = vertexOrder_.size();
    vertexOrder_.push_back(id);
    vertexIndex_.emplace(id, index);
    vertices_.emplace(id, MeshVertex{id});
    attributes_.setDomainSize(AttributeDomain::Vertex, vertexOrder_.size());

    auto* positions = attributes_.values<Vec3>("position", AttributeDomain::Vertex);
    if (positions != nullptr) {
        (*positions)[index] = positionValue;
    }
    return id;
}

EdgeId EditableMesh::findEdge(const VertexId vertexA, const VertexId vertexB) const noexcept {
    for (const EdgeId edgeId : edgeOrder_) {
        const auto it = edges_.find(edgeId);
        if (it == edges_.end()) {
            continue;
        }
        const MeshEdge& edge = it->second;
        const bool same = edge.vertexA == vertexA && edge.vertexB == vertexB;
        const bool reverse = edge.vertexA == vertexB && edge.vertexB == vertexA;
        if (same || reverse) {
            return edgeId;
        }
    }
    return {};
}

EdgeId EditableMesh::addEdge(const VertexId vertexA, const VertexId vertexB) {
    if (!hasVertex(vertexA) || !hasVertex(vertexB) || vertexA == vertexB) {
        return {};
    }

    if (const EdgeId existing = findEdge(vertexA, vertexB)) {
        return existing;
    }

    const EdgeId id = allocateId<EdgeId>();
    const std::size_t index = edgeOrder_.size();
    edgeOrder_.push_back(id);
    edgeIndex_.emplace(id, index);
    edges_.emplace(id, MeshEdge{id, vertexA, vertexB, {}});
    attributes_.setDomainSize(AttributeDomain::Edge, edgeOrder_.size());
    return id;
}

FaceId EditableMesh::addFace(const std::vector<VertexId>& vertices) {
    if (vertices.size() < 3) {
        return {};
    }

    std::unordered_set<VertexId, IdHash<VertexId>> unique;
    for (const VertexId vertexId : vertices) {
        if (!hasVertex(vertexId) || !unique.insert(vertexId).second) {
            return {};
        }
    }

    std::vector<EdgeId> boundaryEdges;
    boundaryEdges.reserve(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const VertexId a = vertices[i];
        const VertexId b = vertices[(i + 1) % vertices.size()];
        const EdgeId edgeId = addEdge(a, b);
        if (!edgeId) {
            return {};
        }
        boundaryEdges.push_back(edgeId);
    }

    const FaceId faceId = allocateId<FaceId>();
    const std::size_t faceIndex = faceOrder_.size();
    faceOrder_.push_back(faceId);
    faceIndex_.emplace(faceId, faceIndex);

    std::vector<CornerId> newCorners;
    newCorners.reserve(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const CornerId cornerId = allocateId<CornerId>();
        newCorners.push_back(cornerId);
        const std::size_t cornerIndex = cornerOrder_.size();
        cornerOrder_.push_back(cornerId);
        cornerIndex_.emplace(cornerId, cornerIndex);
        corners_.emplace(cornerId, MeshCorner{cornerId, faceId, vertices[i], boundaryEdges[i], {}, {}, {}, {}});
    }

    for (std::size_t i = 0; i < newCorners.size(); ++i) {
        MeshCorner& cornerData = corners_.at(newCorners[i]);
        cornerData.next = newCorners[(i + 1) % newCorners.size()];
        cornerData.prev = newCorners[(i + newCorners.size() - 1) % newCorners.size()];
        attachCornerToRadialCycle(cornerData.edgeId, cornerData.id);
    }

    faces_.emplace(faceId, MeshFace{faceId, newCorners.front(), static_cast<std::uint32_t>(newCorners.size())});
    attributes_.setDomainSize(AttributeDomain::Face, faceOrder_.size());
    attributes_.setDomainSize(AttributeDomain::Corner, cornerOrder_.size());
    return faceId;
}

void EditableMesh::attachCornerToRadialCycle(const EdgeId edgeId, const CornerId cornerId) {
    MeshEdge& edgeData = edges_.at(edgeId);
    MeshCorner& newCorner = corners_.at(cornerId);

    if (!edgeData.anyCorner) {
        edgeData.anyCorner = cornerId;
        newCorner.radialNext = cornerId;
        newCorner.radialPrev = cornerId;
        return;
    }

    MeshCorner& anchor = corners_.at(edgeData.anyCorner);
    MeshCorner& previous = corners_.at(anchor.radialPrev);
    newCorner.radialNext = anchor.id;
    newCorner.radialPrev = previous.id;
    previous.radialNext = newCorner.id;
    anchor.radialPrev = newCorner.id;
}

bool EditableMesh::hasVertex(const VertexId id) const noexcept { return id && vertices_.contains(id); }
bool EditableMesh::hasEdge(const EdgeId id) const noexcept { return id && edges_.contains(id); }
bool EditableMesh::hasFace(const FaceId id) const noexcept { return id && faces_.contains(id); }
bool EditableMesh::hasCorner(const CornerId id) const noexcept { return id && corners_.contains(id); }

const MeshVertex* EditableMesh::vertex(const VertexId id) const noexcept {
    const auto it = vertices_.find(id);
    return it == vertices_.end() ? nullptr : &it->second;
}

const MeshEdge* EditableMesh::edge(const EdgeId id) const noexcept {
    const auto it = edges_.find(id);
    return it == edges_.end() ? nullptr : &it->second;
}

const MeshFace* EditableMesh::face(const FaceId id) const noexcept {
    const auto it = faces_.find(id);
    return it == faces_.end() ? nullptr : &it->second;
}

const MeshCorner* EditableMesh::corner(const CornerId id) const noexcept {
    const auto it = corners_.find(id);
    return it == corners_.end() ? nullptr : &it->second;
}

std::optional<Vec3> EditableMesh::position(const VertexId id) const noexcept {
    const auto indexIt = vertexIndex_.find(id);
    const auto* positions = attributes_.values<Vec3>("position", AttributeDomain::Vertex);
    if (indexIt == vertexIndex_.end() || positions == nullptr || indexIt->second >= positions->size()) {
        return std::nullopt;
    }
    return (*positions)[indexIt->second];
}

bool EditableMesh::setPosition(const VertexId id, const Vec3 positionValue) noexcept {
    const auto indexIt = vertexIndex_.find(id);
    auto* positions = attributes_.values<Vec3>("position", AttributeDomain::Vertex);
    if (indexIt == vertexIndex_.end() || positions == nullptr || indexIt->second >= positions->size()) {
        return false;
    }
    (*positions)[indexIt->second] = positionValue;
    return true;
}

MeshValidationResult EditableMesh::validate() const {
    MeshValidationResult result;
    const auto issue = [&result](const MeshValidationCode code, const std::uint64_t id, std::string message) {
        result.issues.push_back(MeshValidationIssue{code, id, std::move(message)});
    };

    if (!attributes_.validateSizes() || attributes_.domainSize(AttributeDomain::Vertex) != vertexCount() ||
        attributes_.domainSize(AttributeDomain::Edge) != edgeCount() ||
        attributes_.domainSize(AttributeDomain::Face) != faceCount() ||
        attributes_.domainSize(AttributeDomain::Corner) != cornerCount()) {
        issue(MeshValidationCode::AttributeSizeMismatch, 0, "Attribute domain sizes do not match topology domain sizes");
    }

    for (const VertexId id : vertexOrder_) {
        if (!hasVertex(id)) {
            issue(MeshValidationCode::MissingElement, id.value(), "Vertex order references a missing vertex");
        }
    }

    for (const EdgeId id : edgeOrder_) {
        const MeshEdge* edgeData = edge(id);
        if (edgeData == nullptr) {
            issue(MeshValidationCode::MissingElement, id.value(), "Edge order references a missing edge");
            continue;
        }
        if (!hasVertex(edgeData->vertexA) || !hasVertex(edgeData->vertexB) || edgeData->vertexA == edgeData->vertexB) {
            issue(MeshValidationCode::InvalidEdgeEndpoints, id.value(), "Edge endpoints are missing or identical");
        }

        if (edgeData->anyCorner) {
            const MeshCorner* start = corner(edgeData->anyCorner);
            if (start == nullptr) {
                issue(MeshValidationCode::BrokenRadialCycle, id.value(), "Edge radial anchor is missing");
                continue;
            }

            std::unordered_set<CornerId, IdHash<CornerId>> visited;
            CornerId cursor = start->id;
            while (cursor && !visited.contains(cursor)) {
                visited.insert(cursor);
                const MeshCorner* current = corner(cursor);
                if (current == nullptr || current->edgeId != id || !hasCorner(current->radialNext) || !hasCorner(current->radialPrev)) {
                    issue(MeshValidationCode::BrokenRadialCycle, id.value(), "Radial cycle references invalid topology");
                    break;
                }
                const MeshCorner* next = corner(current->radialNext);
                const MeshCorner* prev = corner(current->radialPrev);
                if (next == nullptr || prev == nullptr || next->radialPrev != current->id || prev->radialNext != current->id) {
                    issue(MeshValidationCode::BrokenRadialCycle, id.value(), "Radial next/prev links are not mutually consistent");
                    break;
                }
                cursor = current->radialNext;
            }
            if (cursor != start->id) {
                issue(MeshValidationCode::BrokenRadialCycle, id.value(), "Radial cycle did not close on its anchor");
            }
        }
    }

    std::unordered_set<CornerId, IdHash<CornerId>> reachableCorners;
    for (const FaceId id : faceOrder_) {
        const MeshFace* faceData = face(id);
        if (faceData == nullptr) {
            issue(MeshValidationCode::MissingElement, id.value(), "Face order references a missing face");
            continue;
        }
        if (faceData->cornerCount < 3) {
            issue(MeshValidationCode::InvalidFaceSize, id.value(), "Face has fewer than three corners");
            continue;
        }

        CornerId cursor = faceData->firstCorner;
        for (std::uint32_t i = 0; i < faceData->cornerCount; ++i) {
            const MeshCorner* current = corner(cursor);
            if (current == nullptr || current->faceId != id || !hasCorner(current->next) || !hasCorner(current->prev)) {
                issue(MeshValidationCode::BrokenFaceCycle, id.value(), "Face corner cycle references invalid topology");
                cursor = {};
                break;
            }

            reachableCorners.insert(current->id);
            const MeshCorner* next = corner(current->next);
            const MeshCorner* prev = corner(current->prev);
            if (next == nullptr || prev == nullptr || next->prev != current->id || prev->next != current->id) {
                issue(MeshValidationCode::BrokenFaceCycle, id.value(), "Face next/prev links are not mutually consistent");
                cursor = {};
                break;
            }

            const MeshEdge* edgeData = edge(current->edgeId);
            if (edgeData == nullptr || !hasVertex(current->vertexId)) {
                issue(MeshValidationCode::MissingElement, current->id.value(), "Corner references a missing vertex or edge");
            } else if (edgeData->vertexA != current->vertexId && edgeData->vertexB != current->vertexId) {
                issue(MeshValidationCode::CornerEdgeMismatch, current->id.value(), "Corner vertex is not an endpoint of its edge");
            }
            cursor = current->next;
        }

        if (cursor && cursor != faceData->firstCorner) {
            issue(MeshValidationCode::BrokenFaceCycle, id.value(), "Face cycle did not close after cornerCount steps");
        }
    }

    for (const CornerId id : cornerOrder_) {
        if (!hasCorner(id)) {
            issue(MeshValidationCode::MissingElement, id.value(), "Corner order references a missing corner");
        } else if (!reachableCorners.contains(id)) {
            issue(MeshValidationCode::UnreachableCorner, id.value(), "Live corner is unreachable from its face");
        }
    }

    return result;
}

} // namespace vortex
