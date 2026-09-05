#include "vulkan_viewport.hpp"
#include "ViewportStage1ShadersGenerated.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

namespace vortex::android {
namespace {

void addLine(
    std::vector<ViewportVertex>& output,
    const std::array<float, 3>& a,
    const std::array<float, 3>& b,
    const std::array<float, 3>& color) {
    output.push_back(ViewportVertex{a, color});
    output.push_back(ViewportVertex{b, color});
}

[[nodiscard]] std::vector<ViewportVertex> buildGridVertices() {
    constexpr int halfExtent = 10;
    constexpr float minor = 0.20F;
    constexpr float major = 0.32F;
    std::vector<ViewportVertex> vertices;
    vertices.reserve(96U);

    for (int coordinate = -halfExtent; coordinate <= halfExtent; ++coordinate) {
        if (coordinate == 0) {
            continue;
        }
        const float value = static_cast<float>(coordinate);
        const float intensity = coordinate % 5 == 0 ? major : minor;
        const std::array<float, 3> color{intensity, intensity, intensity};
        addLine(
            vertices,
            {-static_cast<float>(halfExtent), 0.0F, value},
            { static_cast<float>(halfExtent), 0.0F, value},
            color);
        addLine(
            vertices,
            {value, 0.0F, -static_cast<float>(halfExtent)},
            {value, 0.0F,  static_cast<float>(halfExtent)},
            color);
    }

    addLine(vertices, {-10.0F, 0.0F, 0.0F}, {10.0F, 0.0F, 0.0F}, {0.90F, 0.18F, 0.16F});
    addLine(vertices, {0.0F, 0.0F, -10.0F}, {0.0F, 0.0F, 10.0F}, {0.18F, 0.38F, 0.95F});
    addLine(vertices, {0.0F, -3.0F, 0.0F}, {0.0F, 3.0F, 0.0F}, {0.20F, 0.85F, 0.28F});
    return vertices;
}

[[nodiscard]] VkShaderModule createShaderModule(
    VkDevice device,
    const std::uint32_t* words,
    std::size_t wordCount) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = wordCount * sizeof(std::uint32_t);
    info.pCode = words;
    VkShaderModule module = VK_NULL_HANDLE;
    return vkCreateShaderModule(device, &info, nullptr, &module) == VK_SUCCESS ? module : VK_NULL_HANDLE;
}

} // namespace

std::uint32_t VulkanViewport::findMemoryType(
    const std::uint32_t typeBits,
    const VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
    for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        if ((typeBits & (1U << index)) != 0U &&
            (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties) {
            return index;
        }
    }
    return UINT32_MAX;
}

bool VulkanViewport::createBuffer(
    const VkDeviceSize size,
    const VkBufferUsageFlags usage,
    const VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& memory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        buffer = VK_NULL_HANDLE;
        return failVk("vkCreateBuffer", result);
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    const std::uint32_t memoryType = findMemoryType(requirements.memoryTypeBits, properties);
    if (memoryType == UINT32_MAX) {
        vkDestroyBuffer(device_, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return fail("No compatible Vulkan memory type for viewport buffer");
    }

    VkMemoryAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = memoryType;
    result = vkAllocateMemory(device_, &allocationInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        memory = VK_NULL_HANDLE;
        vkDestroyBuffer(device_, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return failVk("vkAllocateMemory(buffer)", result);
    }
    result = vkBindBufferMemory(device_, buffer, memory, 0U);
    if (result != VK_SUCCESS) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyBuffer(device_, buffer, nullptr);
        memory = VK_NULL_HANDLE;
        buffer = VK_NULL_HANDLE;
        return failVk("vkBindBufferMemory", result);
    }
    return true;
}

bool VulkanViewport::createDynamicHostBuffer(
    const VkDeviceSize size,
    const VkBufferUsageFlags usage,
    VkBuffer& buffer,
    VkDeviceMemory& memory) {
    return createBuffer(
        size,
        usage,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        buffer,
        memory);
}

bool VulkanViewport::copyBuffer(
    const VkBuffer source,
    const VkBuffer destination,
    const VkDeviceSize size) {
    if (source == VK_NULL_HANDLE || destination == VK_NULL_HANDLE || size == 0U ||
        commandPool_ == VK_NULL_HANDLE || graphicsQueue_ == VK_NULL_HANDLE) {
        return fail("Invalid Vulkan staging-copy request");
    }

    VkCommandBufferAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocationInfo.commandPool = commandPool_;
    allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocationInfo.commandBufferCount = 1U;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkResult result = vkAllocateCommandBuffers(device_, &allocationInfo, &commandBuffer);
    if (result != VK_SUCCESS) {
        return failVk("vkAllocateCommandBuffers(staging)", result);
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, commandPool_, 1U, &commandBuffer);
        return failVk("vkBeginCommandBuffer(staging)", result);
    }

    const VkBufferCopy region{0U, 0U, size};
    vkCmdCopyBuffer(commandBuffer, source, destination, 1U, &region);
    result = vkEndCommandBuffer(commandBuffer);
    if (result == VK_SUCCESS) {
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1U;
        submitInfo.pCommandBuffers = &commandBuffer;
        result = vkQueueSubmit(graphicsQueue_, 1U, &submitInfo, VK_NULL_HANDLE);
    }
    if (result == VK_SUCCESS) {
        result = vkQueueWaitIdle(graphicsQueue_);
    }
    vkFreeCommandBuffers(device_, commandPool_, 1U, &commandBuffer);
    return result == VK_SUCCESS || failVk("viewport staging copy", result);
}

bool VulkanViewport::createStaticDeviceBuffer(
    const void* sourceData,
    const VkDeviceSize size,
    const VkBufferUsageFlags usage,
    VkBuffer& buffer,
    VkDeviceMemory& memory) {
    if (sourceData == nullptr || size == 0U) {
        return fail("Static viewport buffer upload received no data");
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    if (!createDynamicHostBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, stagingBuffer, stagingMemory)) {
        return false;
    }

    void* mapped = nullptr;
    VkResult result = vkMapMemory(device_, stagingMemory, 0U, size, 0U, &mapped);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingMemory, nullptr);
        return failVk("vkMapMemory(staging)", result);
    }
    std::memcpy(mapped, sourceData, static_cast<std::size_t>(size));
    vkUnmapMemory(device_, stagingMemory);

    const bool destinationCreated = createBuffer(
        size,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        buffer,
        memory);
    const bool copied = destinationCreated && copyBuffer(stagingBuffer, buffer, size);

    vkDestroyBuffer(device_, stagingBuffer, nullptr);
    vkFreeMemory(device_, stagingMemory, nullptr);
    if (!copied) {
        if (buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }
        return false;
    }
    return true;
}

bool VulkanViewport::createGeometryResources() {
    if (vertexBuffer_ != VK_NULL_HANDLE && indexBuffer_ != VK_NULL_HANDLE) {
        return true;
    }
    if (sceneVertices_.empty() || sceneIndices_.empty()) {
        return fail("No evaluated viewport snapshot was supplied by the editor session");
    }

    const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(sceneVertices_.size() * sizeof(ViewportVertex));
    const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(sceneIndices_.size() * sizeof(std::uint32_t));
    if (!createStaticDeviceBuffer(
            sceneVertices_.data(),
            vertexBytes,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            vertexBuffer_,
            vertexMemory_) ||
        !createStaticDeviceBuffer(
            sceneIndices_.data(),
            indexBytes,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            indexBuffer_,
            indexMemory_)) {
        destroyGeometry();
        return false;
    }

    void* mapped = nullptr;
    VkResult result = VK_SUCCESS;
    indexCount_ = static_cast<std::uint32_t>(sceneIndices_.size());

    if (!selectionOverlayVertices_.empty()) {
        const VkDeviceSize overlayBytes =
            static_cast<VkDeviceSize>(selectionOverlayVertices_.size() * sizeof(ViewportVertex));
        if (!createDynamicHostBuffer(
                overlayBytes,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                selectionVertexBuffer_,
                selectionVertexMemory_)) {
            destroyGeometry();
            return false;
        }
        mapped = nullptr;
        result = vkMapMemory(device_, selectionVertexMemory_, 0U, overlayBytes, 0U, &mapped);
        if (result != VK_SUCCESS) {
            destroyGeometry();
            return failVk("vkMapMemory(selection overlay)", result);
        }
        std::memcpy(mapped, selectionOverlayVertices_.data(), static_cast<std::size_t>(overlayBytes));
        vkUnmapMemory(device_, selectionVertexMemory_);
        selectionVertexCount_ = static_cast<std::uint32_t>(selectionOverlayVertices_.size());
    }
    return true;
}

bool VulkanViewport::createGridResources() {
    if (gridVertexBuffer_ != VK_NULL_HANDLE) {
        return true;
    }
    const std::vector<ViewportVertex> vertices = buildGridVertices();
    if (vertices.empty()) {
        return fail("Viewport grid generation returned no vertices");
    }

    const VkDeviceSize bytes = static_cast<VkDeviceSize>(vertices.size() * sizeof(ViewportVertex));
    if (!createStaticDeviceBuffer(
            vertices.data(),
            bytes,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            gridVertexBuffer_,
            gridVertexMemory_)) {
        return false;
    }
    gridVertexCount_ = static_cast<std::uint32_t>(vertices.size());
    return true;
}

VkFormat VulkanViewport::findDepthFormat() const {
    constexpr std::array<VkFormat, 3> candidates{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM,
    };
    for (const VkFormat format : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U) {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

bool VulkanViewport::createDepthResources() {
    depthFormat_ = findDepthFormat();
    if (depthFormat_ == VK_FORMAT_UNDEFINED) {
        return fail("No supported Vulkan depth format");
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = depthFormat_;
    imageInfo.extent = {swapchainExtent_.width, swapchainExtent_.height, 1U};
    imageInfo.mipLevels = 1U;
    imageInfo.arrayLayers = 1U;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = vkCreateImage(device_, &imageInfo, nullptr, &depthImage_);
    if (result != VK_SUCCESS) {
        return failVk("vkCreateImage(depth)", result);
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, depthImage_, &requirements);
    const std::uint32_t memoryType =
        findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == UINT32_MAX) {
        return fail("No device-local Vulkan memory type for depth image");
    }
    VkMemoryAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = memoryType;
    result = vkAllocateMemory(device_, &allocationInfo, nullptr, &depthMemory_);
    if (result != VK_SUCCESS) {
        return failVk("vkAllocateMemory(depth)", result);
    }
    result = vkBindImageMemory(device_, depthImage_, depthMemory_, 0U);
    if (result != VK_SUCCESS) {
        return failVk("vkBindImageMemory(depth)", result);
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1U;
    viewInfo.subresourceRange.layerCount = 1U;
    result = vkCreateImageView(device_, &viewInfo, nullptr, &depthView_);
    return result == VK_SUCCESS || failVk("vkCreateImageView(depth)", result);
}

bool VulkanViewport::createGraphicsPipeline() {
    const VkShaderModule vertexModule = createShaderModule(
        device_, stage1_shaders::kVertexSpirv,
        sizeof(stage1_shaders::kVertexSpirv) / sizeof(stage1_shaders::kVertexSpirv[0]));
    const VkShaderModule fragmentModule = createShaderModule(
        device_, stage1_shaders::kFragmentSpirv,
        sizeof(stage1_shaders::kFragmentSpirv) / sizeof(stage1_shaders::kFragmentSpirv[0]));
    if (vertexModule == VK_NULL_HANDLE || fragmentModule == VK_NULL_HANDLE) {
        if (vertexModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, vertexModule, nullptr);
        }
        if (fragmentModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, fragmentModule, nullptr);
        }
        return fail("Failed to create viewport shader modules");
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0U;
    binding.stride = sizeof(ViewportVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    const std::array<VkVertexInputAttributeDescription, 2> attributes{{
        {0U, 0U, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(ViewportVertex, position))},
        {1U, 0U, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(ViewportVertex, color))},
    }};
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1U;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1U;
    viewportState.scissorCount = 1U;
    constexpr std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0F;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState colorAttachment{};
    colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1U;
    colorBlend.pAttachments = &colorAttachment;

    VkPushConstantRange cameraPushConstant{};
    cameraPushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    cameraPushConstant.offset = 0U;
    cameraPushConstant.size = sizeof(CameraPushConstants);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1U;
    layoutInfo.pPushConstantRanges = &cameraPushConstant;
    VkResult result = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_);
    if (result != VK_SUCCESS) {
        vkDestroyShaderModule(device_, fragmentModule, nullptr);
        vkDestroyShaderModule(device_, vertexModule, nullptr);
        return failVk("vkCreatePipelineLayout", result);
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depth;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0U;

    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1U, &pipelineInfo, nullptr, &graphicsPipeline_);
    if (result == VK_SUCCESS) {
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        depth.depthWriteEnable = VK_FALSE;
        result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1U, &pipelineInfo, nullptr, &gridPipeline_);
    }

    vkDestroyShaderModule(device_, fragmentModule, nullptr);
    vkDestroyShaderModule(device_, vertexModule, nullptr);
    return result == VK_SUCCESS || failVk("vkCreateGraphicsPipelines", result);
}

void VulkanViewport::destroyGeometry() noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    if (selectionVertexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, selectionVertexBuffer_, nullptr);
    }
    if (selectionVertexMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, selectionVertexMemory_, nullptr);
    }
    if (gridVertexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, gridVertexBuffer_, nullptr);
    }
    if (gridVertexMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, gridVertexMemory_, nullptr);
    }
    if (indexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, indexBuffer_, nullptr);
    }
    if (indexMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, indexMemory_, nullptr);
    }
    if (vertexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, vertexBuffer_, nullptr);
    }
    if (vertexMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, vertexMemory_, nullptr);
    }
    selectionVertexBuffer_ = VK_NULL_HANDLE;
    selectionVertexMemory_ = VK_NULL_HANDLE;
    selectionVertexCount_ = 0U;
    gridVertexBuffer_ = VK_NULL_HANDLE;
    gridVertexMemory_ = VK_NULL_HANDLE;
    gridVertexCount_ = 0U;
    indexBuffer_ = VK_NULL_HANDLE;
    indexMemory_ = VK_NULL_HANDLE;
    vertexBuffer_ = VK_NULL_HANDLE;
    vertexMemory_ = VK_NULL_HANDLE;
    indexCount_ = 0U;
}

} // namespace vortex::android
