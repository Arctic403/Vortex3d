#pragma once

#include "vortex/viewport/render_extract.hpp"

#include <android/native_window.h>
#include <vulkan/vulkan.h>

#include <array>
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

// One visible editor object and its evaluated/extracted render snapshot. Object identity
// crosses the renderer boundary, while authored topology stays owned by the engine.
struct ViewportObjectSnapshot final {
    vortex::ObjectId objectId;
    vortex::ViewportMesh mesh;
    std::array<float, 3> origin{};
};

struct ViewportPick final {
    vortex::ObjectId objectId;
    vortex::FaceId sourceFace;
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
    struct PickTriangle final {
        std::array<float, 3> a{};
        std::array<float, 3> b{};
        std::array<float, 3> c{};
        vortex::ObjectId objectId;
        vortex::FaceId sourceFace;
    };

    struct SelectionOverlay final {
        vortex::ObjectId objectId;
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
    [[nodiscard]] bool recordCommandBuffers();
    [[nodiscard]] bool rebuildCommandBuffers();
    [[nodiscard]] bool refreshSelectionOverlay();
    [[nodiscard]] CameraPushConstants cameraPushConstants(float aspect) const noexcept;

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

    // CPU-side state is fully rebuildable from RenderExtractor snapshots.
    std::vector<ViewportVertex> sceneVertices_;
    std::vector<std::uint32_t> sceneIndices_;
    std::vector<PickTriangle> pickTriangles_;
    std::vector<SelectionOverlay> selectionOverlays_;
    std::size_t selectionOverlayCapacity_ = 0U;

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
    vortex::ObjectId selectedObject_;
    bool selectionVisible_ = false;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    VkPipeline gridPipeline_ = VK_NULL_HANDLE;

    ViewportCamera camera_{};
    bool commandBuffersDirty_ = false;
    bool selectionOverlayDirty_ = false;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    VkFence frameFence_ = VK_NULL_HANDLE;
    std::string lastError_;
};

} // namespace vortex::android
