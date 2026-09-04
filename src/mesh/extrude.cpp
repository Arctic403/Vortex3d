#include "vortex/mesh/editable_mesh.hpp"

#include <utility>

namespace vortex {

std::optional<FaceExtrudeResult> EditableMesh::extrudeFace(const FaceId id, const Vec3 offset) {
    if (!hasFace(id) || !validate()) {
        return std::nullopt;
    }

    EditableMesh snapshot = *this;
    const auto rollback = [&]() -> std::optional<FaceExtrudeResult> {
        *this = std::move(snapshot);
        return std::nullopt;
    };

    const std::vector<CornerId> sourceCorners = faceCorners(id);
    const MeshFace* sourceFace = face(id);
    const auto sourceFaceIndexIt = faceIndex_.find(id);
    if (sourceFace == nullptr || sourceCorners.size() != sourceFace->cornerCount || sourceFaceIndexIt == faceIndex_.end()) {
        return rollback();
    }

    const std::size_t sourceFaceIndex = sourceFaceIndexIt->second;
    std::vector<VertexId> sourceVertices;
    std::vector<std::size_t> sourceVertexIndices;
    std::vector<std::size_t> sourceCornerIndices;
    sourceVertices.reserve(sourceCorners.size());
    sourceVertexIndices.reserve(sourceCorners.size());
    sourceCornerIndices.reserve(sourceCorners.size());

    for (const CornerId cornerId : sourceCorners) {
        const MeshCorner* cornerData = corner(cornerId);
        if (cornerData == nullptr) {
            return rollback();
        }

        const auto vertexIndexIt = vertexIndex_.find(cornerData->vertexId);
        const auto cornerIndexIt = cornerIndex_.find(cornerId);
        if (vertexIndexIt == vertexIndex_.end() || cornerIndexIt == cornerIndex_.end() || !position(cornerData->vertexId)) {
            return rollback();
        }

        sourceVertices.push_back(cornerData->vertexId);
        sourceVertexIndices.push_back(vertexIndexIt->second);
        sourceCornerIndices.push_back(cornerIndexIt->second);
    }

    FaceExtrudeResult result;
    result.sourceFace = id;
    result.newVertices.reserve(sourceVertices.size());
    result.sideFaces.reserve(sourceVertices.size());

    for (std::size_t index = 0; index < sourceVertices.size(); ++index) {
        const auto sourcePosition = position(sourceVertices[index]);
        if (!sourcePosition) {
            return rollback();
        }

        const VertexId newVertex = addVertex({
            sourcePosition->x + offset.x,
            sourcePosition->y + offset.y,
            sourcePosition->z + offset.z});
        if (!newVertex) {
            return rollback();
        }

        const std::size_t newVertexIndex = vertexIndex_.at(newVertex);
        if (!attributes_.copyDomainIndex(AttributeDomain::Vertex, sourceVertexIndices[index], newVertexIndex) ||
            !setPosition(newVertex, {
                sourcePosition->x + offset.x,
                sourcePosition->y + offset.y,
                sourcePosition->z + offset.z})) {
            return rollback();
        }
        result.newVertices.push_back(newVertex);
    }

    result.capFace = addFace(result.newVertices);
    if (!result.capFace) {
        return rollback();
    }

    const auto capFaceIndexIt = faceIndex_.find(result.capFace);
    const std::vector<CornerId> capCorners = faceCorners(result.capFace);
    if (capFaceIndexIt == faceIndex_.end() || capCorners.size() != sourceCorners.size() ||
        !attributes_.copyDomainIndex(AttributeDomain::Face, sourceFaceIndex, capFaceIndexIt->second)) {
        return rollback();
    }

    std::vector<std::size_t> capCornerIndices;
    capCornerIndices.reserve(capCorners.size());
    for (std::size_t index = 0; index < capCorners.size(); ++index) {
        const auto capCornerIndexIt = cornerIndex_.find(capCorners[index]);
        if (capCornerIndexIt == cornerIndex_.end() ||
            !attributes_.copyDomainIndex(AttributeDomain::Corner, sourceCornerIndices[index], capCornerIndexIt->second)) {
            return rollback();
        }
        capCornerIndices.push_back(capCornerIndexIt->second);
    }

    for (std::size_t index = 0; index < sourceVertices.size(); ++index) {
        const std::size_t next = (index + 1) % sourceVertices.size();
        const FaceId sideFace = addFace({
            sourceVertices[index],
            sourceVertices[next],
            result.newVertices[next],
            result.newVertices[index]});
        if (!sideFace) {
            return rollback();
        }

        const auto sideFaceIndexIt = faceIndex_.find(sideFace);
        const std::vector<CornerId> sideCorners = faceCorners(sideFace);
        if (sideFaceIndexIt == faceIndex_.end() || sideCorners.size() != 4 ||
            !attributes_.copyDomainIndex(AttributeDomain::Face, sourceFaceIndex, sideFaceIndexIt->second)) {
            return rollback();
        }

        const auto side0 = cornerIndex_.find(sideCorners[0]);
        const auto side1 = cornerIndex_.find(sideCorners[1]);
        const auto side2 = cornerIndex_.find(sideCorners[2]);
        const auto side3 = cornerIndex_.find(sideCorners[3]);
        if (side0 == cornerIndex_.end() || side1 == cornerIndex_.end() ||
            side2 == cornerIndex_.end() || side3 == cornerIndex_.end()) {
            return rollback();
        }

        if (!attributes_.copyDomainIndex(AttributeDomain::Corner, sourceCornerIndices[index], side0->second) ||
            !attributes_.copyDomainIndex(AttributeDomain::Corner, sourceCornerIndices[next], side1->second) ||
            !attributes_.copyDomainIndex(AttributeDomain::Corner, capCornerIndices[next], side2->second) ||
            !attributes_.copyDomainIndex(AttributeDomain::Corner, capCornerIndices[index], side3->second)) {
            return rollback();
        }

        result.sideFaces.push_back(sideFace);
    }

    if (!removeFace(id, true) || !validate()) {
        return rollback();
    }

    return result;
}

} // namespace vortex
