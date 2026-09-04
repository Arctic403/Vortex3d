#include "vortex/procedural/geometry_graph.hpp"

#include <algorithm>
#include <type_traits>
#include <unordered_set>
#include <vector>

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
    if (!nodes_.contains(before) || !nodes_.contains(after)) {
        return false;
    }

    const auto existingDependents = graph_.dependentsOf(before);
    const auto existingDependencies = graph_.dependenciesOf(after);
    if (std::find(existingDependents.begin(), existingDependents.end(), after) != existingDependents.end()) {
        return true;
    }

    // v0.2 GeometryGraph is deliberately a modifier-chain graph. Fan-in/fan-out would
    // imply branching merge semantics that do not exist yet, so reject them instead of
    // silently linearizing a DAG into the wrong geometry result.
    if (!existingDependents.empty() || !existingDependencies.empty()) {
        return false;
    }
    return graph_.addDependency(before, after);
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

    std::unordered_set<DependencyNodeId> required;
    std::vector<DependencyNodeId> pending{output_};
    while (!pending.empty()) {
        const DependencyNodeId current = pending.back();
        pending.pop_back();
        if (!required.insert(current).second) {
            continue;
        }
        const auto dependencies = graph_.dependenciesOf(current);
        pending.insert(pending.end(), dependencies.begin(), dependencies.end());
    }

    std::vector<std::unique_ptr<MeshModifier>> owned;
    std::vector<const MeshModifier*> stack;
    for (const DependencyNodeId id : *order) {
        if (!required.contains(id)) {
            continue;
        }
        const auto nodeIt = nodes_.find(id);
        if (nodeIt == nodes_.end()) {
            MeshEvaluationResult failed;
            failed.error = MeshEvaluationError::ModifierFailed;
            return failed;
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
