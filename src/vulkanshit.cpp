// vulkanshit.cpp — implementation of the explicit Vulkan layer (see vulkanshit.hpp).
#include "vulkanshit.hpp"

#include "rgfwshit.hpp" // RGFW_getRequiredInstanceExtensions_Vulkan

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace vks {
namespace {

const std::vector<const char*> k_validation_layers = {
    "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> k_device_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
constexpr bool k_validation_enabled = false;
#else
constexpr bool k_validation_enabled = true;
#endif

// ---------------- Debug messenger ----------------

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT       severity,
    VkDebugUtilsMessageTypeFlagsEXT              type,
    const VkDebugUtilsMessengerCallbackDataEXT*  data,
    void*                                        user_data) {
    (void)severity;
    (void)type;
    (void)user_data;
    std::cerr << "[vk] " << data->pMessage << "\n";
    return VK_FALSE;
}

VkResult create_debug_messenger(
    VkInstance                                  instance,
    const VkDebugUtilsMessengerCreateInfoEXT*   create_info,
    VkDebugUtilsMessengerEXT*                   out_messenger) {
    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (fn != nullptr) {
        return fn(instance, create_info, nullptr, out_messenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void destroy_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger) {
    auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (fn != nullptr) {
        fn(instance, messenger, nullptr);
    }
}

// ---------------- GPU probing ----------------

bool validation_layers_available() {
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available.data());

    for (const char* layer_name : k_validation_layers) {
        bool found = false;
        for (const VkLayerProperties& layer : available) {
            if (std::strcmp(layer_name, layer.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics_family;
    std::optional<uint32_t> present_family;

    bool complete() const {
        return graphics_family.has_value() && present_family.has_value();
    }
};

QueueFamilyIndices find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices;

    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);

    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());

    for (uint32_t i = 0; i < families.size() && !indices.complete(); i++) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics_family = i;
        }

        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
        if (present_support == VK_TRUE) {
            indices.present_family = i;
        }
    }

    return indices;
}

bool device_extensions_supported(VkPhysicalDevice device) {
    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);

    std::vector<VkExtensionProperties> available(extension_count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available.data());

    std::set<std::string> required(k_device_extensions.begin(), k_device_extensions.end());
    for (const VkExtensionProperties& extension : available) {
        required.erase(extension.extensionName);
    }
    return required.empty();
}

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR        capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   present_modes;
};

SwapchainSupport query_swapchain_support(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapchainSupport support;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &support.capabilities);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);
    if (format_count != 0) {
        support.formats.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, support.formats.data());
    }

    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, nullptr);
    if (present_mode_count != 0) {
        support.present_modes.resize(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, support.present_modes.data());
    }

    return support;
}

bool device_suitable(VkPhysicalDevice device, VkSurfaceKHR surface) {
    if (!find_queue_families(device, surface).complete()) return false;
    if (!device_extensions_supported(device)) return false;

    SwapchainSupport support = query_swapchain_support(device, surface);
    return !support.formats.empty() && !support.present_modes.empty();
}

// ---------------- Swapchain format choices ----------------

VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats[0];
}

VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes) {
    for (const VkPresentModeKHR mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    VkExtent2D extent = { width, height };
    extent.width  = std::clamp(extent.width,  capabilities.minImageExtent.width,  capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}

// ---------------- Memory + staging helpers ----------------

uint32_t find_memory_type(VkPhysicalDevice physical, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("[vk] failed to find suitable memory type");
}

// Allocates a transient command buffer, submits the recorded commands on the
// graphics queue and blocks until the queue is idle. Used for staging copies
// and image layout transitions.
void submit_single_shot(const Device& device, const std::function<void(VkCommandBuffer)>& record) {
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = device.graphics_family;

    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device.handle, &pool_info, nullptr, &pool) != VK_SUCCESS) {
        throw std::runtime_error("[vk] failed to create one-shot command pool");
    }

    VkCommandBufferAllocateInfo allocate_info = {};
    allocate_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool        = pool;
    allocate_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device.handle, &allocate_info, &cmd);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &begin_info);
    record(cmd);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info = {};
    submit_info.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers    = &cmd;

    vkQueueSubmit(device.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(device.graphics_queue);

    vkDestroyCommandPool(device.handle, pool, nullptr);
}

// ---------------- Shaders ----------------

// SPIR-V is read straight into 4-byte words so pCode is correctly aligned.
std::vector<uint32_t> read_spirv(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("[vk] failed to open file: " + path);
    }

    std::streamsize byte_size = file.tellg();
    if (byte_size <= 0 || byte_size % 4 != 0) {
        throw std::runtime_error("[vk] not a valid SPIR-V size: " + path);
    }

    std::vector<uint32_t> code(static_cast<size_t>(byte_size) / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), byte_size);
    return code;
}

VkShaderModule create_shader_module(VkDevice device, const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo create_info = {};
    create_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size() * sizeof(uint32_t);
    create_info.pCode    = code.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &create_info, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return module;
}

// Reads vert.spv + frag.spv, builds and destroys the modules inside.
VkResult load_vertex_and_fragment_stages(
    VkDevice device, const std::string& shader_dir,
    VkPipelineShaderStageCreateInfo stages[2]) {
    std::string sep = (shader_dir.empty() || shader_dir.back() == '/') ? "" : "/";

    std::vector<uint32_t> vertex_code;
    std::vector<uint32_t> fragment_code;
    try {
        vertex_code   = read_spirv(shader_dir + sep + "vert.spv");
        fragment_code = read_spirv(shader_dir + sep + "frag.spv");
    } catch (const std::exception& error) {
        std::cerr << "[vk] " << error.what() << "\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkShaderModule vertex_module   = create_shader_module(device, vertex_code);
    VkShaderModule fragment_module = create_shader_module(device, fragment_code);
    if (vertex_module == VK_NULL_HANDLE || fragment_module == VK_NULL_HANDLE) {
        if (vertex_module != VK_NULL_HANDLE)   vkDestroyShaderModule(device, vertex_module, nullptr);
        if (fragment_module != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragment_module, nullptr);
        std::cerr << "[vk] vkCreateShaderModule failed\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    stages[0] = {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex_module;
    stages[0].pName  = "main";

    stages[1] = {};
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment_module;
    stages[1].pName  = "main";

    return VK_SUCCESS;
}

void destroy_stages_modules(VkDevice device, VkPipelineShaderStageCreateInfo stages[2]) {
    vkDestroyShaderModule(device, stages[0].module, nullptr);
    vkDestroyShaderModule(device, stages[1].module, nullptr);
}

// ---------------- Frame pipeline construction ----------------

VkResult create_render_pass(const Device& device, const Swapchain& swapchain, FramePipeline& pipeline) {
    VkAttachmentDescription color_attachment = {};
    color_attachment.format         = swapchain.format;
    color_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_reference = {};
    color_reference.attachment = 0;
    color_reference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &color_reference;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo create_info = {};
    create_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 1;
    create_info.pAttachments    = &color_attachment;
    create_info.subpassCount    = 1;
    create_info.pSubpasses      = &subpass;
    create_info.dependencyCount = 1;
    create_info.pDependencies   = &dependency;

    VkResult result = vkCreateRenderPass(device.handle, &create_info, nullptr, &pipeline.render_pass);
    if (result != VK_SUCCESS) {
        std::cerr << "[vk] vkCreateRenderPass failed\n";
    }
    return result;
}

VkResult create_graphics_pipeline(const Device& device, const Swapchain& swapchain, FramePipeline& pipeline) {
    VkPipelineShaderStageCreateInfo stages[2];
    VkResult result = load_vertex_and_fragment_stages(device.handle, pipeline.shader_dir, stages);
    if (result != VK_SUCCESS) {
        return result;
    }

    // Vertex layout comes from the caller via the deep-copied PipelineConfig.
    VkPipelineVertexInputStateCreateInfo vertex_input = {};
    vertex_input.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount   = static_cast<uint32_t>(pipeline.vertex_bindings.size());
    vertex_input.pVertexBindingDescriptions      = pipeline.vertex_bindings.data();
    vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(pipeline.vertex_attributes.size());
    vertex_input.pVertexAttributeDescriptions    = pipeline.vertex_attributes.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(swapchain.extent.width);
    viewport.height   = static_cast<float>(swapchain.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchain.extent;

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports    = &viewport;
    viewport_state.scissorCount  = 1;
    viewport_state.pScissors     = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable        = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.cullMode                = pipeline.backface_cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rasterizer.frontFace               = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable  = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_attachment = {};
    blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_attachment.blendEnable = pipeline.alpha_blend ? VK_TRUE : VK_FALSE;
    if (blend_attachment.blendEnable) {
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable   = VK_FALSE;
    color_blending.logicOp         = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments    = &blend_attachment;

    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.pushConstantRangeCount = 0;
    if (pipeline.descriptor_set_layout != VK_NULL_HANDLE) {
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts    = &pipeline.descriptor_set_layout;
    }

    result = vkCreatePipelineLayout(device.handle, &layout_info, nullptr, &pipeline.layout);
    if (result != VK_SUCCESS) {
        destroy_stages_modules(device.handle, stages);
        std::cerr << "[vk] vkCreatePipelineLayout failed\n";
        return result;
    }

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo dynamic_state = {};
    dynamic_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(std::size(dynamic_states));
    dynamic_state.pDynamicStates    = dynamic_states;

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount          = 2;
    pipeline_info.pStages             = stages;
    pipeline_info.pVertexInputState   = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState      = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState   = &multisampling;
    pipeline_info.pColorBlendState    = &color_blending;
    pipeline_info.pDynamicState       = &dynamic_state;
    pipeline_info.layout              = pipeline.layout;
    pipeline_info.renderPass          = pipeline.render_pass;
    pipeline_info.subpass             = 0;

    result = vkCreateGraphicsPipelines(device.handle, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline.handle);
    destroy_stages_modules(device.handle, stages);

    if (result != VK_SUCCESS) {
        std::cerr << "[vk] vkCreateGraphicsPipelines failed\n";
        return result;
    }
    return VK_SUCCESS;
}

VkImageView create_image_view(VkDevice device, VkImage image, VkFormat format) {
    VkImageViewCreateInfo create_info = {};
    create_info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image    = image;
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format   = format;
    create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    create_info.subresourceRange.baseMipLevel   = 0;
    create_info.subresourceRange.levelCount     = 1;
    create_info.subresourceRange.baseArrayLayer = 0;
    create_info.subresourceRange.layerCount     = 1;

    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device, &create_info, nullptr, &view) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return view;
}

VkResult create_framebuffers(const Device& device, const Swapchain& swapchain, FramePipeline& pipeline) {
    pipeline.image_views.reserve(swapchain.images.size());
    for (VkImage image : swapchain.images) {
        VkImageView view = create_image_view(device.handle, image, swapchain.format);
        if (view == VK_NULL_HANDLE) {
            std::cerr << "[vk] failed to create image view\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        pipeline.image_views.push_back(view);
    }

    pipeline.framebuffers.reserve(pipeline.image_views.size());
    for (VkImageView view : pipeline.image_views) {
        VkFramebufferCreateInfo create_info = {};
        create_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        create_info.renderPass      = pipeline.render_pass;
        create_info.attachmentCount = 1;
        create_info.pAttachments    = &view;
        create_info.width           = swapchain.extent.width;
        create_info.height          = swapchain.extent.height;
        create_info.layers          = 1;

        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(device.handle, &create_info, nullptr, &framebuffer) != VK_SUCCESS) {
            std::cerr << "[vk] vkCreateFramebuffer failed\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        pipeline.framebuffers.push_back(framebuffer);
    }
    return VK_SUCCESS;
}

VkResult create_command_pool(const Device& device, FramePipeline& pipeline) {
    VkCommandPoolCreateInfo create_info = {};
    create_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    create_info.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                                   VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    create_info.queueFamilyIndex = device.graphics_family;

    VkResult result = vkCreateCommandPool(device.handle, &create_info, nullptr, &pipeline.command_pool);
    if (result != VK_SUCCESS) {
        std::cerr << "[vk] vkCreateCommandPool failed\n";
    }
    return result;
}

// Command buffers are recorded per frame by begin_frame()/end_frame(), so the
// render pass body is not baked in at creation time.
VkResult create_command_buffers(const Device& device, FramePipeline& pipeline) {
    pipeline.command_buffers.resize(pipeline.framebuffers.size());

    VkCommandBufferAllocateInfo allocate_info = {};
    allocate_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool        = pipeline.command_pool;
    allocate_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = static_cast<uint32_t>(pipeline.command_buffers.size());

    if (vkAllocateCommandBuffers(device.handle, &allocate_info, pipeline.command_buffers.data()) != VK_SUCCESS) {
        std::cerr << "[vk] vkAllocateCommandBuffers failed\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return VK_SUCCESS;
}

// One waitable semaphore per swapchain image (see render_finished in the header).
VkResult create_render_finished_semaphores(const Device& device, const Swapchain& swapchain, FramePipeline& pipeline) {
    pipeline.render_finished.assign(swapchain.images.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (VkSemaphore& semaphore : pipeline.render_finished) {
        if (vkCreateSemaphore(device.handle, &create_info, nullptr, &semaphore) != VK_SUCCESS) {
            std::cerr << "[vk] vkCreateSemaphore (render finished) failed\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    return VK_SUCCESS;
}

VkResult create_sync_objects(const Device& device, const Swapchain& swapchain, FramePipeline& pipeline) {
    pipeline.image_available.resize(k_max_frames_in_flight);
    pipeline.frame_fences.resize(k_max_frames_in_flight);
    pipeline.image_fences.assign(swapchain.images.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < k_max_frames_in_flight; i++) {
        if (vkCreateSemaphore(device.handle, &semaphore_info, nullptr, &pipeline.image_available[i]) != VK_SUCCESS ||
            vkCreateFence(device.handle, &fence_info, nullptr, &pipeline.frame_fences[i]) != VK_SUCCESS) {
            std::cerr << "[vk] failed to create frame sync objects\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    VkResult result = create_render_finished_semaphores(device, swapchain, pipeline);
    if (result != VK_SUCCESS) {
        std::cerr << "[vk] failed to create render finished semaphores\n";
    }
    return result;
}

} // namespace

// ---------------- Math ----------------

Mat4 ortho_projection(float width, float height) {
    Mat4 out = {};
    // Maps screen space with the origin at the top-left to NDC [-1, 1].
    // Vulkan's NDC has +Y going down, so the rows match window pixels 1:1
    // and the winding stays as authored (clockwise quads stay clockwise).
    out.m[0]  =  2.0f / width;
    out.m[5]  =  2.0f / height;
    out.m[10] =  1.0f;
    out.m[12] = -1.0f;
    out.m[13] = -1.0f;
    out.m[15] =  1.0f;
    return out;
}

// ---------------- Buffer ----------------

Buffer create_buffer(const Device& device, VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible) {
    Buffer out;
    out.size = size;

    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size        = size;
    buffer_info.usage       = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device.handle, &buffer_info, nullptr, &out.handle) != VK_SUCCESS) {
        std::cerr << "[vk] vkCreateBuffer failed\n";
        return out;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device.handle, out.handle, &requirements);

    const VkMemoryPropertyFlags flags =
        host_visible
        ? static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = requirements.size;
    try {
        alloc_info.memoryTypeIndex = find_memory_type(device.physical, requirements.memoryTypeBits, flags);
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        vkDestroyBuffer(device.handle, out.handle, nullptr);
        out = {};
        return out;
    }

    if (vkAllocateMemory(device.handle, &alloc_info, nullptr, &out.memory) != VK_SUCCESS) {
        std::cerr << "[vk] vkAllocateMemory failed\n";
        vkDestroyBuffer(device.handle, out.handle, nullptr);
        out = {};
        return out;
    }
    vkBindBufferMemory(device.handle, out.handle, out.memory, 0);

    if (host_visible) {
        vkMapMemory(device.handle, out.memory, 0, size, 0, &out.mapped);
        if (out.mapped == nullptr) {
            std::cerr << "[vk] vkMapMemory failed\n";
            destroy_buffer(device, out);
            return out;
        }
    }
    return out;
}

void update_buffer(const Device& device, Buffer& buffer, const void* data, VkDeviceSize size) {
    if (size > buffer.size) {
        std::cerr << "[vk] update_buffer size exceeds buffer capacity\n";
        return;
    }
    if (buffer.mapped != nullptr) {
        std::memcpy(buffer.mapped, data, size);
        return;
    }

    Buffer staging = create_buffer(device, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    if (staging.mapped == nullptr) {
        std::cerr << "[vk] failed to create staging buffer\n";
        return;
    }
    std::memcpy(staging.mapped, data, size);

    try {
        submit_single_shot(device, [&](VkCommandBuffer cmd) {
            VkBufferCopy region = {};
            region.size = size;
            vkCmdCopyBuffer(cmd, staging.handle, buffer.handle, 1, &region);
        });
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
    }

    destroy_buffer(device, staging);
}

void destroy_buffer(const Device& device, Buffer& buffer) {
    if (buffer.mapped != nullptr) {
        vkUnmapMemory(device.handle, buffer.memory);
    }
    if (buffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device.handle, buffer.memory, nullptr);
    }
    if (buffer.handle != VK_NULL_HANDLE) {
        vkDestroyBuffer(device.handle, buffer.handle, nullptr);
    }
    buffer = {};
}

// ---------------- Texture ----------------

Texture create_texture(const Device& device, uint32_t width, uint32_t height, const void* pixels) {
    constexpr VkFormat k_format = VK_FORMAT_R8G8B8A8_SRGB;

    Texture out;
    out.width  = width;
    out.height = height;

    VkDeviceSize image_size = static_cast<VkDeviceSize>(width) * height * 4;

    Buffer staging = create_buffer(device, image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    if (staging.mapped == nullptr) {
        std::cerr << "[vk] failed to create texture staging buffer\n";
        return out;
    }
    std::memcpy(staging.mapped, pixels, image_size);

    VkImageCreateInfo image_info = {};
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.extent.width  = width;
    image_info.extent.height = height;
    image_info.extent.depth  = 1;
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.format        = k_format;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device.handle, &image_info, nullptr, &out.image) != VK_SUCCESS) {
        std::cerr << "[vk] vkCreateImage failed\n";
        destroy_buffer(device, staging);
        return out;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device.handle, out.image, &requirements);

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = requirements.size;
    try {
        alloc_info.memoryTypeIndex = find_memory_type(
            device.physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        destroy_texture(device, out);
        destroy_buffer(device, staging);
        return out;
    }

    if (vkAllocateMemory(device.handle, &alloc_info, nullptr, &out.memory) != VK_SUCCESS) {
        std::cerr << "[vk] vkAllocateMemory failed\n";
        destroy_texture(device, out);
        destroy_buffer(device, staging);
        return out;
    }
    vkBindImageMemory(device.handle, out.image, out.memory, 0);

    try {
        submit_single_shot(device, [&](VkCommandBuffer cmd) {
            VkImageSubresourceRange range = {};
            range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            range.levelCount     = 1;
            range.layerCount     = 1;

            VkImageMemoryBarrier to_transfer = {};
            to_transfer.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            to_transfer.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
            to_transfer.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_transfer.image             = out.image;
            to_transfer.subresourceRange  = range;
            to_transfer.srcAccessMask     = 0;
            to_transfer.dstAccessMask     = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, nullptr, 0, nullptr, 1, &to_transfer);

            VkBufferImageCopy region = {};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = { width, height, 1 };
            vkCmdCopyBufferToImage(cmd, staging.handle, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            VkImageMemoryBarrier to_shader = {};
            to_shader.sType             = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            to_shader.oldLayout         = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_shader.newLayout         = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            to_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_shader.image             = out.image;
            to_shader.subresourceRange  = range;
            to_shader.srcAccessMask     = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_shader.dstAccessMask     = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                0, nullptr, 0, nullptr, 1, &to_shader);
        });
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        destroy_texture(device, out);
        destroy_buffer(device, staging);
        return out;
    }

    destroy_buffer(device, staging);

    VkImageViewCreateInfo view_info = {};
    view_info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image    = out.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format   = k_format;
    view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device.handle, &view_info, nullptr, &out.view) != VK_SUCCESS) {
        std::cerr << "[vk] failed to create texture image view\n";
        destroy_texture(device, out);
        return out;
    }

    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter               = VK_FILTER_LINEAR;
    sampler_info.minFilter               = VK_FILTER_LINEAR;
    sampler_info.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropyEnable        = VK_FALSE;
    sampler_info.maxAnisotropy           = 1.0f;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable           = VK_FALSE;
    sampler_info.minLod                  = 0.0f;
    sampler_info.maxLod                  = 0.0f;

    if (vkCreateSampler(device.handle, &sampler_info, nullptr, &out.sampler) != VK_SUCCESS) {
        std::cerr << "[vk] failed to create texture sampler\n";
        destroy_texture(device, out);
        return out;
    }

    return out;
}

void destroy_texture(const Device& device, Texture& texture) {
    if (texture.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device.handle, texture.sampler, nullptr);
    }
    if (texture.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device.handle, texture.view, nullptr);
    }
    if (texture.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device.handle, texture.memory, nullptr);
    }
    if (texture.image != VK_NULL_HANDLE) {
        vkDestroyImage(device.handle, texture.image, nullptr);
    }
    texture = {};
}

// ---------------- Descriptor ----------------

DescriptorSetLayout create_descriptor_layout(const Device& device, std::span<const VkDescriptorSetLayoutBinding> bindings) {
    DescriptorSetLayout out;

    VkDescriptorSetLayoutCreateInfo create_info = {};
    create_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = static_cast<uint32_t>(bindings.size());
    create_info.pBindings    = bindings.data();

    if (vkCreateDescriptorSetLayout(device.handle, &create_info, nullptr, &out.handle) != VK_SUCCESS) {
        std::cerr << "[vk] vkCreateDescriptorSetLayout failed\n";
        out = {};
    }
    return out;
}

DescriptorPool create_descriptor_pool(
    const Device& device,
    std::span<const VkDescriptorPoolSize> pool_sizes,
    uint32_t max_sets,
    uint32_t layout_count) {
    DescriptorPool out;
    out.layout_count = layout_count;

    VkDescriptorPoolCreateInfo create_info = {};
    create_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    create_info.maxSets       = max_sets;
    create_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    create_info.pPoolSizes    = pool_sizes.data();

    if (vkCreateDescriptorPool(device.handle, &create_info, nullptr, &out.handle) != VK_SUCCESS) {
        std::cerr << "[vk] vkCreateDescriptorPool failed\n";
        out = {};
    }
    return out;
}

std::vector<VkDescriptorSet> allocate_descriptor_sets(
    const Device& device, const DescriptorPool& pool, const DescriptorSetLayout& layout, uint32_t count) {
    std::vector<VkDescriptorSet> out;
    if (count == 0) return out;

    out.resize(count);
    std::vector<VkDescriptorSetLayout> layouts(count, layout.handle);

    VkDescriptorSetAllocateInfo allocate_info = {};
    allocate_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate_info.descriptorPool     = pool.handle;
    allocate_info.descriptorSetCount = count;
    allocate_info.pSetLayouts        = layouts.data();

    if (vkAllocateDescriptorSets(device.handle, &allocate_info, out.data()) != VK_SUCCESS) {
        std::cerr << "[vk] vkAllocateDescriptorSets failed\n";
        out.clear();
    }
    return out;
}

void destroy_descriptor_layout(const Device& device, DescriptorSetLayout& layout) {
    if (layout.handle != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.handle, layout.handle, nullptr);
    }
    layout = {};
}

void destroy_descriptor_pool(const Device& device, DescriptorPool& pool) {
    if (pool.handle != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device.handle, pool.handle, nullptr);
    }
    pool = {};
}

void write_descriptor_buffer(
    const Device& device, VkDescriptorSet set, uint32_t binding,
    VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
    VkDescriptorBufferInfo buffer_info = {};
    buffer_info.buffer = buffer;
    buffer_info.offset = offset;
    buffer_info.range  = range;

    VkWriteDescriptorSet write = {};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = binding;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo     = &buffer_info;

    vkUpdateDescriptorSets(device.handle, 1, &write, 0, nullptr);
}

void write_descriptor_textures(
    const Device& device, VkDescriptorSet set, uint32_t binding,
    std::span<const VkImageView> views, VkSampler sampler) {
    std::vector<VkDescriptorImageInfo> image_infos(views.size());
    for (size_t i = 0; i < views.size(); i++) {
        image_infos[i].sampler     = sampler;
        image_infos[i].imageView   = views[i];
        image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkWriteDescriptorSet write = {};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = binding;
    write.descriptorCount = static_cast<uint32_t>(views.size());
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = image_infos.data();

    vkUpdateDescriptorSets(device.handle, 1, &write, 0, nullptr);
}

// ---------------- Instance ----------------

Instance create_instance() {
    Instance out;

    if (k_validation_enabled && !validation_layers_available()) {
        std::cerr << "[vk] validation layers are not available\n";
        return out;
    }

    VkApplicationInfo app_info = {};
    app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName   = "huinya engine";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName        = "huinya engine";
    app_info.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion         = VK_API_VERSION_1_0;

    // Extensions RGFW needs to create the surface from a window.
    size_t rgfw_extension_count = 0;
    const char** rgfw_extensions = RGFW_getRequiredInstanceExtensions_Vulkan(&rgfw_extension_count);

    std::vector<const char*> extensions;
    if (rgfw_extensions != nullptr && rgfw_extension_count > 0) {
        extensions.assign(rgfw_extensions, rgfw_extensions + rgfw_extension_count);
    }
    if (k_validation_enabled) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo create_info = {};
    create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo        = &app_info;
    create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();
    if (k_validation_enabled) {
        create_info.enabledLayerCount   = static_cast<uint32_t>(k_validation_layers.size());
        create_info.ppEnabledLayerNames = k_validation_layers.data();
    }

    VkResult result = vkCreateInstance(&create_info, nullptr, &out.handle);
    if (result != VK_SUCCESS) {
        std::cerr << "[vk] vkCreateInstance failed: " << result << "\n";
        return out;
    }

    if (!k_validation_enabled) {
        return out;
    }
    out.validation = true;

    VkDebugUtilsMessengerCreateInfoEXT messenger_info = {};
    messenger_info.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    messenger_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messenger_info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messenger_info.pfnUserCallback = debug_callback;

    result = create_debug_messenger(out.handle, &messenger_info, &out.debug_messenger);
    if (result != VK_SUCCESS) {
        std::cerr << "[vk] failed to set up debug messenger\n";
        vkDestroyInstance(out.handle, nullptr);
        out = {};
    }
    return out;
}

void destroy_instance(Instance& instance) {
    if (instance.debug_messenger != VK_NULL_HANDLE) {
        destroy_debug_messenger(instance.handle, instance.debug_messenger);
    }
    if (instance.handle != VK_NULL_HANDLE) {
        vkDestroyInstance(instance.handle, nullptr);
    }
    instance = {};
}

// ---------------- Device ----------------

Device create_device(const Instance& instance, VkSurfaceKHR surface) {
    Device out;
    out.instance = instance.handle;

    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance.handle, &device_count, nullptr);
    if (device_count == 0) {
        std::cerr << "[vk] no GPUs with Vulkan support found\n";
        return out;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance.handle, &device_count, devices.data());

    QueueFamilyIndices queue_families;
    for (VkPhysicalDevice candidate : devices) {
        if (device_suitable(candidate, surface)) {
            out.physical = candidate;
            queue_families = find_queue_families(candidate, surface);
            break;
        }
    }

    if (out.physical == VK_NULL_HANDLE) {
        std::cerr << "[vk] no suitable GPU found\n";
        return out;
    }

    out.graphics_family = queue_families.graphics_family.value();
    out.present_family  = queue_families.present_family.value();

    std::set<uint32_t> unique_families = { out.graphics_family, out.present_family };

    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    float queue_priority = 1.0f;
    for (uint32_t family : unique_families) {
        VkDeviceQueueCreateInfo info = {};
        info.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        info.queueFamilyIndex = family;
        info.queueCount       = 1;
        info.pQueuePriorities = &queue_priority;
        queue_infos.push_back(info);
    }

    VkPhysicalDeviceFeatures features = {};

    VkDeviceCreateInfo create_info = {};
    create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount    = static_cast<uint32_t>(queue_infos.size());
    create_info.pQueueCreateInfos       = queue_infos.data();
    create_info.pEnabledFeatures        = &features;
    create_info.enabledExtensionCount   = static_cast<uint32_t>(k_device_extensions.size());
    create_info.ppEnabledExtensionNames = k_device_extensions.data();
    // Validation layers are instance-level; none are needed on the device.

    if (vkCreateDevice(out.physical, &create_info, nullptr, &out.handle) != VK_SUCCESS) {
        std::cerr << "[vk] vkCreateDevice failed\n";
        out = {};
        return out;
    }

    vkGetDeviceQueue(out.handle, out.graphics_family, 0, &out.graphics_queue);
    vkGetDeviceQueue(out.handle, out.present_family, 0, &out.present_queue);
    return out;
}

void destroy_device(Device& device) {
    if (device.handle != VK_NULL_HANDLE) {
        vkDestroyDevice(device.handle, nullptr);
    }
    device = {};
}

// ---------------- Swapchain ----------------

Swapchain create_swapchain(const Device& device, VkSurfaceKHR surface, uint32_t width, uint32_t height) {
    Swapchain out;
    out.surface = surface;
    out.width   = width;
    out.height  = height;

    SwapchainSupport support = query_swapchain_support(device.physical, surface);
    if (support.formats.empty() || support.present_modes.empty()) {
        std::cerr << "[vk] swapchain support incomplete (no formats/present modes)\n";
        return out;
    }

    VkSurfaceFormatKHR surface_format = choose_surface_format(support.formats);
    VkPresentModeKHR   present_mode   = choose_present_mode(support.present_modes);
    VkExtent2D         extent         = choose_extent(support.capabilities, width, height);

    uint32_t image_count = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 &&
        image_count > support.capabilities.maxImageCount) {
        image_count = support.capabilities.maxImageCount;
    }

    uint32_t queue_family_indices[] = {
        device.graphics_family,
        device.present_family
    };

    VkSwapchainCreateInfoKHR create_info = {};
    create_info.sType           = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface         = surface;
    create_info.minImageCount   = image_count;
    create_info.imageFormat     = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent     = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (device.graphics_family != device.present_family) {
        create_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices   = queue_family_indices;
    } else {
        create_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
        create_info.queueFamilyIndexCount = 0;
        create_info.pQueueFamilyIndices   = nullptr;
    }

    // Prefer opaque, fall back to inherit: some surfaces (e.g. Wayland
    // compositors) do not advertise the opaque flag.
    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((support.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) == 0 &&
        (support.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) != 0) {
        composite_alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }

    create_info.preTransform   = support.capabilities.currentTransform;
    create_info.compositeAlpha = composite_alpha;
    create_info.presentMode    = present_mode;
    create_info.clipped        = VK_TRUE;

    if (vkCreateSwapchainKHR(device.handle, &create_info, nullptr, &out.handle) != VK_SUCCESS) {
        std::cerr << "[vk] vkCreateSwapchainKHR failed\n";
        out = {};
        return out;
    }

    out.format = surface_format.format;
    out.extent = extent;

    vkGetSwapchainImagesKHR(device.handle, out.handle, &image_count, nullptr);
    out.images.resize(image_count);
    vkGetSwapchainImagesKHR(device.handle, out.handle, &image_count, out.images.data());
    return out;
}

void destroy_swapchain(const Device& device, Swapchain& swapchain) {
    if (swapchain.handle != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device.handle, swapchain.handle, nullptr);
    }
    swapchain = {};
}

// ---------------- Frame pipeline ----------------

FramePipeline create_frame_pipeline(
    const Device& device, const Swapchain& swapchain, const char* shader_dir, const PipelineConfig& config) {
    FramePipeline out;
    out.shader_dir = shader_dir != nullptr ? shader_dir : ".";
    // Deep copy the caller's spans so the pipeline can be rebuilt on resize.
    out.vertex_bindings.assign(config.bindings.begin(), config.bindings.end());
    out.vertex_attributes.assign(config.attributes.begin(), config.attributes.end());
    out.descriptor_set_layout = config.descriptor_set_layout;
    out.alpha_blend   = config.alpha_blend;
    out.backface_cull = config.backface_cull;

    if (create_render_pass(device, swapchain, out) != VK_SUCCESS ||
        create_graphics_pipeline(device, swapchain, out) != VK_SUCCESS ||
        create_framebuffers(device, swapchain, out) != VK_SUCCESS ||
        create_command_pool(device, out) != VK_SUCCESS ||
        create_command_buffers(device, out) != VK_SUCCESS ||
        create_sync_objects(device, swapchain, out) != VK_SUCCESS) {
        std::cerr << "[vk] failed to build frame pipeline\n";
        destroy_frame_pipeline(device, out);
    }
    return out;
}

void destroy_frame_pipeline(const Device& device, FramePipeline& pipeline) {
    for (size_t i = 0; i < k_max_frames_in_flight; i++) {
        if (i < pipeline.image_available.size() && pipeline.image_available[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device.handle, pipeline.image_available[i], nullptr);
        }
        if (i < pipeline.frame_fences.size() && pipeline.frame_fences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device.handle, pipeline.frame_fences[i], nullptr);
        }
    }
    for (VkSemaphore semaphore : pipeline.render_finished) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device.handle, semaphore, nullptr);
        }
    }

    if (pipeline.command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device.handle, pipeline.command_pool, nullptr);
    }
    for (VkFramebuffer framebuffer : pipeline.framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device.handle, framebuffer, nullptr);
        }
    }
    for (VkImageView view : pipeline.image_views) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device.handle, view, nullptr);
        }
    }

    if (pipeline.handle != VK_NULL_HANDLE) {
        vkDestroyPipeline(device.handle, pipeline.handle, nullptr);
    }
    if (pipeline.layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device.handle, pipeline.layout, nullptr);
    }
    if (pipeline.render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device.handle, pipeline.render_pass, nullptr);
    }
    pipeline = {};
}

// ---------------- Frame ----------------

// Returns VK_NULL_HANDLE on OUT_OF_DATE (the swapchain was rebuilt) or error;
// end_frame() must not be called in that case.
VkCommandBuffer begin_frame(const Device& device, Swapchain& swapchain, FramePipeline& pipeline) {
    vkWaitForFences(device.handle, 1, &pipeline.frame_fences[pipeline.current_frame], VK_TRUE, UINT64_MAX);

    uint32_t image_index = 0;
    VkResult result = vkAcquireNextImageKHR(
        device.handle, swapchain.handle, UINT64_MAX,
        pipeline.image_available[pipeline.current_frame],
        VK_NULL_HANDLE, &image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        if (recreate_swapchain(device, swapchain, pipeline) != VK_SUCCESS) {
            std::cerr << "[vk] failed to recreate swapchain\n";
        }
        return VK_NULL_HANDLE;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::cerr << "[vk] vkAcquireNextImageKHR failed: " << result << "\n";
        return VK_NULL_HANDLE;
    }

    if (pipeline.image_fences[image_index] != VK_NULL_HANDLE) {
        vkWaitForFences(device.handle, 1, &pipeline.image_fences[image_index], VK_TRUE, UINT64_MAX);
    }
    pipeline.image_fences[image_index] = pipeline.frame_fences[pipeline.current_frame];
    pipeline.current_image_index       = image_index;

    VkCommandBuffer cmd = pipeline.command_buffers[image_index];

    // The pool is shared by every swapchain image, so resetting it wholesale
    // would clobber buffers whose frames are still in flight. Reset only the
    // buffer we are about to re-record after waiting on its fences.
    vkResetCommandBuffer(cmd, 0);
    vkResetFences(device.handle, 1, &pipeline.frame_fences[pipeline.current_frame]);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
        std::cerr << "[vk] vkBeginCommandBuffer failed\n";
        return VK_NULL_HANDLE;
    }

    VkClearValue clear_color = { { { 0.0f, 0.0f, 0.0f, 1.0f } } };

    VkRenderPassBeginInfo render_pass_info = {};
    render_pass_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass      = pipeline.render_pass;
    render_pass_info.framebuffer     = pipeline.framebuffers[image_index];
    render_pass_info.renderArea.offset = { 0, 0 };
    render_pass_info.renderArea.extent = swapchain.extent;
    render_pass_info.clearValueCount   = 1;
    render_pass_info.pClearValues      = &clear_color;

    vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(swapchain.extent.width);
    viewport.height   = static_cast<float>(swapchain.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchain.extent;

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    return cmd;
}

VkResult end_frame(const Device& device, Swapchain& swapchain, FramePipeline& pipeline, VkCommandBuffer cmd) {
    uint32_t image_index = pipeline.current_image_index;

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkSemaphore wait_semaphores[] = { pipeline.image_available[pipeline.current_frame] };
    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    // The render-finished semaphore belongs to this swapchain image, not to the
    // frame in flight: the present engine may hold it longer than the submit fence.
    VkSemaphore signal_semaphores[] = { pipeline.render_finished[image_index] };

    VkSubmitInfo submit_info = {};
    submit_info.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount   = 1;
    submit_info.pWaitSemaphores      = wait_semaphores;
    submit_info.pWaitDstStageMask    = wait_stages;
    submit_info.commandBufferCount   = 1;
    submit_info.pCommandBuffers      = &cmd;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores    = signal_semaphores;

    VkResult result = vkQueueSubmit(device.graphics_queue, 1, &submit_info, pipeline.frame_fences[pipeline.current_frame]);
    if (result != VK_SUCCESS) {
        std::cerr << "[vk] vkQueueSubmit failed: " << result << "\n";
        return result;
    }

    VkSwapchainKHR swapchains[] = { swapchain.handle };

    VkPresentInfoKHR present_info = {};
    present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores    = signal_semaphores;
    present_info.swapchainCount     = 1;
    present_info.pSwapchains        = swapchains;
    present_info.pImageIndices      = &image_index;

    result = vkQueuePresentKHR(device.present_queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        return recreate_swapchain(device, swapchain, pipeline);
    }
    if (result != VK_SUCCESS) {
        std::cerr << "[vk] vkQueuePresentKHR failed: " << result << "\n";
        return result;
    }

    pipeline.current_frame = (pipeline.current_frame + 1) % k_max_frames_in_flight;
    return VK_SUCCESS;
}

// ---------------- Swapchain rebuild ----------------

VkResult recreate_swapchain(const Device& device, Swapchain& swapchain, FramePipeline& pipeline) {
    vkDeviceWaitIdle(device.handle);

    // Copy the configured vertex layout/blending into locals: burying the
    // pipeline below wipes its vectors, which the config spans would dangle to.
    std::vector<VkVertexInputBindingDescription>   bindings   = pipeline.vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> attributes = pipeline.vertex_attributes;
    VkDescriptorSetLayout descriptor_set_layout = pipeline.descriptor_set_layout;
    const bool alpha_blend = pipeline.alpha_blend;
    const bool cull_mode   = pipeline.backface_cull;

    std::string shader_dir = pipeline.shader_dir;
    destroy_frame_pipeline(device, pipeline);

    VkSurfaceKHR surface = swapchain.surface;
    uint32_t     width   = swapchain.width;
    uint32_t     height  = swapchain.height;
    destroy_swapchain(device, swapchain);

    swapchain = create_swapchain(device, surface, width, height);
    if (swapchain.handle == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PipelineConfig config;
    config.bindings              = bindings;
    config.attributes            = attributes;
    config.descriptor_set_layout = descriptor_set_layout;
    config.alpha_blend           = alpha_blend;
    config.backface_cull         = cull_mode;

    pipeline = create_frame_pipeline(device, swapchain, shader_dir.c_str(), config);
    if (pipeline.handle == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return VK_SUCCESS;
}

void wait_device_idle(Device& device) {
    vkDeviceWaitIdle(device.handle);
}

} // namespace vks