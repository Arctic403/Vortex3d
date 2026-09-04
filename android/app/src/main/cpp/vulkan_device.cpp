#include "vulkan_viewport.hpp"

#include <array>
#include <vector>

namespace vortex::android {
namespace {

[[nodiscard]] bool supportsDeviceExtension(const VkPhysicalDevice device, const char* extensionName) {
    std::uint32_t extensionCount = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(extensionCount);
    if (extensionCount != 0U &&
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data()) != VK_SUCCESS) {
        return false;
    }
    for (const VkExtensionProperties& extension : extensions) {
        if (std::string_view{extension.extensionName} == extensionName) {
            return true;
        }
    }
    return false;
}

} // namespace

bool VulkanViewport::createInstance() {
    VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    applicationInfo.pApplicationName = "Vortex3D";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 3, 0);
    applicationInfo.pEngineName = "Vortex3D";
    applicationInfo.engineVersion = VK_MAKE_VERSION(0, 3, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_0;

    constexpr std::array<const char*, 2> extensions{
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };
    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        instance_ = VK_NULL_HANDLE;
        return failVk("vkCreateInstance", result);
    }
    return true;
}

bool VulkanViewport::createSurface() {
    VkAndroidSurfaceCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    createInfo.window = window_;
    const VkResult result = vkCreateAndroidSurfaceKHR(instance_, &createInfo, nullptr, &surface_);
    if (result != VK_SUCCESS) {
        surface_ = VK_NULL_HANDLE;
        return failVk("vkCreateAndroidSurfaceKHR", result);
    }
    return true;
}

bool VulkanViewport::createDeviceForSurface() {
    physicalDevice_ = VK_NULL_HANDLE;
    physicalProperties_ = {};
    graphicsQueueFamily_ = UINT32_MAX;
    presentQueueFamily_ = UINT32_MAX;

    std::uint32_t deviceCount = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    if (result != VK_SUCCESS) {
        return failVk("vkEnumeratePhysicalDevices", result);
    }
    if (deviceCount == 0U) {
        return fail("No Vulkan physical device is available on this phone");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    result = vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());
    if (result != VK_SUCCESS) {
        return failVk("vkEnumeratePhysicalDevices", result);
    }

    for (const VkPhysicalDevice candidate : devices) {
        if (!supportsDeviceExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }
        std::uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());

        std::uint32_t graphicsFamily = UINT32_MAX;
        std::uint32_t presentFamily = UINT32_MAX;
        for (std::uint32_t index = 0; index < queueCount; ++index) {
            VkBool32 supportsPresent = VK_FALSE;
            result = vkGetPhysicalDeviceSurfaceSupportKHR(candidate, index, surface_, &supportsPresent);
            if (result != VK_SUCCESS) {
                continue;
            }
            const bool graphics = (queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U;
            if (graphics && graphicsFamily == UINT32_MAX) {
                graphicsFamily = index;
            }
            if (supportsPresent == VK_TRUE && presentFamily == UINT32_MAX) {
                presentFamily = index;
            }
            if (graphics && supportsPresent == VK_TRUE) {
                graphicsFamily = index;
                presentFamily = index;
                break;
            }
        }
        if (graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX) {
            physicalDevice_ = candidate;
            graphicsQueueFamily_ = graphicsFamily;
            presentQueueFamily_ = presentFamily;
            break;
        }
    }

    if (physicalDevice_ == VK_NULL_HANDLE) {
        return fail("No Vulkan device supports both graphics and Android presentation");
    }
    vkGetPhysicalDeviceProperties(physicalDevice_, &physicalProperties_);

    constexpr float queuePriority = 1.0F;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    VkDeviceQueueCreateInfo graphicsQueueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    graphicsQueueInfo.queueFamilyIndex = graphicsQueueFamily_;
    graphicsQueueInfo.queueCount = 1U;
    graphicsQueueInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(graphicsQueueInfo);
    if (presentQueueFamily_ != graphicsQueueFamily_) {
        VkDeviceQueueCreateInfo presentQueueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        presentQueueInfo.queueFamilyIndex = presentQueueFamily_;
        presentQueueInfo.queueCount = 1U;
        presentQueueInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(presentQueueInfo);
    }

    constexpr std::array<const char*, 1> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    result = vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_);
    if (result != VK_SUCCESS) {
        device_ = VK_NULL_HANDLE;
        return failVk("vkCreateDevice", result);
    }
    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0U, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentQueueFamily_, 0U, &presentQueue_);
    return true;
}

bool VulkanViewport::verifyPresentSupport() {
    VkBool32 supported = VK_FALSE;
    const VkResult result =
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, presentQueueFamily_, surface_, &supported);
    if (result != VK_SUCCESS) {
        return failVk("vkGetPhysicalDeviceSurfaceSupportKHR", result);
    }
    return supported == VK_TRUE || fail("Recreated Android surface is unsupported by the selected present queue");
}

bool VulkanViewport::createCommandPool() {
    VkCommandPoolCreateInfo createInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    createInfo.queueFamilyIndex = graphicsQueueFamily_;
    const VkResult result = vkCreateCommandPool(device_, &createInfo, nullptr, &commandPool_);
    return result == VK_SUCCESS || failVk("vkCreateCommandPool", result);
}

bool VulkanViewport::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkResult result = vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailable_);
    if (result != VK_SUCCESS) {
        return failVk("vkCreateSemaphore(imageAvailable)", result);
    }
    result = vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinished_);
    if (result != VK_SUCCESS) {
        return failVk("vkCreateSemaphore(renderFinished)", result);
    }
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    result = vkCreateFence(device_, &fenceInfo, nullptr, &frameFence_);
    return result == VK_SUCCESS || failVk("vkCreateFence", result);
}

} // namespace vortex::android
