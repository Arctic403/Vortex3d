#pragma once

#include <android/native_window.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vortex::android {

struct ViewportVertex {
    std::array<float, 3> position{};
    std::array<float, 3> color{};
};
static_assert(sizeof(ViewportVertex) == sizeof(float) * 6U);

class VulkanViewport final {
public:
    VulkanViewport() = default;
    ~VulkanViewport();

    VulkanViewport(const VulkanViewport&) = delete;
    VulkanViewport& operator=(const VulkanViewport&) = delete;

    // Takes ownership of the ANativeWindow reference returned by ANativeWindow_fromSurface().
    [[nodiscard]] bool attach(ANativeWindow* window);
    [[nodiscard]] bool resize();
    void detach() noexcept;
    [[nodiscard]] bool render();

    [[nodiscard]] std::string info() const;

private:
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
    [[nodiscard]] bool createGraphicsPipeline();
    [[nodiscard]] bool recordCommandBuffers();

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

    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory_ = VK_NULL_HANDLE;
    std::uint32_t indexCount_ = 0U;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    VkFence frameFence_ = VK_NULL_HANDLE;
    std::string lastError_;
};

} // namespace vortex::android
