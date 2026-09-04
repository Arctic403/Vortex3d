#include "vortex/editor/operator.hpp"

#include <vector>

namespace vortex {

OperatorResult ExtrudeFaceOperator::execute(EditorContext& context) {
    if (context.mode() != EditorMode::Edit || !context.activeMesh() || context.selection().faces.size() != 1U) {
        return {OperatorStatus::InvalidContext, {}};
    }

    const FaceId faceId = *context.selection().faces.begin();
    ExtrudeFaceCommand command{faceId, offset_};
    MeshCommandResult meshResult;
    if (!context.history().executeMesh(context.document(), context.activeMesh(), command, &meshResult)) {
        return {OperatorStatus::Failed, {}};
    }
    if (meshResult.extrusion) {
        context.selection().clear();
        context.selection().faces.insert(meshResult.extrusion->capFace);
    }
    return {OperatorStatus::Finished, std::move(meshResult)};
}

OperatorResult TranslateSelectionOperator::execute(EditorContext& context) {
    if (context.mode() != EditorMode::Edit || !context.activeMesh() || context.selection().vertices.empty()) {
        return {OperatorStatus::InvalidContext, {}};
    }

    const EditableMesh* mesh = context.document().authoredMesh(context.activeMesh());
    if (mesh == nullptr) {
        return {OperatorStatus::InvalidContext, {}};
    }

    std::vector<VertexPositionTarget> targets;
    targets.reserve(context.selection().vertices.size());
    for (const VertexId vertexId : context.selection().vertices) {
        const auto position = mesh->position(vertexId);
        if (!position) {
            return {OperatorStatus::Failed, {}};
        }
        targets.push_back({vertexId, {position->x + offset_.x, position->y + offset_.y, position->z + offset_.z}});
    }

    MoveVerticesCommand command{std::move(targets)};
    MeshCommandResult meshResult;
    if (!context.history().executeMesh(context.document(), context.activeMesh(), command, &meshResult)) {
        return {OperatorStatus::Failed, {}};
    }
    return {OperatorStatus::Finished, std::move(meshResult)};
}

} // namespace vortex
