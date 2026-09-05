#include "vulkan_viewport.hpp"
#include "ViewportStage1ShadersGenerated.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <unordered_map>
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
    if (a.value() <= b.value()) {
        return {a.value(), b.value()};
    }
    return {b.value(), a.value()};
}

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

bool VulkanViewport::setViewportMesh(const vortex::ViewportMesh& mesh) {
    if (device_ != VK_NULL_HANDLE) {
        return fail("Stage 5A viewport snapshots must be supplied before Vulkan device creation");
    }
    if (mesh.vertices.empty() || mesh.triangles.empty()) {
        return fail("Viewport snapshot contains no renderable geometry");
    }

    sceneVertices_.clear();
    sceneIndices_.clear();
    selectionOverlayVertices_.clear();
    pickTriangles_.clear();

    sceneVertices_.reserve(mesh.vertices.size());
    for (const vortex::ViewportVertex& source : mesh.vertices) {
        const Vec3& n = source.normal;
        sceneVertices_.push_back(ViewportVertex{
            {source.position.x, source.position.y, source.position.z},
            {
                0.25F + 0.65F * (n.x * 0.5F + 0.5F),
                0.25F + 0.65F * (n.y * 0.5F + 0.5F),
                0.25F + 0.65F * (n.z * 0.5F + 0.5F),
            },
        });
    }

    sceneIndices_.reserve(mesh.triangles.size() * 3U);
    pickTriangles_.reserve(mesh.triangles.size());
    std::unordered_map<EdgeKey, EdgeInfo, EdgeKeyHash> edges;
    edges.reserve(mesh.triangles.size() * 3U);

    for (const vortex::ViewportTriangle& triangle : mesh.triangles) {
        if (triangle.a >= mesh.vertices.size() ||
            triangle.b >= mesh.vertices.size() ||
            triangle.c >= mesh.vertices.size()) {
            sceneVertices_.clear();
            sceneIndices_.clear();
            pickTriangles_.clear();
            return fail("Viewport snapshot contains an out-of-range triangle index");
        }

        sceneIndices_.push_back(triangle.a);
        sceneIndices_.push_back(triangle.b);
        sceneIndices_.push_back(triangle.c);
        pickTriangles_.push_back(PickTriangle{
            sceneVertices_[triangle.a].position,
            sceneVertices_[triangle.b].position,
            sceneVertices_[triangle.c].position,
            triangle.sourceFace,
        });

        const std::array<std::uint32_t, 3> indices{triangle.a, triangle.b, triangle.c};
        for (std::size_t edge = 0; edge < 3U; ++edge) {
            const std::uint32_t firstIndex = indices[edge];
            const std::uint32_t secondIndex = indices[(edge + 1U) % 3U];
            const vortex::VertexId firstSource = mesh.vertices[firstIndex].sourceVertex;
            const vortex::VertexId secondSource = mesh.vertices[secondIndex].sourceVertex;
            if (!firstSource || !secondSource || firstSource == secondSource) {
                continue;
            }

            const EdgeKey key = edgeKey(firstSource, secondSource);
            auto [iterator, inserted] = edges.try_emplace(key);
            EdgeInfo& info = iterator->second;
            if (inserted) {
                info.a = sceneVertices_[firstIndex].position;
                info.b = sceneVertices_[secondIndex].position;
                info.firstFace = triangle.sourceFace;
            } else if (triangle.sourceFace != info.firstFace) {
                info.crossesFaces = true;
            }
            ++info.occurrences;
        }
    }

    // Render only evaluated face boundaries. A triangulation diagonal appears twice with
    // the same source FaceId and is omitted, while a real mesh edge either appears once
    // or is shared by triangles carrying different source FaceIds.
    constexpr std::array<float, 3> selectedColor{1.0F, 0.55F, 0.08F};
    selectionOverlayVertices_.reserve(edges.size() * 2U + 18U);
    for (const auto& [key, info] : edges) {
        (void)key;
        if (info.occurrences == 1U || info.crossesFaces) {
            addLine(selectionOverlayVertices_, info.a, info.b, selectedColor);
        }
    }

    // Stage 5A gizmo foundation. Object transforms are not authored yet, so the bootstrap
    // cube's engine object origin is the world origin. These derived lines become a proper
    // transform-gizmo draw item when object transforms land.
    addLine(selectionOverlayVertices_, {-0.10F, 0.0F, 0.0F}, {0.10F, 0.0F, 0.0F}, {1.0F, 0.85F, 0.18F});
    addLine(selectionOverlayVertices_, {0.0F, -0.10F, 0.0F}, {0.0F, 0.10F, 0.0F}, {1.0F, 0.85F, 0.18F});
    addLine(selectionOverlayVertices_, {0.0F, 0.0F, -0.10F}, {0.0F, 0.0F, 0.10F}, {1.0F, 0.85F, 0.18F});
    addLine(selectionOverlayVertices_, {0.0F, 0.0F, 0.0F}, {1.55F, 0.0F, 0.0F}, {0.95F, 0.16F, 0.14F});
    addLine(selectionOverlayVertices_, {0.0F, 0.0F, 0.0F}, {0.0F, 1.55F, 0.0F}, {0.18F, 0.92F, 0.28F});
    addLine(selectionOverlayVertices_, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.55F}, {0.18F, 0.42F, 1.0F});

    selectionVisible_ = false;
    commandBuffersDirty_ = true;
    return !sceneVertices_.empty() && !sceneIndices_.empty() && !pickTriangles_.empty();
}

bool VulkanViewport::setSelectionVisible(const bool visible) noexcept {
    if (visible && selectionOverlayVertices_.empty()) {
        return false;
    }
    if (selectionVisible_ == visible) {
        return true;
    }
    selectionVisible_ = visible;
    commandBuffersDirty_ = true;
    return true;
}

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
        return fail("No compatible Vulkan memory type for viewport buffer");
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
    if (sceneVertices_.empty() || sceneIndices_.empty()) {
        return fail("No evaluated viewport snapshot was supplied by the editor session");
    }

    const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(sceneVertices_.size() * sizeof(ViewportVertex));
    const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(sceneIndices_.size() * sizeof(std::uint32_t));
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
    std::memcpy(mapped, sceneVertices_.data(), static_cast<std::size_t>(vertexBytes));
    vkUnmapMemory(device_, vertexMemory_);

    mapped = nullptr;
    result = vkMapMemory(device_, indexMemory_, 0U, indexBytes, 0U, &mapped);
    if (result != VK_SUCCESS) {
        destroyGeometry();
        return failVk("vkMapMemory(index)", result);
    }
    std::memcpy(mapped, sceneIndices_.data(), static_cast<std::size_t>(indexBytes));
    vkUnmapMemory(device_, indexMemory_);
    indexCount_ = static_cast<std::uint32_t>(sceneIndices_.size());

    if (!selectionOverlayVertices_.empty()) {
        const VkDeviceSize overlayBytes =
            static_cast<VkDeviceSize>(selectionOverlayVertices_.size() * sizeof(ViewportVertex));
        if (!createBuffer(
                overlayBytes,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                hostMemory,
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
    const VkMemoryPropertyFlags hostMemory =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!createBuffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, hostMemory, gridVertexBuffer_, gridVertexMemory_)) {
        return false;
    }

    void* mapped = nullptr;
    const VkResult result = vkMapMemory(device_, gridVertexMemory_, 0U, bytes, 0U, &mapped);
    if (result != VK_SUCCESS) {
        return failVk("vkMapMemory(grid)", result);
    }
    std::memcpy(mapped, vertices.data(), static_cast<std::size_t>(bytes));
    vkUnmapMemory(device_, gridVertexMemory_);
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
