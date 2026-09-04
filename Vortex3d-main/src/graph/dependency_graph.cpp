#include "vortex/graph/dependency_graph.hpp"

#include <algorithm>
#include <deque>

namespace vortex {

DependencyNodeId DependencyGraph::addNode(std::string name) {
    const DependencyNodeId id = nextId_++;
    nodes_.emplace(id, DependencyNode{id, std::move(name), true});
    return id;
}

bool DependencyGraph::reaches(const DependencyNodeId from, const DependencyNodeId target) const {
    if (from == target) {
        return true;
    }
    std::unordered_set<DependencyNodeId> visited;
    std::vector<DependencyNodeId> stack{from};
    while (!stack.empty()) {
        const DependencyNodeId current = stack.back();
        stack.pop_back();
        if (!visited.insert(current).second) {
            continue;
        }
        const auto it = outgoing_.find(current);
        if (it == outgoing_.end()) {
            continue;
        }
        for (const DependencyNodeId next : it->second) {
            if (next == target) {
                return true;
            }
            stack.push_back(next);
        }
    }
    return false;
}

bool DependencyGraph::addDependency(const DependencyNodeId dependency, const DependencyNodeId dependent) {
    if (dependency == dependent || !nodes_.contains(dependency) || !nodes_.contains(dependent)) {
        return false;
    }
    if (outgoing_[dependency].contains(dependent)) {
        return true;
    }
    if (reaches(dependent, dependency)) {
        return false;
    }
    outgoing_[dependency].insert(dependent);
    incoming_[dependent].insert(dependency);
    (void)markDirty(dependent);
    return true;
}

bool DependencyGraph::removeDependency(const DependencyNodeId dependency, const DependencyNodeId dependent) noexcept {
    const auto outIt = outgoing_.find(dependency);
    if (outIt == outgoing_.end() || outIt->second.erase(dependent) == 0U) {
        return false;
    }
    if (const auto inIt = incoming_.find(dependent); inIt != incoming_.end()) {
        inIt->second.erase(dependency);
    }
    (void)markDirty(dependent);
    return true;
}

bool DependencyGraph::markDirty(const DependencyNodeId nodeId) noexcept {
    if (!nodes_.contains(nodeId)) {
        return false;
    }

    std::unordered_set<DependencyNodeId> visited;
    std::vector<DependencyNodeId> stack{nodeId};
    while (!stack.empty()) {
        const DependencyNodeId current = stack.back();
        stack.pop_back();
        if (!visited.insert(current).second) {
            continue;
        }

        auto nodeIt = nodes_.find(current);
        if (nodeIt == nodes_.end()) {
            continue;
        }
        nodeIt->second.dirty = true;
        if (const auto outIt = outgoing_.find(current); outIt != outgoing_.end()) {
            for (const DependencyNodeId next : outIt->second) {
                stack.push_back(next);
            }
        }
    }
    return true;
}

void DependencyGraph::markClean(const DependencyNodeId nodeId) noexcept {
    if (const auto it = nodes_.find(nodeId); it != nodes_.end()) {
        it->second.dirty = false;
    }
}

void DependencyGraph::markAllClean() noexcept {
    for (auto& [id, nodeValue] : nodes_) {
        (void)id;
        nodeValue.dirty = false;
    }
}

const DependencyNode* DependencyGraph::node(const DependencyNodeId nodeId) const noexcept {
    const auto it = nodes_.find(nodeId);
    return it == nodes_.end() ? nullptr : &it->second;
}

std::vector<DependencyNodeId> DependencyGraph::dependenciesOf(const DependencyNodeId nodeId) const {
    std::vector<DependencyNodeId> result;
    if (const auto it = incoming_.find(nodeId); it != incoming_.end()) {
        result.assign(it->second.begin(), it->second.end());
        std::sort(result.begin(), result.end());
    }
    return result;
}

std::vector<DependencyNodeId> DependencyGraph::dependentsOf(const DependencyNodeId nodeId) const {
    std::vector<DependencyNodeId> result;
    if (const auto it = outgoing_.find(nodeId); it != outgoing_.end()) {
        result.assign(it->second.begin(), it->second.end());
        std::sort(result.begin(), result.end());
    }
    return result;
}

std::optional<std::vector<DependencyNodeId>> DependencyGraph::evaluationOrder() const {
    std::unordered_map<DependencyNodeId, std::size_t> indegree;
    for (const auto& [id, nodeValue] : nodes_) {
        (void)nodeValue;
        indegree[id] = 0;
    }
    for (const auto& [dependent, incoming] : incoming_) {
        indegree[dependent] = incoming.size();
    }

    std::vector<DependencyNodeId> ready;
    for (const auto& [id, degree] : indegree) {
        if (degree == 0U) {
            ready.push_back(id);
        }
    }
    std::sort(ready.begin(), ready.end(), std::greater<DependencyNodeId>{});

    std::vector<DependencyNodeId> order;
    order.reserve(nodes_.size());
    while (!ready.empty()) {
        const DependencyNodeId current = ready.back();
        ready.pop_back();
        order.push_back(current);
        if (const auto outIt = outgoing_.find(current); outIt != outgoing_.end()) {
            std::vector<DependencyNodeId> sorted(outIt->second.begin(), outIt->second.end());
            std::sort(sorted.begin(), sorted.end());
            for (const DependencyNodeId next : sorted) {
                auto degreeIt = indegree.find(next);
                if (degreeIt != indegree.end() && --degreeIt->second == 0U) {
                    ready.push_back(next);
                    std::sort(ready.begin(), ready.end(), std::greater<DependencyNodeId>{});
                }
            }
        }
    }
    if (order.size() != nodes_.size()) {
        return std::nullopt;
    }
    return order;
}

std::vector<DependencyNodeId> DependencyGraph::dirtyEvaluationOrder() const {
    std::vector<DependencyNodeId> result;
    const auto order = evaluationOrder();
    if (!order) {
        return result;
    }
    for (const DependencyNodeId id : *order) {
        const DependencyNode* nodeValue = node(id);
        if (nodeValue != nullptr && nodeValue->dirty) {
            result.push_back(id);
        }
    }
    return result;
}

} // namespace vortex
