#include "vulkan_viewport.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace vortex::android {
namespace {

[[nodiscard]] VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    if (formats.size() == 1U && formats.front().format == VK_FORMAT_UNDEFINED) {
        return {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }
    for (const VkSurfaceFormatKHR format : formats) {
        if ((format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

[[nodiscard]] VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(const VkSurfaceCapabilitiesKHR& capabilities) {
    constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> choices{
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (const VkCompositeAlphaFlagBitsKHR choice : choices) {
        if ((capabilities.supportedCompositeAlpha & choice) != 0U) {
            return choice;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

} // namespace

bool VulkanViewport::recreateSwapchain() {
    if (device_ == VK_NULL_HANDLE || surface_ == VK_NULL_HANDLE || window_ == nullptr) {
        return false;
    }
    const VkResult result = vkDeviceWaitIdle(device_);
    if (result != VK_SUCCESS) {
        return failVk("vkDeviceWaitIdle", result);
    }
    destroySwapchain();
    return createSwapchain();
}

bool VulkanViewport::createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities{};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities);
    if (result != VK_SUCCESS) {
        return failVk("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);
    }
    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0U) {
        return fail("Android swapchain does not support color-attachment rendering");
    }

    std::uint32_t formatCount = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
    if (result != VK_SUCCESS) {
        return failVk("vkGetPhysicalDeviceSurfaceFormatsKHR", result);
    }
    if (formatCount == 0U) {
        return fail("Android Vulkan surface reported no formats");
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());
    if (result != VK_SUCCESS) {
        return failVk("vkGetPhysicalDeviceSurfaceFormatsKHR", result);
    }
    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
        const auto width = static_cast<std::uint32_t>(std::max(0, ANativeWindow_getWidth(window_)));
        const auto height = static_cast<std::uint32_t>(std::max(0, ANativeWindow_getHeight(window_)));
        if (width == 0U || height == 0U) {
            return fail("Android viewport has zero extent");
        }
        extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    std::uint32_t imageCount = capabilities.minImageCount + 1U;
    if (capabilities.maxImageCount != 0U && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    const std::array<std::uint32_t, 2> queueFamilies{graphicsQueueFamily_, presentQueueFamily_};
    VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1U;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (graphicsQueueFamily_ != presentQueueFamily_) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = static_cast<std::uint32_t>(queueFamilies.size());
        createInfo.pQueueFamilyIndices = queueFamilies.data();
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = chooseCompositeAlpha(capabilities);
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;

    result = vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_);
    if (result != VK_SUCCESS) {
        swapchain_ = VK_NULL_HANDLE;
        return failVk("vkCreateSwapchainKHR", result);
    }
    swapchainFormat_ = surfaceFormat.format;
    swapchainExtent_ = extent;

    result = vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
    if (result != VK_SUCCESS || imageCount == 0U) {
        return result == VK_SUCCESS ? fail("Vulkan swapchain returned no images")
                                    : failVk("vkGetSwapchainImagesKHR", result);
    }
    swapchainImages_.resize(imageCount);
    result = vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data());
    if (result != VK_SUCCESS) {
        return failVk("vkGetSwapchainImagesKHR", result);
    }

    swapchainImageViews_.reserve(swapchainImages_.size());
    for (const VkImage image : swapchainImages_) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat_;
        viewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                               VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1U;
        viewInfo.subresourceRange.layerCount = 1U;
        VkImageView view = VK_NULL_HANDLE;
        result = vkCreateImageView(device_, &viewInfo, nullptr, &view);
        if (result != VK_SUCCESS) {
            return failVk("vkCreateImageView", result);
        }
        swapchainImageViews_.push_back(view);
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorReference{0U, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1U;
    subpass.pColorAttachments = &colorReference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0U;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    passInfo.attachmentCount = 1U;
    passInfo.pAttachments = &colorAttachment;
    passInfo.subpassCount = 1U;
    passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 1U;
    passInfo.pDependencies = &dependency;
    result = vkCreateRenderPass(device_, &passInfo, nullptr, &renderPass_);
    if (result != VK_SUCCESS) {
        return failVk("vkCreateRenderPass", result);
    }

    framebuffers_.reserve(swapchainImageViews_.size());
    for (const VkImageView view : swapchainImageViews_) {
        VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebufferInfo.renderPass = renderPass_;
        framebufferInfo.attachmentCount = 1U;
        framebufferInfo.pAttachments = &view;
        framebufferInfo.width = swapchainExtent_.width;
        framebufferInfo.height = swapchainExtent_.height;
        framebufferInfo.layers = 1U;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        result = vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &framebuffer);
        if (result != VK_SUCCESS) {
            return failVk("vkCreateFramebuffer", result);
        }
        framebuffers_.push_back(framebuffer);
    }
    return recordCommandBuffers();
}

bool VulkanViewport::recordCommandBuffers() {
    commandBuffers_.resize(framebuffers_.size(), VK_NULL_HANDLE);
    VkCommandBufferAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocationInfo.commandPool = commandPool_;
    allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocationInfo.commandBufferCount = static_cast<std::uint32_t>(commandBuffers_.size());
    VkResult result = vkAllocateCommandBuffers(device_, &allocationInfo, commandBuffers_.data());
    if (result != VK_SUCCESS) {
        commandBuffers_.clear();
        return failVk("vkAllocateCommandBuffers", result);
    }

    for (std::size_t index = 0; index < commandBuffers_.size(); ++index) {
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        result = vkBeginCommandBuffer(commandBuffers_[index], &beginInfo);
        if (result != VK_SUCCESS) {
            return failVk("vkBeginCommandBuffer", result);
        }
        VkClearValue clearValue{};
        clearValue.color = {{0.025F, 0.035F, 0.055F, 1.0F}};
        VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        renderPassInfo.renderPass = renderPass_;
        renderPassInfo.framebuffer = framebuffers_[index];
        renderPassInfo.renderArea.extent = swapchainExtent_;
        renderPassInfo.clearValueCount = 1U;
        renderPassInfo.pClearValues = &clearValue;
        vkCmdBeginRenderPass(commandBuffers_[index], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(commandBuffers_[index]);
        result = vkEndCommandBuffer(commandBuffers_[index]);
        if (result != VK_SUCCESS) {
            return failVk("vkEndCommandBuffer", result);
        }
    }
    return true;
}

void VulkanViewport::destroySwapchain() noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    if (!commandBuffers_.empty() && commandPool_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(
            device_, commandPool_, static_cast<std::uint32_t>(commandBuffers_.size()), commandBuffers_.data());
    }
    commandBuffers_.clear();
    for (const VkFramebuffer framebuffer : framebuffers_) {
        vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.clear();
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    for (const VkImageView view : swapchainImageViews_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    swapchainImageViews_.clear();
    swapchainImages_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    swapchainFormat_ = VK_FORMAT_UNDEFINED;
    swapchainExtent_ = {};
}

} // namespace vortex::android
