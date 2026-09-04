#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vortex {

using DependencyNodeId = std::uint32_t;

struct DependencyNode final {
    DependencyNodeId id = 0;
    std::string name;
    bool dirty = true;
};

class DependencyGraph final {
public:
    [[nodiscard]] DependencyNodeId addNode(std::string name);
    [[nodiscard]] bool addDependency(DependencyNodeId dependency, DependencyNodeId dependent);
    [[nodiscard]] bool removeDependency(DependencyNodeId dependency, DependencyNodeId dependent) noexcept;
    [[nodiscard]] bool markDirty(DependencyNodeId nodeId) noexcept;
    void markClean(DependencyNodeId nodeId) noexcept;
    void markAllClean() noexcept;

    [[nodiscard]] const DependencyNode* node(DependencyNodeId nodeId) const noexcept;
    [[nodiscard]] std::vector<DependencyNodeId> dependenciesOf(DependencyNodeId nodeId) const;
    [[nodiscard]] std::vector<DependencyNodeId> dependentsOf(DependencyNodeId nodeId) const;
    [[nodiscard]] std::optional<std::vector<DependencyNodeId>> evaluationOrder() const;
    [[nodiscard]] std::vector<DependencyNodeId> dirtyEvaluationOrder() const;
    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodes_.size(); }

private:
    [[nodiscard]] bool reaches(DependencyNodeId from, DependencyNodeId target) const;

    DependencyNodeId nextId_ = 1;
    std::unordered_map<DependencyNodeId, DependencyNode> nodes_;
    std::unordered_map<DependencyNodeId, std::unordered_set<DependencyNodeId>> outgoing_;
    std::unordered_map<DependencyNodeId, std::unordered_set<DependencyNodeId>> incoming_;
};

} // namespace vortex
