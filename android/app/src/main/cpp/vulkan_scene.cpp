#include "vulkan_viewport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
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

[[nodiscard]] bool finiteMatrix(const vortex::TransformMatrix& matrix) noexcept {
    return std::all_of(
        matrix.values.begin(),
        matrix.values.end(),
        [](const float value) { return std::isfinite(value); });
}

[[nodiscard]] std::array<float, 3> toArray(const vortex::Vec3 value) noexcept {
    return {value.x, value.y, value.z};
}

[[nodiscard]] vortex::Vec3 toVec3(const std::array<float, 3>& value) noexcept {
    return {value[0], value[1], value[2]};
}

[[nodiscard]] bool transformedPoint(
    const vortex::TransformMatrix& matrix,
    const vortex::Vec3 local,
    std::array<float, 3>& world) noexcept {
    const vortex::Vec3 transformed = vortex::transformPoint(matrix, local);
    if (!std::isfinite(transformed.x) ||
        !std::isfinite(transformed.y) ||
        !std::isfinite(transformed.z)) {
        return false;
    }
    world = toArray(transformed);
    return true;
}

[[nodiscard]] ViewportVertex viewportVertex(const vortex::ViewportVertex& source) noexcept {
    const vortex::Vec3& n = source.normal;
    return ViewportVertex{
        {source.position.x, source.position.y, source.position.z},
        {
            0.25F + 0.65F * (n.x * 0.5F + 0.5F),
            0.25F + 0.65F * (n.y * 0.5F + 0.5F),
            0.25F + 0.65F * (n.z * 0.5F + 0.5F),
        },
    };
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
                info.a = toArray(mesh.vertices[firstIndex].position);
                info.b = toArray(mesh.vertices[secondIndex].position);
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

    // The transform gizmo remains object-local for Phase 6. All Move/Rotate/Scale previews
    // update only this derived world matrix; authored state stays untouched until commit.
    constexpr std::array<float, 3> origin{0.0F, 0.0F, 0.0F};
    constexpr float marker = 0.10F;
    constexpr float axisLength = 1.35F;
    addLine(output, {-marker, 0.0F, 0.0F}, {marker, 0.0F, 0.0F}, {1.0F, 0.85F, 0.18F});
    addLine(output, {0.0F, -marker, 0.0F}, {0.0F, marker, 0.0F}, {1.0F, 0.85F, 0.18F});
    addLine(output, {0.0F, 0.0F, -marker}, {0.0F, 0.0F, marker}, {1.0F, 0.85F, 0.18F});
    addLine(output, origin, {axisLength, 0.0F, 0.0F}, {0.95F, 0.16F, 0.14F});
    addLine(output, origin, {0.0F, axisLength, 0.0F}, {0.18F, 0.92F, 0.28F});
    addLine(output, origin, {0.0F, 0.0F, axisLength}, {0.18F, 0.42F, 1.0F});
    return !output.empty();
}

} // namespace

bool VulkanViewport::setViewportObjects(const std::vector<ViewportObjectSnapshot>& objects) {
    if (device_ != VK_NULL_HANDLE) {
        return fail("Phase 6 render list must be supplied before Vulkan device creation");
    }
    if (objects.empty()) {
        return fail("Phase 6 render list contains no visible objects");
    }

    const vortex::RuntimeDocumentId sourceDocument = objects.front().mesh.sourceDocumentRuntimeId;
    if (!sourceDocument) {
        return fail("Phase 6 render list is missing source document identity");
    }

    constexpr std::size_t maxVertexCount =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    constexpr std::size_t maxIndexCount = maxVertexCount;
    std::size_t totalVertices = 0U;
    std::size_t totalIndices = 0U;
    for (const ViewportObjectSnapshot& object : objects) {
        if (!object.mesh.sourceMeshId ||
            object.mesh.sourceDocumentRuntimeId != sourceDocument ||
            !finiteMatrix(object.worldMatrix)) {
            return fail("Phase 6 render list contains inconsistent source identity or world transform");
        }
        if (object.mesh.vertices.size() > maxVertexCount - totalVertices ||
            object.mesh.triangles.size() > (maxIndexCount - totalIndices) / 3U) {
            return fail("Phase 6 render list exceeds 32-bit Vulkan index capacity");
        }
        totalVertices += object.mesh.vertices.size();
        totalIndices += object.mesh.triangles.size() * 3U;
    }

    sceneVertices_.clear();
    sceneIndices_.clear();
    pickTriangles_.clear();
    pickMap_.clear();
    sceneDrawRanges_.clear();
    selectionOverlays_.clear();
    selectionOverlayCapacity_ = 0U;

    sceneVertices_.reserve(totalVertices);
    sceneIndices_.reserve(totalIndices);
    pickTriangles_.reserve(totalIndices / 3U);
    sceneDrawRanges_.reserve(objects.size());
    selectionOverlays_.reserve(objects.size());

    std::unordered_set<std::uint64_t> objectIds;
    objectIds.reserve(objects.size());
    std::uint64_t nextSyntheticFace = 1U;

    for (const ViewportObjectSnapshot& object : objects) {
        if (!object.objectId || object.mesh.vertices.empty() || object.mesh.triangles.empty() ||
            !objectIds.insert(object.objectId.value()).second) {
            return fail("Phase 6 render list contains an invalid or duplicate object");
        }

        const std::uint32_t baseVertex = static_cast<std::uint32_t>(sceneVertices_.size());
        const std::uint32_t firstIndex = static_cast<std::uint32_t>(sceneIndices_.size());
        const std::uint32_t firstPickTriangle = static_cast<std::uint32_t>(pickTriangles_.size());
        for (const vortex::ViewportVertex& source : object.mesh.vertices) {
            if (!source.sourceVertex) {
                return fail("Phase 6 snapshot contains a vertex without stable source identity");
            }
            sceneVertices_.push_back(viewportVertex(source));
        }

        std::unordered_map<std::uint64_t, std::uint64_t> faceIds;
        faceIds.reserve(object.mesh.triangles.size());
        for (const vortex::ViewportTriangle& triangle : object.mesh.triangles) {
            if (triangle.a >= object.mesh.vertices.size() ||
                triangle.b >= object.mesh.vertices.size() ||
                triangle.c >= object.mesh.vertices.size() ||
                !triangle.sourceFace) {
                return fail("Phase 6 snapshot contains invalid triangle identity or indices");
            }

            auto [iterator, inserted] = faceIds.try_emplace(triangle.sourceFace.value(), 0U);
            if (inserted) {
                iterator->second = nextSyntheticFace++;
                pickMap_.push_back(PickMapEntry{
                    vortex::FaceId{iterator->second},
                    ViewportPick{object.objectId, triangle.sourceFace},
                });
            }
            const vortex::FaceId syntheticFace{iterator->second};

            sceneIndices_.push_back(baseVertex + triangle.a);
            sceneIndices_.push_back(baseVertex + triangle.b);
            sceneIndices_.push_back(baseVertex + triangle.c);

            const std::array<float, 3> localA = toArray(object.mesh.vertices[triangle.a].position);
            const std::array<float, 3> localB = toArray(object.mesh.vertices[triangle.b].position);
            const std::array<float, 3> localC = toArray(object.mesh.vertices[triangle.c].position);
            std::array<float, 3> a{};
            std::array<float, 3> b{};
            std::array<float, 3> c{};
            if (!transformedPoint(object.worldMatrix, toVec3(localA), a) ||
                !transformedPoint(object.worldMatrix, toVec3(localB), b) ||
                !transformedPoint(object.worldMatrix, toVec3(localC), c)) {
                return fail("Phase 6 world transform produced non-finite picking geometry");
            }
            pickTriangles_.push_back(PickTriangle{localA, localB, localC, a, b, c, syntheticFace});
        }

        const std::uint32_t indexCount =
            static_cast<std::uint32_t>(sceneIndices_.size()) - firstIndex;
        const std::uint32_t pickTriangleCount =
            static_cast<std::uint32_t>(pickTriangles_.size()) - firstPickTriangle;
        sceneDrawRanges_.push_back(SceneDrawRange{
            object.objectId,
            firstIndex,
            indexCount,
            firstPickTriangle,
            pickTriangleCount,
            object.worldMatrix,
        });

        SelectionOverlay overlay;
        overlay.objectId = object.objectId;
        overlay.worldMatrix = object.worldMatrix;
        if (!buildSelectionOverlay(object, overlay.vertices)) {
            return fail("Phase 6 failed to build an object selection overlay");
        }
        selectionOverlayCapacity_ = std::max(selectionOverlayCapacity_, overlay.vertices.size());
        selectionOverlays_.push_back(std::move(overlay));
    }

    if (sceneVertices_.empty() || sceneIndices_.empty() || sceneDrawRanges_.empty() ||
        pickTriangles_.empty() || pickMap_.empty() || selectionOverlays_.empty()) {
        return fail("Phase 6 render list did not produce complete derived scene state");
    }

    // Allocate enough host-visible overlay storage for whichever object becomes active.
    selectionOverlayVertices_.assign(selectionOverlayCapacity_, ViewportVertex{});
    selectionVertexCount_ = 0U;
    selectionVisible_ = false;
    selectedObject_ = {};
    selectionWorldMatrix_ = vortex::identityTransformMatrix();
    selectionOverlayDirty_ = false;
    commandBuffersDirty_ = true;
    return true;
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

bool VulkanViewport::updateObjectWorldMatrix(
    const vortex::ObjectId objectId,
    const vortex::TransformMatrix& worldMatrix) noexcept {
    if (!objectId || !finiteMatrix(worldMatrix)) {
        return false;
    }

    auto draw = std::find_if(
        sceneDrawRanges_.begin(),
        sceneDrawRanges_.end(),
        [objectId](const SceneDrawRange& value) { return value.objectId == objectId; });
    auto overlay = std::find_if(
        selectionOverlays_.begin(),
        selectionOverlays_.end(),
        [objectId](const SelectionOverlay& value) { return value.objectId == objectId; });
    if (draw == sceneDrawRanges_.end() || overlay == selectionOverlays_.end() ||
        static_cast<std::size_t>(draw->firstPickTriangle) + draw->pickTriangleCount > pickTriangles_.size()) {
        return false;
    }

    std::vector<std::array<float, 3>> transformed;
    transformed.reserve(static_cast<std::size_t>(draw->pickTriangleCount) * 3U);
    for (std::uint32_t offset = 0U; offset < draw->pickTriangleCount; ++offset) {
        const PickTriangle& triangle = pickTriangles_[draw->firstPickTriangle + offset];
        std::array<float, 3> a{};
        std::array<float, 3> b{};
        std::array<float, 3> c{};
        if (!transformedPoint(worldMatrix, toVec3(triangle.localA), a) ||
            !transformedPoint(worldMatrix, toVec3(triangle.localB), b) ||
            !transformedPoint(worldMatrix, toVec3(triangle.localC), c)) {
            return false;
        }
        transformed.push_back(a);
        transformed.push_back(b);
        transformed.push_back(c);
    }

    std::size_t transformedIndex = 0U;
    for (std::uint32_t offset = 0U; offset < draw->pickTriangleCount; ++offset) {
        PickTriangle& triangle = pickTriangles_[draw->firstPickTriangle + offset];
        triangle.a = transformed[transformedIndex++];
        triangle.b = transformed[transformedIndex++];
        triangle.c = transformed[transformedIndex++];
    }

    draw->worldMatrix = worldMatrix;
    overlay->worldMatrix = worldMatrix;
    if (selectedObject_ == objectId) {
        selectionWorldMatrix_ = worldMatrix;
    }
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
        selectionWorldMatrix_ = vortex::identityTransformMatrix();
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

    selectionWorldMatrix_ = iterator->worldMatrix;
    selectionVertexCount_ = static_cast<std::uint32_t>(iterator->vertices.size());
    selectionVisible_ = true;
    selectionOverlayDirty_ = false;
    return true;
}

} // namespace vortex::android
