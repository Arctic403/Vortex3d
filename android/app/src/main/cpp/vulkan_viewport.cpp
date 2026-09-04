#include "vulkan_viewport.hpp"

#include <android/log.h>

#include <algorithm>
#include <limits>
#include <sstream>
#include <utility>

namespace vortex::android {
namespace {

constexpr const char* logTag = "Vortex3D";

[[nodiscard]] std::string apiVersionString(const std::uint32_t version) {
    std::ostringstream stream;
    stream << VK_VERSION_MAJOR(version) << '.' << VK_VERSION_MINOR(version) << '.' << VK_VERSION_PATCH(version);
    return stream.str();
}

} // namespace

VulkanViewport::~VulkanViewport() {
    shutdown();
}

bool VulkanViewport::attach(ANativeWindow* window) {
    if (window == nullptr) {
        return fail("Android surface did not provide a native window");
    }

    detach();
    window_ = window;
    lastError_.clear();

    if (instance_ == VK_NULL_HANDLE && !createInstance()) {
        destroySurface();
        return false;
    }
    if (!createSurface()) {
        destroySurface();
        return false;
    }

    if (device_ == VK_NULL_HANDLE) {
        if (!createDeviceForSurface() || !createCommandPool() || !createSyncObjects()) {
            shutdown();
            return false;
        }
    } else if (!verifyPresentSupport()) {
        destroySurface();
        return false;
    }

    if (!createGeometryResources() || !createGridResources()) {
        shutdown();
        return false;
    }
    if (!createSwapchain()) {
        destroySwapchain();
        destroySurface();
        return false;
    }
    return true;
}

bool VulkanViewport::resize() {
    if (surface_ == VK_NULL_HANDLE || window_ == nullptr || device_ == VK_NULL_HANDLE) {
        return false;
    }
    const std::int32_t nativeWidth = ANativeWindow_getWidth(window_);
    const std::int32_t nativeHeight = ANativeWindow_getHeight(window_);
    const auto width = nativeWidth > 0 ? static_cast<std::uint32_t>(nativeWidth) : 0U;
    const auto height = nativeHeight > 0 ? static_cast<std::uint32_t>(nativeHeight) : 0U;
    if (swapchain_ != VK_NULL_HANDLE && width == swapchainExtent_.width && height == swapchainExtent_.height) {
        return true;
    }
    return recreateSwapchain();
}

void VulkanViewport::detach() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(device_);
    }
    destroySwapchain();
    destroySurface();
}

bool VulkanViewport::rebuildCameraCommandBuffers() {
    if (!cameraDirty_) {
        return true;
    }
    if (device_ == VK_NULL_HANDLE || commandPool_ == VK_NULL_HANDLE || framebuffers_.empty()) {
        return false;
    }

    if (!commandBuffers_.empty()) {
        vkFreeCommandBuffers(
            device_, commandPool_, static_cast<std::uint32_t>(commandBuffers_.size()), commandBuffers_.data());
        commandBuffers_.clear();
    }

    if (!recordCommandBuffers()) {
        return false;
    }
    cameraDirty_ = false;
    return true;
}

bool VulkanViewport::render() {
    if (device_ == VK_NULL_HANDLE || swapchain_ == VK_NULL_HANDLE || commandBuffers_.empty()) {
        return false;
    }

    VkResult result = vkWaitForFences(device_, 1U, &frameFence_, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    if (result != VK_SUCCESS) {
        return failVk("vkWaitForFences", result);
    }

    // Camera input only marks state dirty. Re-record after the previous frame fence signals,
    // keeping gesture JNI calls lightweight and avoiding command-buffer mutation while in flight.
    if (cameraDirty_ && !rebuildCameraCommandBuffers()) {
        return false;
    }

    std::uint32_t imageIndex = 0;
    result = vkAcquireNextImageKHR(
        device_, swapchain_, std::numeric_limits<std::uint64_t>::max(), imageAvailable_, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return recreateSwapchain();
    }
    const bool acquiredSuboptimal = result == VK_SUBOPTIMAL_KHR;
    if (result != VK_SUCCESS && !acquiredSuboptimal) {
        return failVk("vkAcquireNextImageKHR", result);
    }
    if (imageIndex >= commandBuffers_.size()) {
        return fail("Swapchain returned an invalid image index");
    }

    result = vkResetFences(device_, 1U, &frameFence_);
    if (result != VK_SUCCESS) {
        return failVk("vkResetFences", result);
    }

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1U;
    submitInfo.pWaitSemaphores = &imageAvailable_;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1U;
    submitInfo.pCommandBuffers = &commandBuffers_[imageIndex];
    submitInfo.signalSemaphoreCount = 1U;
    submitInfo.pSignalSemaphores = &renderFinished_;

    result = vkQueueSubmit(graphicsQueue_, 1U, &submitInfo, frameFence_);
    if (result != VK_SUCCESS) {
        return failVk("vkQueueSubmit", result);
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1U;
    presentInfo.pWaitSemaphores = &renderFinished_;
    presentInfo.swapchainCount = 1U;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueue_, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquiredSuboptimal) {
        return recreateSwapchain();
    }
    return result == VK_SUCCESS || failVk("vkQueuePresentKHR", result);
}

std::string VulkanViewport::info() const {
    if (!lastError_.empty()) {
        return "Renderer error: " + lastError_;
    }
    if (device_ == VK_NULL_HANDLE) {
        return "Custom Vulkan viewport waiting for a surface";
    }

    std::ostringstream stream;
    stream << "Custom Vulkan | " << physicalProperties_.deviceName << " | API "
           << apiVersionString(physicalProperties_.apiVersion) << " | " << sizeof(void*) * 8U << "-bit";
    if (swapchain_ != VK_NULL_HANDLE) {
        stream << " | " << swapchainExtent_.width << 'x' << swapchainExtent_.height;
    }
    if (graphicsPipeline_ != VK_NULL_HANDLE && gridPipeline_ != VK_NULL_HANDLE &&
        depthView_ != VK_NULL_HANDLE && indexCount_ != 0U && gridVertexCount_ != 0U) {
        stream << " | Stage4 touch camera";
    }
    return stream.str();
}

void VulkanViewport::destroySurface() noexcept {
    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (window_ != nullptr) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
}

void VulkanViewport::shutdown() noexcept {
    detach();
    if (device_ != VK_NULL_HANDLE) {
        destroyGeometry();
        if (frameFence_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_, frameFence_, nullptr);
        }
        if (renderFinished_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, renderFinished_, nullptr);
        }
        if (imageAvailable_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, imageAvailable_, nullptr);
        }
        if (commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
        }
        vkDestroyDevice(device_, nullptr);
    }
    frameFence_ = VK_NULL_HANDLE;
    renderFinished_ = VK_NULL_HANDLE;
    imageAvailable_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    graphicsQueue_ = VK_NULL_HANDLE;
    presentQueue_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
    graphicsQueueFamily_ = UINT32_MAX;
    presentQueueFamily_ = UINT32_MAX;
    physicalProperties_ = {};
    cameraDirty_ = false;
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

bool VulkanViewport::fail(std::string message) {
    lastError_ = std::move(message);
    __android_log_print(ANDROID_LOG_ERROR, logTag, "%s", lastError_.c_str());
    return false;
}

bool VulkanViewport::failVk(const std::string_view action, const VkResult result) {
    return fail(std::string{action} + " failed with VkResult " + std::to_string(static_cast<int>(result)));
}

} // namespace vortex::android
