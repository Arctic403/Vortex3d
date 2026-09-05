#include "vulkan_viewport.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vortex::android {
namespace {

struct EdgeKey final {
    std::uint64_t a = 0U;
    std::uint64_t b = 0U;

    [[nodiscard]] bool operator==(const EdgeKey&) const noexcept = default;
};

struct EdgeKeyHash final {
    [[nodiscard]] std::size_t operator()(const EdgeKey& key) const noexcept {
        std::size_t value = std::hash<std::uint64_t>{}(key.a);
        value ^= std::hash<std::uint64_t>{}(key.b) + std::size_t{0x9e3779b9U} +
                 (value << 6U) + (value >> 2U);
        return value;
    }
};

struct EdgeInfo final {
    std::array<float, 3> a{};
    std::array<float, 3> b{};
    vortex::FaceId firstFace;
    std::uint32_t occurrences = 0U;
    bool crossesFaces = false;
};

[[nodiscard]] EdgeKey edgeKey(const vortex::VertexId a, const vortex::VertexId b) noexcept {
    return a.value() <= b.value() ? EdgeKey{a.value(), b.value()} : EdgeKey{b.value(), a.value()};
}

void addLine(
    std::vector<ViewportVertex>& output,
    const std::array<float, 3>& a,
    const std::array<float, 3>& b,
    const std::array<float, 3>& color) {
    output.push_back(ViewportVertex{a, color});
    output.push_back(ViewportVertex{b, color});
}

[[nodiscard]] bool buildSelectionOverlay(
    const ViewportObjectSnapshot& object,
    std::vector<ViewportVertex>& output) {
    const vortex::ViewportMesh& mesh = object.mesh;
    std::unordered_map<EdgeKey, EdgeInfo, EdgeKeyHash> edges;
    edges.reserve(mesh.triangles.size() * 3U);

    for (const vortex::ViewportTriangle& triangle : mesh.triangles) {
        if (triangle.a >= mesh.vertices.size() ||
            triangle.b >= mesh.vertices.size() ||
            triangle.c >= mesh.vertices.size()) {
            return false;
        }

        const std::array<std::uint32_t, 3> indices{triangle.a, triangle.b, triangle.c};
        for (std::size_t edge = 0; edge < indices.size(); ++edge) {
            const std::uint32_t firstIndex = indices[edge];
            const std::uint32_t secondIndex = indices[(edge + 1U) % indices.size()];
            const vortex::VertexId firstSource = mesh.vertices[firstIndex].sourceVertex;
            const vortex::VertexId secondSource = mesh.vertices[secondIndex].sourceVertex;
            if (!firstSource || !secondSource || firstSource == secondSource) {
                continue;
            }

            const EdgeKey key = edgeKey(firstSource, secondSource);
            auto [iterator, inserted] = edges.try_emplace(key);
            EdgeInfo& info = iterator->second;
            if (inserted) {
                const Vec3& a = mesh.vertices[firstIndex].position;
                const Vec3& b = mesh.vertices[secondIndex].position;
                info.a = {a.x, a.y, a.z};
                info.b = {b.x, b.y, b.z};
                info.firstFace = triangle.sourceFace;
            } else if (triangle.sourceFace != info.firstFace) {
                info.crossesFaces = true;
            }
            ++info.occurrences;
        }
    }

    constexpr std::array<float, 3> selectedColor{1.0F, 0.55F, 0.08F};
    output.clear();
    output.reserve(edges.size() * 2U + 12U);
    for (const auto& [key, info] : edges) {
        (void)key;
        if (info.occurrences == 1U || info.crossesFaces) {
            addLine(output, info.a, info.b, selectedColor);
        }
    }

    const auto& o = object.origin;
    constexpr float marker = 0.10F;
    constexpr float axisLength = 1.35F;
    addLine(output, {o[0] - marker, o[1], o[2]}, {o[0] + marker, o[1], o[2]}, {1.0F, 0.85F, 0.18F});
    addLine(output, {o[0], o[1] - marker, o[2]}, {o[0], o[1] + marker, o[2]}, {1.0F, 0.85F, 0.18F});
    addLine(output, {o[0], o[1], o[2] - marker}, {o[0], o[1], o[2] + marker}, {1.0F, 0.85F, 0.18F});
    addLine(output, o, {o[0] + axisLength, o[1], o[2]}, {0.95F, 0.16F, 0.14F});
    addLine(output, o, {o[0], o[1] + axisLength, o[2]}, {0.18F, 0.92F, 0.28F});
    addLine(output, o, {o[0], o[1], o[2] + axisLength}, {0.18F, 0.42F, 1.0F});
    return !output.empty();
}

} // namespace

bool VulkanViewport::setViewportObjects(const std::vector<ViewportObjectSnapshot>& objects) {
    if (device_ != VK_NULL_HANDLE) {
        return fail("Stage 5B render list must be supplied before Vulkan device creation");
    }
    if (objects.empty()) {
        return fail("Stage 5B render list contains no visible objects");
    }

    std::size_t totalVertices = 0U;
    std::size_t totalTriangles = 0U;
    for (const ViewportObjectSnapshot& object : objects) {
        totalVertices += object.mesh.vertices.size();
        totalTriangles += object.mesh.triangles.size();
    }

    vortex::ViewportMesh combined;
    combined.sourceDocumentRuntimeId = objects.front().mesh.sourceDocumentRuntimeId;
    combined.vertices.reserve(totalVertices);
    combined.triangles.reserve(totalTriangles);

    pickMap_.clear();
    selectionOverlays_.clear();
    selectionOverlayCapacity_ = 0U;
    std::unordered_set<std::uint64_t> objectIds;
    objectIds.reserve(objects.size());

    std::uint64_t nextSyntheticVertex = 1U;
    std::uint64_t nextSyntheticFace = 1U;

    for (const ViewportObjectSnapshot& object : objects) {
        if (!object.objectId || object.mesh.vertices.empty() || object.mesh.triangles.empty() ||
            !objectIds.insert(object.objectId.value()).second) {
            return fail("Stage 5B render list contains an invalid or duplicate object");
        }

        const std::uint32_t baseVertex = static_cast<std::uint32_t>(combined.vertices.size());
        std::unordered_map<std::uint64_t, std::uint64_t> vertexIds;
        std::unordered_map<std::uint64_t, std::uint64_t> faceIds;
        vertexIds.reserve(object.mesh.vertices.size());
        faceIds.reserve(object.mesh.triangles.size());

        for (const vortex::ViewportVertex& source : object.mesh.vertices) {
            if (!source.sourceVertex) {
                return fail("Stage 5B snapshot contains a vertex without stable source identity");
            }
            auto [iterator, inserted] = vertexIds.try_emplace(source.sourceVertex.value(), 0U);
            if (inserted) {
                iterator->second = nextSyntheticVertex++;
            }
            vortex::ViewportVertex copy = source;
            copy.sourceVertex = vortex::VertexId{iterator->second};
            combined.vertices.push_back(copy);
        }

        for (const vortex::ViewportTriangle& triangle : object.mesh.triangles) {
            if (triangle.a >= object.mesh.vertices.size() ||
                triangle.b >= object.mesh.vertices.size() ||
                triangle.c >= object.mesh.vertices.size() || !triangle.sourceFace) {
                return fail("Stage 5B snapshot contains invalid triangle identity or indices");
            }

            auto [iterator, inserted] = faceIds.try_emplace(triangle.sourceFace.value(), 0U);
            if (inserted) {
                iterator->second = nextSyntheticFace++;
                pickMap_.push_back(PickMapEntry{
                    vortex::FaceId{iterator->second},
                    ViewportPick{object.objectId, triangle.sourceFace},
                });
            }
            combined.triangles.push_back(vortex::ViewportTriangle{
                baseVertex + triangle.a,
                baseVertex + triangle.b,
                baseVertex + triangle.c,
                vortex::FaceId{iterator->second},
            });
        }

        SelectionOverlay overlay;
        overlay.objectId = object.objectId;
        if (!buildSelectionOverlay(object, overlay.vertices)) {
            return fail("Stage 5B failed to build an object selection overlay");
        }
        selectionOverlayCapacity_ = std::max(selectionOverlayCapacity_, overlay.vertices.size());
        selectionOverlays_.push_back(std::move(overlay));
    }

    if (!setViewportMesh(combined)) {
        return false;
    }

    // Stage 5A produced one overlay for the combined batch. Replace that CPU payload with
    // capacity for the largest per-object overlay. The host-visible GPU buffer is allocated
    // once at attach time; selection changes upload into it only after the frame fence.
    selectionOverlayVertices_.assign(selectionOverlayCapacity_, ViewportVertex{});
    selectionVertexCount_ = 0U;
    selectionVisible_ = false;
    selectedObject_ = {};
    selectionOverlayDirty_ = false;
    commandBuffersDirty_ = true;
    return !pickMap_.empty() && !selectionOverlays_.empty();
}

std::optional<ViewportPick> VulkanViewport::pickObject(
    const float xPixels,
    const float yPixels) const noexcept {
    const auto syntheticFace = pickFace(xPixels, yPixels);
    if (!syntheticFace) {
        return std::nullopt;
    }
    for (const PickMapEntry& entry : pickMap_) {
        if (entry.syntheticFace == *syntheticFace) {
            return entry.stablePick;
        }
    }
    return std::nullopt;
}

bool VulkanViewport::setSelectedObject(const vortex::ObjectId objectId) noexcept {
    if (objectId) {
        const auto iterator = std::find_if(
            selectionOverlays_.begin(),
            selectionOverlays_.end(),
            [objectId](const SelectionOverlay& overlay) { return overlay.objectId == objectId; });
        if (iterator == selectionOverlays_.end()) {
            return false;
        }
    }
    if (selectedObject_ == objectId) {
        return true;
    }
    selectedObject_ = objectId;
    selectionOverlayDirty_ = true;
    commandBuffersDirty_ = true;
    return true;
}

bool VulkanViewport::refreshSelectionOverlay() {
    if (!selectionOverlayDirty_) {
        return true;
    }

    if (!selectedObject_) {
        selectionVertexCount_ = 0U;
        selectionVisible_ = false;
        selectionOverlayDirty_ = false;
        return true;
    }

    const auto iterator = std::find_if(
        selectionOverlays_.begin(),
        selectionOverlays_.end(),
        [this](const SelectionOverlay& overlay) { return overlay.objectId == selectedObject_; });
    if (iterator == selectionOverlays_.end() || iterator->vertices.empty() ||
        iterator->vertices.size() > selectionOverlayCapacity_ ||
        selectionVertexMemory_ == VK_NULL_HANDLE) {
        return false;
    }

    const VkDeviceSize bytes = static_cast<VkDeviceSize>(iterator->vertices.size() * sizeof(ViewportVertex));
    void* mapped = nullptr;
    const VkResult result = vkMapMemory(device_, selectionVertexMemory_, 0U, bytes, 0U, &mapped);
    if (result != VK_SUCCESS) {
        return failVk("vkMapMemory(active selection overlay)", result);
    }
    std::memcpy(mapped, iterator->vertices.data(), static_cast<std::size_t>(bytes));
    vkUnmapMemory(device_, selectionVertexMemory_);

    selectionVertexCount_ = static_cast<std::uint32_t>(iterator->vertices.size());
    selectionVisible_ = true;
    selectionOverlayDirty_ = false;
    return true;
}

} // namespace vortex::android
