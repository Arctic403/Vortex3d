#include "vortex/mesh/editable_mesh.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vortex {
namespace {

struct UndirectedEdgeKey final {
    std::uint64_t low = 0;
    std::uint64_t high = 0;

    [[nodiscard]] bool operator==(const UndirectedEdgeKey&) const noexcept = default;
};

struct UndirectedEdgeKeyHash final {
    [[nodiscard]] std::size_t operator()(const UndirectedEdgeKey& key) const noexcept {
        const auto fold = [](const std::uint64_t value) noexcept {
            return static_cast<std::size_t>(value ^ (value >> 32U));
        };
        std::size_t hash = fold(key.low);
        hash ^= fold(key.high) + std::size_t{0x9e3779b9U} + (hash << 6U) + (hash >> 2U);
        return hash;
    }
};

[[nodiscard]] UndirectedEdgeKey edgeKey(const VertexId a, const VertexId b) noexcept {
    const std::uint64_t first = a.value();
    const std::uint64_t second = b.value();
    return first < second ? UndirectedEdgeKey{first, second} : UndirectedEdgeKey{second, first};
}

} // namespace

MeshValidationResult EditableMesh::validateStrict() const {
    MeshValidationResult result;
    const auto issue = [&result](const MeshValidationCode code, const std::uint64_t id, std::string message) {
        result.issues.push_back(MeshValidationIssue{code, id, std::move(message)});
    };

    const auto validateDomainStorage = [&issue](
                                           const auto& order,
                                           const auto& indexMap,
                                           const auto& registry,
                                           const char* domainName) {
        using Order = std::decay_t<decltype(order)>;
        using IdType = typename Order::value_type;

        if (order.size() != indexMap.size() || order.size() != registry.size()) {
            issue(
                MeshValidationCode::StorageSizeMismatch,
                0,
                std::string(domainName) + " order/index/registry sizes disagree");
        }

        std::unordered_set<IdType, IdHash<IdType>> seen;
        seen.reserve(order.size());
        for (std::size_t packedIndex = 0; packedIndex < order.size(); ++packedIndex) {
            const IdType id = order[packedIndex];
            if (!id) {
                issue(
                    MeshValidationCode::MissingElement,
                    0,
                    std::string(domainName) + " order contains an invalid zero ID");
                continue;
            }
            if (!seen.insert(id).second) {
                issue(
                    MeshValidationCode::DuplicateElementId,
                    id.value(),
                    std::string(domainName) + " order contains a duplicate stable ID");
            }

            const auto indexIt = indexMap.find(id);
            if (indexIt == indexMap.end() || indexIt->second != packedIndex) {
                issue(
                    MeshValidationCode::IndexMapMismatch,
                    id.value(),
                    std::string(domainName) + " packed index map does not match order storage");
            }

            const auto registryIt = registry.find(id);
            if (registryIt == registry.end()) {
                issue(
                    MeshValidationCode::MissingElement,
                    id.value(),
                    std::string(domainName) + " order references an element missing from its registry");
            } else if (registryIt->second.id != id) {
                issue(
                    MeshValidationCode::ElementIdentityMismatch,
                    id.value(),
                    std::string(domainName) + " registry record ID does not match its key");
            }
        }

        for (const auto& [id, packedIndex] : indexMap) {
            if (!id || packedIndex >= order.size() || order[packedIndex] != id || !registry.contains(id)) {
                issue(
                    MeshValidationCode::IndexMapMismatch,
                    id.value(),
                    std::string(domainName) + " index entry does not resolve to the same ordered registry element");
            }
        }

        for (const auto& [id, record] : registry) {
            if (!id || record.id != id) {
                issue(
                    MeshValidationCode::ElementIdentityMismatch,
                    id.value(),
                    std::string(domainName) + " registry key and stored record identity disagree");
            }
            if (!indexMap.contains(id)) {
                issue(
                    MeshValidationCode::IndexMapMismatch,
                    id.value(),
                    std::string(domainName) + " registry element has no packed-index entry");
            }
        }
    };

    validateDomainStorage(vertexOrder_, vertexIndex_, vertices_, "Vertex");
    validateDomainStorage(edgeOrder_, edgeIndex_, edges_, "Edge");
    validateDomainStorage(faceOrder_, faceIndex_, faces_, "Face");
    validateDomainStorage(cornerOrder_, cornerIndex_, corners_, "Corner");

    std::uint64_t maximumLiveId = 0;
    const auto considerOrder = [&maximumLiveId](const auto& order) {
        for (const auto id : order) {
            maximumLiveId = std::max(maximumLiveId, id.value());
        }
    };
    const auto considerRegistry = [&maximumLiveId](const auto& registry) {
        for (const auto& [id, record] : registry) {
            (void)record;
            maximumLiveId = std::max(maximumLiveId, id.value());
        }
    };
    considerOrder(vertexOrder_);
    considerOrder(edgeOrder_);
    considerOrder(faceOrder_);
    considerOrder(cornerOrder_);
    considerRegistry(vertices_);
    considerRegistry(edges_);
    considerRegistry(faces_);
    considerRegistry(corners_);
    if (nextElementId_ == 0U || nextElementId_ <= maximumLiveId) {
        issue(
            MeshValidationCode::InvalidAllocatorState,
            nextElementId_,
            "Next element ID is not strictly above every live stable ID");
    }

    std::unordered_map<UndirectedEdgeKey, EdgeId, UndirectedEdgeKeyHash> edgeLookup;
    edgeLookup.reserve(edges_.size());
    for (const auto& [edgeId, edgeData] : edges_) {
        if (!edgeData.vertexA || !edgeData.vertexB || edgeData.vertexA == edgeData.vertexB) {
            continue;
        }
        const auto [existing, inserted] = edgeLookup.emplace(edgeKey(edgeData.vertexA, edgeData.vertexB), edgeId);
        if (!inserted && existing->second != edgeId) {
            issue(
                MeshValidationCode::DuplicateEdge,
                edgeId.value(),
                "Multiple authored edges connect the same unordered vertex pair");
        }
    }

    // The legacy topology validator did not detect an early short cycle when cornerCount happened
    // to be a multiple of that short cycle length. Strict validation requires every face step to
    // visit a distinct corner before returning to the first corner.
    for (const FaceId faceId : faceOrder_) {
        const auto faceIt = faces_.find(faceId);
        if (faceIt == faces_.end() || faceIt->second.cornerCount < 3U || !faceIt->second.firstCorner) {
            continue;
        }

        const MeshFace& faceData = faceIt->second;
        std::unordered_set<CornerId, IdHash<CornerId>> visited;
        visited.reserve(faceData.cornerCount);
        CornerId cursor = faceData.firstCorner;
        bool broken = false;
        for (std::uint32_t step = 0; step < faceData.cornerCount; ++step) {
            const auto cornerIt = corners_.find(cursor);
            if (cornerIt == corners_.end()) {
                break;
            }
            if (!visited.insert(cursor).second) {
                issue(
                    MeshValidationCode::BrokenFaceCycle,
                    faceId.value(),
                    "Face cycle repeats a corner before cornerCount distinct corners are visited");
                broken = true;
                break;
            }
            cursor = cornerIt->second.next;
        }
        if (!broken && visited.size() == faceData.cornerCount && cursor != faceData.firstCorner) {
            issue(
                MeshValidationCode::BrokenFaceCycle,
                faceId.value(),
                "Strict face cycle did not close on its first corner");
        }
    }

    MeshValidationResult topology = validate();
    result.issues.insert(
        result.issues.end(),
        std::make_move_iterator(topology.issues.begin()),
        std::make_move_iterator(topology.issues.end()));
    return result;
}

} // namespace vortex
