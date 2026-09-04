#include "vulkan_viewport.hpp"
#include "ViewportStage1ShadersGenerated.hpp"

#include "vortex/engine.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

namespace vortex::android {
namespace {

[[nodiscard]] bool buildEngineCube(
    std::vector<ViewportVertex>& vertices,
    std::vector<std::uint32_t>& indices) {
    EditableMesh authored;
    const auto v0 = authored.addVertex({-1.0F, -1.0F, -1.0F});
    const auto v1 = authored.addVertex({ 1.0F, -1.0F, -1.0F});
    const auto v2 = authored.addVertex({ 1.0F,  1.0F, -1.0F});
    const auto v3 = authored.addVertex({-1.0F,  1.0F, -1.0F});
    const auto v4 = authored.addVertex({-1.0F, -1.0F,  1.0F});
    const auto v5 = authored.addVertex({ 1.0F, -1.0F,  1.0F});
    const auto v6 = authored.addVertex({ 1.0F,  1.0F,  1.0F});
    const auto v7 = authored.addVertex({-1.0F,  1.0F,  1.0F});
    if (!v0 || !v1 || !v2 || !v3 || !v4 || !v5 || !v6 || !v7) {
        return false;
    }

    if (!authored.addFace({v0, v3, v2, v1}) ||
        !authored.addFace({v4, v5, v6, v7}) ||
        !authored.addFace({v0, v1, v5, v4}) ||
        !authored.addFace({v1, v2, v6, v5}) ||
        !authored.addFace({v2, v3, v7, v6}) ||
        !authored.addFace({v3, v0, v4, v7}) ||
        !authored.validateStrict()) {
        return false;
    }

    Document document;
    const MeshId meshId = document.createMesh("Stage2 Engine Cube", std::move(authored));
    const MeshBlock* block = document.mesh(meshId);
    if (!meshId || block == nullptr) {
        return false;
    }

    const MeshEvaluationResult evaluated = MeshEvaluator::evaluate(*block);
    if (!evaluated || !evaluated.mesh) {
        return false;
    }
    const RenderExtractResult extracted = RenderExtractor::extract(*evaluated.mesh);
    if (!extracted || !extracted.mesh) {
        return false;
    }

    vertices.reserve(extracted.mesh->vertices.size());
    for (const vortex::ViewportVertex& source : extracted.mesh->vertices) {
        const Vec3& n = source.normal;
        vertices.push_back(ViewportVertex{
            {source.position.x, source.position.y, source.position.z},
            {
                0.25F + 0.65F * (n.x * 0.5F + 0.5F),
                0.25F + 0.65F * (n.y * 0.5F + 0.5F),
                0.25F + 0.65F * (n.z * 0.5F + 0.5F),
            },
        });
    }

    indices.reserve(extracted.mesh->triangles.size() * 3U);
    for (const ViewportTriangle& triangle : extracted.mesh->triangles) {
        indices.push_back(triangle.a);
        indices.push_back(triangle.b);
        indices.push_back(triangle.c);
    }
    return !vertices.empty() && !indices.empty();
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
        return fail("No compatible Vulkan memory type for geometry buffer");
    }

    VkMemoryAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = memoryType;
    result = vkAllocateMemory(device_, &allocationInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        memory = VK_NULL_HANDLE;
        return failVk("vkAllocateMemory(buffer)", result);
    }
    result = vkBindBufferMemory(device_, buffer, memory, 0U);
    return result == VK_SUCCESS || failVk("vkBindBufferMemory", result);
}

bool VulkanViewport::createGeometryResources() {
    if (vertexBuffer_ != VK_NULL_HANDLE && indexBuffer_ != VK_NULL_HANDLE) {
        return true;
    }

    std::vector<ViewportVertex> vertices;
    std::vector<std::uint32_t> indices;
    if (!buildEngineCube(vertices, indices)) {
        return fail("Vortex engine failed to evaluate/extract the Stage 2 cube");
    }

    const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(vertices.size() * sizeof(ViewportVertex));
    const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(indices.size() * sizeof(std::uint32_t));
    const VkMemoryPropertyFlags hostMemory =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!createBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, hostMemory, vertexBuffer_, vertexMemory_) ||
        !createBuffer(indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, hostMemory, indexBuffer_, indexMemory_)) {
        destroyGeometry();
        return false;
    }

    void* mapped = nullptr;
    VkResult result = vkMapMemory(device_, vertexMemory_, 0U, vertexBytes, 0U, &mapped);
    if (result != VK_SUCCESS) {
        destroyGeometry();
        return failVk("vkMapMemory(vertex)", result);
    }
    std::memcpy(mapped, vertices.data(), static_cast<std::size_t>(vertexBytes));
    vkUnmapMemory(device_, vertexMemory_);

    mapped = nullptr;
    result = vkMapMemory(device_, indexMemory_, 0U, indexBytes, 0U, &mapped);
    if (result != VK_SUCCESS) {
        destroyGeometry();
        return failVk("vkMapMemory(index)", result);
    }
    std::memcpy(mapped, indices.data(), static_cast<std::size_t>(indexBytes));
    vkUnmapMemory(device_, indexMemory_);
    indexCount_ = static_cast<std::uint32_t>(indices.size());
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
        return fail("Failed to create Stage 2 shader modules");
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
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

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
    cameraPushConstant.size = sizeof(float);
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
    result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1U, &pipelineInfo, nullptr, &graphicsPipeline_);

    vkDestroyShaderModule(device_, fragmentModule, nullptr);
    vkDestroyShaderModule(device_, vertexModule, nullptr);
    return result == VK_SUCCESS || failVk("vkCreateGraphicsPipelines", result);
}

void VulkanViewport::destroyGeometry() noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
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
    indexBuffer_ = VK_NULL_HANDLE;
    indexMemory_ = VK_NULL_HANDLE;
    vertexBuffer_ = VK_NULL_HANDLE;
    vertexMemory_ = VK_NULL_HANDLE;
    indexCount_ = 0U;
}

} // namespace vortex::android
