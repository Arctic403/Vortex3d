#include "vortex/mesh/editable_mesh.hpp"

#include <algorithm>
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

EditableMesh::EdgeLookupKey EditableMesh::edgeLookupKey(
    const VertexId vertexA,
    const VertexId vertexB) noexcept {
    return vertexA.value() < vertexB.value()
        ? EdgeLookupKey{vertexA, vertexB}
        : EdgeLookupKey{vertexB, vertexA};
}

EdgeId EditableMesh::findEdge(const VertexId vertexA, const VertexId vertexB) const noexcept {
    if (!vertexA || !vertexB || vertexA == vertexB) {
        return {};
    }
    const auto it = edgeLookup_.find(edgeLookupKey(vertexA, vertexB));
    return it == edgeLookup_.end() ? EdgeId{} : it->second;
}

EdgeId EditableMesh::edgeBetween(const VertexId vertexA, const VertexId vertexB) const noexcept {
    return findEdge(vertexA, vertexB);
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
    edgeLookup_.emplace(edgeLookupKey(vertexA, vertexB), id);
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

std::vector<CornerId> EditableMesh::faceCorners(const FaceId faceId) const {
    std::vector<CornerId> result;
    const MeshFace* faceData = face(faceId);
    if (faceData == nullptr || !faceData->firstCorner) {
        return result;
    }

    result.reserve(faceData->cornerCount);
    CornerId cursor = faceData->firstCorner;
    for (std::uint32_t index = 0; index < faceData->cornerCount; ++index) {
        const MeshCorner* cornerData = corner(cursor);
        if (cornerData == nullptr) {
            result.clear();
            return result;
        }
        result.push_back(cursor);
        cursor = cornerData->next;
    }

    if (cursor != faceData->firstCorner) {
        result.clear();
    }
    return result;
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

void EditableMesh::rebuildRadialCycle(const EdgeId edgeId) {
    const auto edgeIt = edges_.find(edgeId);
    if (edgeIt == edges_.end()) {
        return;
    }

    std::vector<CornerId> radialCorners;
    for (const CornerId cornerId : cornerOrder_) {
        const auto cornerIt = corners_.find(cornerId);
        if (cornerIt != corners_.end() && cornerIt->second.edgeId == edgeId) {
            radialCorners.push_back(cornerId);
        }
    }

    if (radialCorners.empty()) {
        edgeIt->second.anyCorner = {};
        return;
    }

    edgeIt->second.anyCorner = radialCorners.front();
    for (std::size_t index = 0; index < radialCorners.size(); ++index) {
        MeshCorner& cornerData = corners_.at(radialCorners[index]);
        cornerData.radialNext = radialCorners[(index + 1) % radialCorners.size()];
        cornerData.radialPrev = radialCorners[(index + radialCorners.size() - 1) % radialCorners.size()];
    }
}

void EditableMesh::rebuildVertexIndex() {
    vertexIndex_.clear();
    for (std::size_t index = 0; index < vertexOrder_.size(); ++index) {
        vertexIndex_.emplace(vertexOrder_[index], index);
    }
}

void EditableMesh::rebuildEdgeIndex() {
    edgeIndex_.clear();
    for (std::size_t index = 0; index < edgeOrder_.size(); ++index) {
        edgeIndex_.emplace(edgeOrder_[index], index);
    }
}

void EditableMesh::rebuildEdgeLookup() {
    edgeLookup_.clear();
    edgeLookup_.reserve(edges_.size());
    for (const EdgeId edgeId : edgeOrder_) {
        const auto it = edges_.find(edgeId);
        if (it == edges_.end()) {
            continue;
        }
        const MeshEdge& edgeData = it->second;
        if (!edgeData.vertexA || !edgeData.vertexB || edgeData.vertexA == edgeData.vertexB) {
            continue;
        }
        edgeLookup_.emplace(edgeLookupKey(edgeData.vertexA, edgeData.vertexB), edgeId);
    }
}

void EditableMesh::rebuildFaceIndex() {
    faceIndex_.clear();
    for (std::size_t index = 0; index < faceOrder_.size(); ++index) {
        faceIndex_.emplace(faceOrder_[index], index);
    }
}

void EditableMesh::rebuildCornerIndex() {
    cornerIndex_.clear();
    for (std::size_t index = 0; index < cornerOrder_.size(); ++index) {
        cornerIndex_.emplace(cornerOrder_[index], index);
    }
}

bool EditableMesh::removeFace(const FaceId id, const bool removeUnusedEdges) {
    if (!hasFace(id) || !validate()) {
        return false;
    }

    const std::vector<CornerId> faceCornerIds = faceCorners(id);
    const MeshFace* faceData = face(id);
    if (faceData == nullptr || faceCornerIds.size() != faceData->cornerCount) {
        return false;
    }

    std::unordered_set<EdgeId, IdHash<EdgeId>> affectedEdges;
    std::vector<std::pair<std::size_t, CornerId>> cornerRemovals;
    cornerRemovals.reserve(faceCornerIds.size());
    for (const CornerId cornerId : faceCornerIds) {
        const MeshCorner* cornerData = corner(cornerId);
        const auto indexIt = cornerIndex_.find(cornerId);
        if (cornerData == nullptr || indexIt == cornerIndex_.end()) {
            return false;
        }
        affectedEdges.insert(cornerData->edgeId);
        cornerRemovals.emplace_back(indexIt->second, cornerId);
    }

    std::sort(cornerRemovals.begin(), cornerRemovals.end(), [](const auto& left, const auto& right) {
        return left.first > right.first;
    });

    for (const auto& [index, cornerId] : cornerRemovals) {
        if (!attributes_.eraseDomainIndex(AttributeDomain::Corner, index)) {
            return false;
        }
        cornerOrder_.erase(cornerOrder_.begin() + static_cast<std::ptrdiff_t>(index));
        corners_.erase(cornerId);
    }
    rebuildCornerIndex();

    const auto faceIndexIt = faceIndex_.find(id);
    if (faceIndexIt == faceIndex_.end()) {
        return false;
    }
    const std::size_t removedFaceIndex = faceIndexIt->second;
    if (!attributes_.eraseDomainIndex(AttributeDomain::Face, removedFaceIndex)) {
        return false;
    }
    faceOrder_.erase(faceOrder_.begin() + static_cast<std::ptrdiff_t>(removedFaceIndex));
    faces_.erase(id);
    rebuildFaceIndex();

    for (const EdgeId edgeId : affectedEdges) {
        rebuildRadialCycle(edgeId);
    }

    if (removeUnusedEdges) {
        std::vector<EdgeId> unusedEdges;
        for (const EdgeId edgeId : affectedEdges) {
            if (hasEdge(edgeId) && radialCornerCount(edgeId) == 0) {
                unusedEdges.push_back(edgeId);
            }
        }
        for (const EdgeId edgeId : unusedEdges) {
            if (!removeEdge(edgeId)) {
                return false;
            }
        }
    }

    return static_cast<bool>(validate());
}

bool EditableMesh::removeEdge(const EdgeId id) {
    if (!hasEdge(id) || !validate()) {
        return false;
    }

    for (const CornerId cornerId : cornerOrder_) {
        const MeshCorner* cornerData = corner(cornerId);
        if (cornerData != nullptr && cornerData->edgeId == id) {
            return false;
        }
    }

    const auto indexIt = edgeIndex_.find(id);
    if (indexIt == edgeIndex_.end()) {
        return false;
    }
    const std::size_t index = indexIt->second;
    if (!attributes_.eraseDomainIndex(AttributeDomain::Edge, index)) {
        return false;
    }
    const MeshEdge removedEdge = edges_.at(id);
    edgeOrder_.erase(edgeOrder_.begin() + static_cast<std::ptrdiff_t>(index));
    edgeIndex_.erase(id);
    edgeLookup_.erase(edgeLookupKey(removedEdge.vertexA, removedEdge.vertexB));
    edges_.erase(id);
    rebuildEdgeIndex();
    return static_cast<bool>(validate());
}

bool EditableMesh::removeVertex(const VertexId id) {
    if (!hasVertex(id) || !validate()) {
        return false;
    }

    for (const EdgeId edgeId : edgeOrder_) {
        const MeshEdge* edgeData = edge(edgeId);
        if (edgeData != nullptr && (edgeData->vertexA == id || edgeData->vertexB == id)) {
            return false;
        }
    }

    const auto indexIt = vertexIndex_.find(id);
    if (indexIt == vertexIndex_.end()) {
        return false;
    }
    const std::size_t index = indexIt->second;
    if (!attributes_.eraseDomainIndex(AttributeDomain::Vertex, index)) {
        return false;
    }
    vertexOrder_.erase(vertexOrder_.begin() + static_cast<std::ptrdiff_t>(index));
    vertexIndex_.erase(id);
    vertices_.erase(id);
    rebuildVertexIndex();
    return static_cast<bool>(validate());
}

std::optional<EdgeSplitResult> EditableMesh::splitEdge(const EdgeId id, const float factor) {
    if (!hasEdge(id) || factor <= 0.0F || factor >= 1.0F || !validate()) {
        return std::nullopt;
    }

    const MeshEdge originalEdge = edges_.at(id);
    const auto positionA = position(originalEdge.vertexA);
    const auto positionB = position(originalEdge.vertexB);
    const auto vertexAIndexIt = vertexIndex_.find(originalEdge.vertexA);
    const auto vertexBIndexIt = vertexIndex_.find(originalEdge.vertexB);
    const auto originalEdgeIndexIt = edgeIndex_.find(id);
    if (!positionA || !positionB || vertexAIndexIt == vertexIndex_.end() ||
        vertexBIndexIt == vertexIndex_.end() || originalEdgeIndexIt == edgeIndex_.end()) {
        return std::nullopt;
    }

    const std::size_t vertexAIndex = vertexAIndexIt->second;
    const std::size_t vertexBIndex = vertexBIndexIt->second;
    const std::size_t originalEdgeIndex = originalEdgeIndexIt->second;

    std::vector<CornerId> affectedCorners;
    for (const CornerId cornerId : cornerOrder_) {
        const MeshCorner* cornerData = corner(cornerId);
        if (cornerData != nullptr && cornerData->edgeId == id) {
            const MeshCorner* nextData = corner(cornerData->next);
            if (nextData == nullptr) {
                return std::nullopt;
            }
            const bool forward = cornerData->vertexId == originalEdge.vertexA && nextData->vertexId == originalEdge.vertexB;
            const bool reverse = cornerData->vertexId == originalEdge.vertexB && nextData->vertexId == originalEdge.vertexA;
            if (!forward && !reverse) {
                return std::nullopt;
            }
            affectedCorners.push_back(cornerId);
        }
    }

    const Vec3 splitPosition{
        positionA->x + (positionB->x - positionA->x) * factor,
        positionA->y + (positionB->y - positionA->y) * factor,
        positionA->z + (positionB->z - positionA->z) * factor};

    const VertexId newVertex = addVertex(splitPosition);
    const std::size_t newVertexIndex = vertexIndex_.at(newVertex);
    (void)attributes_.interpolateDomainIndex(
        AttributeDomain::Vertex,
        vertexAIndex,
        vertexBIndex,
        newVertexIndex,
        factor);

    const EdgeId newEdge = addEdge(newVertex, originalEdge.vertexB);
    if (!newEdge) {
        return std::nullopt;
    }
    const std::size_t newEdgeIndex = edgeIndex_.at(newEdge);
    (void)attributes_.copyDomainIndex(AttributeDomain::Edge, originalEdgeIndex, newEdgeIndex);

    MeshEdge& retainedEdge = edges_.at(id);
    edgeLookup_.erase(edgeLookupKey(originalEdge.vertexA, originalEdge.vertexB));
    retainedEdge.vertexB = newVertex;
    edgeLookup_.emplace(edgeLookupKey(retainedEdge.vertexA, retainedEdge.vertexB), id);

    struct CornerInterpolation final {
        CornerId source;
        CornerId destinationEndpoint;
        CornerId inserted;
        float factor = 0.5F;
    };
    std::vector<CornerInterpolation> interpolationJobs;
    interpolationJobs.reserve(affectedCorners.size());

    for (const CornerId cornerId : affectedCorners) {
        const MeshCorner currentBefore = corners_.at(cornerId);
        const MeshCorner nextBefore = corners_.at(currentBefore.next);
        const bool forward = currentBefore.vertexId == originalEdge.vertexA;
        const float localFactor = forward ? factor : 1.0F - factor;

        const CornerId insertedCornerId = allocateId<CornerId>();
        const EdgeId currentEdgeId = forward ? id : newEdge;
        const EdgeId insertedEdgeId = forward ? newEdge : id;

        const std::size_t insertedIndex = cornerOrder_.size();
        cornerOrder_.push_back(insertedCornerId);
        cornerIndex_.emplace(insertedCornerId, insertedIndex);
        corners_.emplace(
            insertedCornerId,
            MeshCorner{
                insertedCornerId,
                currentBefore.faceId,
                newVertex,
                insertedEdgeId,
                currentBefore.next,
                currentBefore.id,
                {},
                {}});

        MeshCorner& current = corners_.at(currentBefore.id);
        MeshCorner& next = corners_.at(nextBefore.id);
        current.edgeId = currentEdgeId;
        current.next = insertedCornerId;
        next.prev = insertedCornerId;
        ++faces_.at(currentBefore.faceId).cornerCount;

        interpolationJobs.push_back(CornerInterpolation{
            currentBefore.id,
            nextBefore.id,
            insertedCornerId,
            localFactor});
    }

    attributes_.setDomainSize(AttributeDomain::Corner, cornerOrder_.size());
    for (const CornerInterpolation& job : interpolationJobs) {
        (void)attributes_.interpolateDomainIndex(
            AttributeDomain::Corner,
            cornerIndex_.at(job.source),
            cornerIndex_.at(job.destinationEndpoint),
            cornerIndex_.at(job.inserted),
            job.factor);
    }

    rebuildRadialCycle(id);
    rebuildRadialCycle(newEdge);

    if (!validate()) {
        return std::nullopt;
    }

    return EdgeSplitResult{
        newVertex,
        id,
        newEdge,
        static_cast<std::uint32_t>(affectedCorners.size())};
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

std::size_t EditableMesh::radialCornerCount(const EdgeId id) const noexcept {
    if (!hasEdge(id)) {
        return 0;
    }

    std::size_t count = 0;
    for (const CornerId cornerId : cornerOrder_) {
        const MeshCorner* cornerData = corner(cornerId);
        if (cornerData != nullptr && cornerData->edgeId == id) {
            ++count;
        }
    }
    return count;
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

        const std::size_t expectedRadialCount = radialCornerCount(id);
        if (!edgeData->anyCorner && expectedRadialCount != 0) {
            issue(MeshValidationCode::BrokenRadialCycle, id.value(), "Edge has corners but no radial anchor");
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
            if (visited.size() != expectedRadialCount) {
                issue(MeshValidationCode::BrokenRadialCycle, id.value(), "Radial cycle does not cover every corner using the edge");
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
            } else {
                const bool startsOnEdge = edgeData->vertexA == current->vertexId || edgeData->vertexB == current->vertexId;
                const bool endsOnEdge = edgeData->vertexA == next->vertexId || edgeData->vertexB == next->vertexId;
                const bool connectsBoundary = startsOnEdge && endsOnEdge && current->vertexId != next->vertexId;
                if (!connectsBoundary) {
                    issue(MeshValidationCode::CornerEdgeMismatch, current->id.value(), "Corner edge does not connect this corner to the next face corner");
                }
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

namespace vortex {

EditableMeshSerializedState EditableMesh::serializedState() const {
    EditableMeshSerializedState state;
    state.nextElementId = nextElementId_;
    state.attributes = attributes_;
    state.vertices.reserve(vertexOrder_.size());
    state.edges.reserve(edgeOrder_.size());
    state.faces.reserve(faceOrder_.size());
    state.corners.reserve(cornerOrder_.size());
    for (const VertexId id : vertexOrder_) state.vertices.push_back(vertices_.at(id));
    for (const EdgeId id : edgeOrder_) state.edges.push_back(edges_.at(id));
    for (const FaceId id : faceOrder_) state.faces.push_back(faces_.at(id));
    for (const CornerId id : cornerOrder_) state.corners.push_back(corners_.at(id));
    return state;
}

std::optional<EditableMesh> EditableMesh::fromSerializedState(EditableMeshSerializedState state) {
    EditableMesh mesh;
    mesh.nextElementId_ = state.nextElementId;
    mesh.attributes_ = std::move(state.attributes);
    mesh.vertexOrder_.clear();
    mesh.edgeOrder_.clear();
    mesh.faceOrder_.clear();
    mesh.cornerOrder_.clear();
    mesh.vertices_.clear();
    mesh.edges_.clear();
    mesh.faces_.clear();
    mesh.corners_.clear();
    mesh.edgeLookup_.clear();

    for (const MeshVertex& value : state.vertices) {
        if (!value.id || mesh.vertices_.contains(value.id)) return std::nullopt;
        mesh.vertexOrder_.push_back(value.id);
        mesh.vertices_.emplace(value.id, value);
    }
    for (const MeshEdge& value : state.edges) {
        if (!value.id || mesh.edges_.contains(value.id)) return std::nullopt;
        mesh.edgeOrder_.push_back(value.id);
        mesh.edges_.emplace(value.id, value);
    }
    for (const MeshFace& value : state.faces) {
        if (!value.id || mesh.faces_.contains(value.id)) return std::nullopt;
        mesh.faceOrder_.push_back(value.id);
        mesh.faces_.emplace(value.id, value);
    }
    for (const MeshCorner& value : state.corners) {
        if (!value.id || mesh.corners_.contains(value.id)) return std::nullopt;
        mesh.cornerOrder_.push_back(value.id);
        mesh.corners_.emplace(value.id, value);
    }
    mesh.rebuildVertexIndex();
    mesh.rebuildEdgeIndex();
    mesh.rebuildEdgeLookup();
    mesh.rebuildFaceIndex();
    mesh.rebuildCornerIndex();
    if (!mesh.validateStrict()) return std::nullopt;
    return mesh;
}

} // namespace vortex
