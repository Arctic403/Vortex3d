#pragma once

#include "vortex/eval/evaluator.hpp"
#include "vortex/graph/dependency_graph.hpp"

#include <memory>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vortex {

struct GeometryTransformNode final { Vec3 translation{}; Vec3 rotationRadians{}; Vec3 scale{1.0F, 1.0F, 1.0F}; };
struct GeometryMirrorNode final { MirrorAxis axis = MirrorAxis::X; float planeOffset = 0.0F; MirrorWeldSettings weld{}; };
struct GeometryTriangulateNode final {};
struct GeometryTwistNode final { float radiansPerUnit = 0.0F; float originZ = 0.0F; };

using GeometryNodePayload = std::variant<GeometryTransformNode, GeometryMirrorNode, GeometryTriangulateNode, GeometryTwistNode>;

struct GeometryNode final {
    DependencyNodeId id = 0;
    GeometryNodePayload payload;
};

class GeometryGraph final {
public:
    [[nodiscard]] DependencyNodeId addNode(GeometryNodePayload payload);
    [[nodiscard]] bool connect(DependencyNodeId before, DependencyNodeId after);
    [[nodiscard]] bool setOutput(DependencyNodeId nodeId) noexcept;
    [[nodiscard]] DependencyNodeId output() const noexcept { return output_; }
    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodes_.size(); }
    [[nodiscard]] MeshEvaluationResult evaluate(const MeshBlock& source) const;

private:
    DependencyGraph graph_;
    std::unordered_map<DependencyNodeId, GeometryNode> nodes_;
    DependencyNodeId output_ = 0;
};

} // namespace vortex
