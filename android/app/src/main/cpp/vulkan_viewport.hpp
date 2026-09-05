#pragma once

#include "vortex/core/transform.hpp"
#include "vortex/viewport/render_extract.hpp"

#include <android/native_window.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vortex::android {

struct ViewportVertex {
    std::array<float, 3> position{};
    std::array<float, 3> color{};
};
static_assert(sizeof(ViewportVertex) == sizeof(float) * 6U);

struct ViewportCamera final {
    float yawRadians = 0.6108652382F;
    float pitchRadians = -0.3490658504F;
    float distance = 7.0F;
    float panX = 0.0F;
    float panY = 0.0F;
    float fovYRadians = 1.0471975512F;
    float nearPlane = 0.1F;
    float farPlane = 100.0F;
};

struct CameraPushConstants final {
    std::array<float, 16> viewProjection{};
};
static_assert(sizeof(CameraPushConstants) == sizeof(float) * 16U);

// One visible editor object and its evaluated/extracted local-space render snapshot.
// The world matrix is derived from persistent engine Object state; Vulkan never authors it.
struct ViewportObjectSnapshot final {
    vortex::ObjectId objectId;
    vortex::ViewportMesh mesh;
    vortex::TransformMatrix worldMatrix;
};

struct ViewportPick final {
    vortex::ObjectId objectId;
    vortex::FaceId sourceFace;
};

enum class GizmoAxis : std::uint8_t {
    X = 0U,
    Y = 1U,
    Z = 2U,
};

enum class GizmoMode : std::uint8_t {
    Move = 0U,
    Rotate = 1U,
    Scale = 2U,
};

// Screen-space metadata captured when a touch hits the active gizmo control.
// pixelsPerWorldUnit is measured at the selected object's depth and gives the editor host
// a stable drag scale without giving Vulkan ownership of authored transforms.
struct GizmoHit final {
    GizmoAxis axis = GizmoAxis::X;
    float screenDirectionX = 0.0F;
    float screenDirectionY = 0.0F;
    float pixelsPerWorldUnit = 0.0F;
};

class VulkanViewport final {
public:
    VulkanViewport() = default;
    ~VulkanViewport();

    VulkanViewport(const VulkanViewport&) = delete;
    VulkanViewport& operator=(const VulkanViewport&) = delete;

    [[nodiscard]] bool setViewportObjects(const std::vector<ViewportObjectSnapshot>& objects);
    [[nodiscard]] std::optional<ViewportPick> pickObject(float xPixels, float yPixels) const noexcept;
    [[nodiscard]] bool setSelectedObject(vortex::ObjectId objectId) noexcept;
    [[nodiscard]] bool setGizmoMode(GizmoMode mode) noexcept;
    [[nodiscard]] bool updateObjectWorldMatrix(
        vortex::ObjectId objectId,
        const vortex::TransformMatrix& worldMatrix) noexcept;
    [[nodiscard]] std::optional<GizmoHit> hitTestGizmo(
        vortex::ObjectId objectId,
        GizmoMode mode,
        float xPixels,
        float yPixels) const noexcept;

    // Takes ownership of the ANativeWindow reference returned by ANativeWindow_fromSurface().
    [[nodiscard]] bool attach(ANativeWindow* window);
    [[nodiscard]] bool resize();
    void detach() noexcept;
    [[nodiscard]] bool render();

    // Touch deltas are expressed in Android view pixels. Camera state remains native-owned.
    [[nodiscard]] bool orbitCamera(float deltaXPixels, float deltaYPixels) noexcept;
    [[nodiscard]] bool panCamera(float deltaXPixels, float deltaYPixels) noexcept;
    [[nodiscard]] bool zoomCamera(float scaleFactor) noexcept;

    [[nodiscard]] std::string info() const;

private:
    // Fixed host-visible capacity keeps selection/gizmo refreshes allocation-free while a
    // selected object is active. The largest single solid gizmo mode fits comfortably below it.
    static constexpr std::size_t kGizmoVertexCapacity = 8192U;

    // Legacy single-snapshot helper remains private while Phase 6 uses per-object draw items.
    [[nodiscard]] bool setViewportMesh(const vortex::ViewportMesh& mesh);
    [[nodiscard]] std::optional<vortex::FaceId> pickFace(float xPixels, float yPixels) const noexcept;
    [[nodiscard]] bool setSelectionVisible(bool visible) noexcept;
    [[nodiscard]] std::vector<ViewportVertex> buildGizmoVertices() const;
    [[nodiscard]] vortex::TransformMatrix gizmoWorldMatrix(
        const vortex::TransformMatrix& objectWorldMatrix) const noexcept;

    struct PickTriangle final {
        PickTriangle() = default;
        PickTriangle(
            const std::array<float, 3>& worldA,
            const std::array<float, 3>& worldB,
            const std::array<float, 3>& worldC,
            const vortex::FaceId face) noexcept
            : localA(worldA), localB(worldB), localC(worldC),
              a(worldA), b(worldB), c(worldC), sourceFace(face) {}
        PickTriangle(
            const std::array<float, 3>& sourceA,
            const std::array<float, 3>& sourceB,
            const std::array<float, 3>& sourceC,
            const std::array<float, 3>& worldA,
            const std::array<float, 3>& worldB,
            const std::array<float, 3>& worldC,
            const vortex::FaceId face) noexcept
            : localA(sourceA), localB(sourceB), localC(sourceC),
              a(worldA), b(worldB), c(worldC), sourceFace(face) {}

        std::array<float, 3> localA{};
        std::array<float, 3> localB{};
        std::array<float, 3> localC{};
        std::array<float, 3> a{};
        std::array<float, 3> b{};
        std::array<float, 3> c{};
        vortex::FaceId sourceFace;
    };

    struct PickMapEntry final {
        vortex::FaceId syntheticFace;
        ViewportPick stablePick;
    };

    struct SceneDrawRange final {
        vortex::ObjectId objectId;
        std::uint32_t firstIndex = 0U;
        std::uint32_t indexCount = 0U;
        std::uint32_t firstPickTriangle = 0U;
        std::uint32_t pickTriangleCount = 0U;
        vortex::TransformMatrix worldMatrix;
    };

    struct SelectionOverlay final {
        vortex::ObjectId objectId;
        vortex::TransformMatrix worldMatrix;
        std::vector<ViewportVertex> vertices;
    };

    [[nodiscard]] bool createInstance();
    [[nodiscard]] bool createSurface();
    [[nodiscard]] bool createDeviceForSurface();
    [[nodiscard]] bool verifyPresentSupport();
    [[nodiscard]] bool createCommandPool();
    [[nodiscard]] bool createSyncObjects();
    [[nodiscard]] bool recreateSwapchain();
    [[nodiscard]] bool createSwapchain();
    [[nodiscard]] bool createDepthResources();
    [[nodiscard]] bool createGeometryResources();
    [[nodiscard]] bool createGridResources();
    [[nodiscard]] bool createGraphicsPipeline();
    [[nodiscard]] bool createGizmoPipeline();
    [[nodiscard]] bool recordCommandBuffers();
    [[nodiscard]] bool rebuildCommandBuffers();
    [[nodiscard]] bool refreshSelectionOverlay();
    [[nodiscard]] CameraPushConstants cameraPushConstants(float aspect) const noexcept;
    [[nodiscard]] CameraPushConstants objectPushConstants(
        float aspect,
        const vortex::TransformMatrix& worldMatrix) const noexcept;

    [[nodiscard]] bool createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& memory);
    [[nodiscard]] std::uint32_t findMemoryType(
        std::uint32_t typeBits,
        VkMemoryPropertyFlags properties) const;
    [[nodiscard]] VkFormat findDepthFormat() const;

    void destroySwapchain() noexcept;
    void destroyGeometry() noexcept;
    void destroySurface() noexcept;
    void shutdown() noexcept;

    [[nodiscard]] bool fail(std::string message);
    [[nodiscard]] bool failVk(std::string_view action, VkResult result);

    ANativeWindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties physicalProperties_{};
    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueFamily_ = UINT32_MAX;
    std::uint32_t presentQueueFamily_ = UINT32_MAX;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;

    // CPU-side data is fully rebuildable from RenderExtractor snapshots.
    std::vector<ViewportVertex> sceneVertices_;
    std::vector<std::uint32_t> sceneIndices_;
    std::vector<ViewportVertex> selectionOverlayVertices_;
    std::vector<PickTriangle> pickTriangles_;

    // Renderer metadata is derived only. Stable engine identity remains attached to every item.
    std::vector<PickMapEntry> pickMap_;
    std::vector<SceneDrawRange> sceneDrawRanges_;
    std::vector<SelectionOverlay> selectionOverlays_;
    std::size_t selectionOverlayCapacity_ = 0U;
    vortex::ObjectId selectedObject_;
    vortex::TransformMatrix selectionWorldMatrix_{};
    GizmoMode gizmoMode_ = GizmoMode::Move;
    bool selectionOverlayDirty_ = false;

    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory_ = VK_NULL_HANDLE;
    std::uint32_t indexCount_ = 0U;

    VkBuffer gridVertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory gridVertexMemory_ = VK_NULL_HANDLE;
    std::uint32_t gridVertexCount_ = 0U;

    VkBuffer selectionVertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory selectionVertexMemory_ = VK_NULL_HANDLE;
    std::uint32_t selectionVertexCount_ = 0U;
    std::uint32_t selectionOutlineVertexCount_ = 0U;
    std::uint32_t gizmoFirstVertex_ = 0U;
    std::uint32_t gizmoVertexCount_ = 0U;
    bool selectionVisible_ = false;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    VkPipeline gridPipeline_ = VK_NULL_HANDLE;
    VkPipeline gizmoPipeline_ = VK_NULL_HANDLE;

    ViewportCamera camera_{};
    bool commandBuffersDirty_ = false;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    VkFence frameFence_ = VK_NULL_HANDLE;
    std::string lastError_;
};

} // namespace vortex::android