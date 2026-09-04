#include "vortex/procedural/geometry_graph.hpp"

#include <type_traits>

namespace vortex {

DependencyNodeId GeometryGraph::addNode(GeometryNodePayload payload) {
    const DependencyNodeId id = graph_.addNode("geometry");
    nodes_.emplace(id, GeometryNode{id, payload});
    if (output_ == 0U) {
        output_ = id;
    }
    return id;
}

bool GeometryGraph::connect(const DependencyNodeId before, const DependencyNodeId after) {
    return nodes_.contains(before) && nodes_.contains(after) && graph_.addDependency(before, after);
}

bool GeometryGraph::setOutput(const DependencyNodeId nodeId) noexcept {
    if (!nodes_.contains(nodeId)) {
        return false;
    }
    output_ = nodeId;
    return true;
}

MeshEvaluationResult GeometryGraph::evaluate(const MeshBlock& source) const {
    if (nodes_.empty()) {
        return MeshEvaluator::evaluate(source);
    }
    const auto order = graph_.evaluationOrder();
    if (!order || output_ == 0U) {
        MeshEvaluationResult failed;
        failed.error = MeshEvaluationError::ModifierFailed;
        return failed;
    }

    std::vector<std::unique_ptr<MeshModifier>> owned;
    std::vector<const MeshModifier*> stack;
    for (const DependencyNodeId id : *order) {
        const auto nodeIt = nodes_.find(id);
        if (nodeIt == nodes_.end()) {
            continue;
        }
        std::unique_ptr<MeshModifier> modifier = std::visit(
            [](const auto& payload) -> std::unique_ptr<MeshModifier> {
                using Payload = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<Payload, GeometryTransformNode>) {
                    return std::make_unique<TransformModifier>(payload.translation, payload.rotationRadians, payload.scale);
                } else if constexpr (std::is_same_v<Payload, GeometryMirrorNode>) {
                    return std::make_unique<MirrorModifier>(payload.axis, payload.planeOffset, payload.weld);
                } else if constexpr (std::is_same_v<Payload, GeometryTriangulateNode>) {
                    return std::make_unique<TriangulateModifier>();
                } else {
                    return std::make_unique<SimpleDeformTwistModifier>(payload.radiansPerUnit, payload.originZ);
                }
            }, nodeIt->second.payload);
        stack.push_back(modifier.get());
        owned.push_back(std::move(modifier));
        if (id == output_) {
            break;
        }
    }
    return MeshEvaluator::evaluate(source, stack);
}

} // namespace vortex
