#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "cube.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int kWidth = 1100, kHeight = 760;
constexpr size_t kMaxVertices = 27 * 6 * 6 + CUBE_STICKER_COUNT * 54;
struct Vertex { float x, y, z, r, g, b; };
struct Point { float x, y, z; };
struct QueueFamilies { uint32_t graphics = UINT32_MAX, present = UINT32_MAX; };

GLFWwindow *window;
VkInstance instance;
VkSurfaceKHR surface;
VkPhysicalDevice physicalDevice;
VkDevice device;
VkQueue graphicsQueue, presentQueue;
VkSwapchainKHR swapchain;
VkFormat swapFormat;
VkExtent2D extent;
std::vector<VkImage> swapImages;
std::vector<VkImageView> imageViews;
VkRenderPass renderPass;
std::vector<VkFramebuffer> framebuffers;
VkImage msaaColorImage, depthImage;
VkDeviceMemory msaaColorMemory, depthMemory;
VkImageView msaaColorView, depthView;
VkFormat depthFormat;
VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
VkCommandPool commandPool;
VkCommandBuffer commandBuffer;
VkDescriptorPool descriptorPool;
VkSemaphore imageAvailable, renderFinished;
VkFence frameFence;
VkBuffer vertexBuffer;
VkDeviceMemory vertexMemory;
Vertex *mappedVertices;
uint32_t activeVertexCount = 0;
VkPipelineLayout pipelineLayout;
VkPipeline pipeline;
Cube cube;
constexpr float kHomeYaw = 0.0f, kHomePitch = 0.0f;
float yaw = kHomeYaw, pitch = kHomePitch;
constexpr float kHomeCameraDistance = 4.8f;
float cameraDistance = kHomeCameraDistance;
bool dragging = false;
bool middleButtonWasDown = false;
bool framebufferResized = false;
bool swapchainOutOfDate = false;
double lastX = 0, lastY = 0;
std::mt19937 rng{42};
std::string lastScramble;
bool imguiContextCreated = false, imguiGlfwReady = false, imguiVulkanReady = false;

void vk_check(VkResult result, const char *what) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(what) + " failed (VkResult " + std::to_string(result) + ")");
}
void framebuffer_size_callback(GLFWwindow *, int, int) { framebufferResized = true; }
std::vector<char> read_file(const char *path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error(std::string("Cannot open shader: ") + path);
    auto size = file.tellg();
    std::vector<char> bytes(static_cast<size_t>(size));
    file.seekg(0); file.read(bytes.data(), size);
    return bytes;
}
uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) return i;
    throw std::runtime_error("No compatible Vulkan memory type");
}
QueueFamilies queue_families(VkPhysicalDevice gpu) {
    QueueFamilies q;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, props.data());
    for (uint32_t i = 0; i < count; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) q.graphics = i;
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &supported);
        if (supported) q.present = i;
    }
    return q;
}
VkShaderModule shader_module(const char *path) {
    auto code = read_file(path);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t *>(code.data());
    VkShaderModule module;
    vk_check(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule");
    return module;
}

void create_instance() {
    uint32_t count = 0;
    const char **extensions = glfwGetRequiredInstanceExtensions(&count);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Rubik's Cube AI"; app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "Rubiks"; app.engineVersion = VK_MAKE_VERSION(0, 1, 0); app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app; info.enabledExtensionCount = count; info.ppEnabledExtensionNames = extensions;
    vk_check(vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance");
    vk_check(glfwCreateWindowSurface(instance, window, nullptr, &surface), "glfwCreateWindowSurface");
}
void create_device() {
    uint32_t count = 0; vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (!count) throw std::runtime_error("No Vulkan physical devices found");
    std::vector<VkPhysicalDevice> gpus(count); vkEnumeratePhysicalDevices(instance, &count, gpus.data());
    for (auto gpu : gpus) {
        auto q = queue_families(gpu);
        if (q.graphics != UINT32_MAX && q.present != UINT32_MAX) { physicalDevice = gpu; break; }
    }
    if (!physicalDevice) throw std::runtime_error("No Vulkan device supports graphics and presentation");
    VkPhysicalDeviceProperties deviceProps{};
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProps);
    VkSampleCountFlags supportedSamples = deviceProps.limits.framebufferColorSampleCounts & deviceProps.limits.framebufferDepthSampleCounts;
    if (supportedSamples & VK_SAMPLE_COUNT_4_BIT) msaaSamples = VK_SAMPLE_COUNT_4_BIT;
    else if (supportedSamples & VK_SAMPLE_COUNT_2_BIT) msaaSamples = VK_SAMPLE_COUNT_2_BIT;
    auto q = queue_families(physicalDevice);
    std::array<uint32_t, 2> indices{q.graphics, q.present};
    std::vector<uint32_t> unique(indices.begin(), indices.end());
    std::sort(unique.begin(), unique.end()); unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queues;
    for (uint32_t index : unique) {
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex = index; qi.queueCount = 1; qi.pQueuePriorities = &priority; queues.push_back(qi);
    }
    const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceFeatures features{};
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = static_cast<uint32_t>(queues.size()); info.pQueueCreateInfos = queues.data();
    info.enabledExtensionCount = 1; info.ppEnabledExtensionNames = extensions; info.pEnabledFeatures = &features;
    vk_check(vkCreateDevice(physicalDevice, &info, nullptr, &device), "vkCreateDevice");
    vkGetDeviceQueue(device, q.graphics, 0, &graphicsQueue); vkGetDeviceQueue(device, q.present, 0, &presentQueue);
}
void create_swapchain() {
    VkSurfaceCapabilitiesKHR caps{}; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);
    uint32_t count = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count); vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data());
    if (formats.empty()) throw std::runtime_error("No Vulkan surface formats available");
    VkSurfaceFormatKHR chosen = formats[0];
    for (VkFormat preferred : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
        auto found = std::find_if(formats.begin(), formats.end(), [&](const auto &f) { return f.format == preferred && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; });
        if (found != formats.end()) { chosen = *found; break; }
    }
    swapFormat = chosen.format;
    if (caps.currentExtent.width != UINT32_MAX) extent = caps.currentExtent;
    else { int w, h; glfwGetFramebufferSize(window, &w, &h); extent.width = std::clamp((uint32_t)w, caps.minImageExtent.width, caps.maxImageExtent.width); extent.height = std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height); }
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;
    auto q = queue_families(physicalDevice);
    uint32_t families[] = {q.graphics, q.present};
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface; info.minImageCount = imageCount; info.imageFormat = chosen.format; info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent; info.imageArrayLayers = 1; info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (q.graphics != q.present) { info.imageSharingMode = VK_SHARING_MODE_CONCURRENT; info.queueFamilyIndexCount = 2; info.pQueueFamilyIndices = families; }
    else info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform; info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR; info.clipped = VK_TRUE;
    vk_check(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain), "vkCreateSwapchainKHR");
    swapImages.clear(); imageViews.clear();
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr); swapImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapImages.data());
    imageViews.resize(imageCount);
    for (size_t i = 0; i < imageCount; ++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = swapImages[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = swapFormat;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
        vk_check(vkCreateImageView(device, &vi, nullptr, &imageViews[i]), "vkCreateImageView");
    }
}
void create_render_targets() {
    VkFormat depthFormats[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM};
    for (VkFormat format : depthFormats) {
        VkFormatProperties props{}; vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { depthFormat = format; break; }
    }
    if (depthFormat == VK_FORMAT_UNDEFINED) throw std::runtime_error("No supported depth attachment format");

    auto make_attachment = [&](VkFormat format, VkImageUsageFlags usage, VkImage *image, VkDeviceMemory *memory, VkImageView *view, VkImageAspectFlags aspect) {
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.imageType = VK_IMAGE_TYPE_2D; ii.extent = {extent.width, extent.height, 1};
        ii.mipLevels = 1; ii.arrayLayers = 1; ii.format = format; ii.tiling = VK_IMAGE_TILING_OPTIMAL; ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ii.usage = usage; ii.samples = msaaSamples; ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vk_check(vkCreateImage(device, &ii, nullptr, image), "vkCreateImage attachment");
        VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device, *image, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size; ai.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vk_check(vkAllocateMemory(device, &ai, nullptr, memory), "vkAllocateMemory attachment");
        vk_check(vkBindImageMemory(device, *image, *memory, 0), "vkBindImageMemory attachment");
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = *image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format;
        vi.subresourceRange.aspectMask = aspect; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
        vk_check(vkCreateImageView(device, &vi, nullptr, view), "vkCreateImageView attachment");
    };
    make_attachment(swapFormat, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &msaaColorImage, &msaaColorMemory, &msaaColorView, VK_IMAGE_ASPECT_COLOR_BIT);
    make_attachment(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &depthImage, &depthMemory, &depthView, VK_IMAGE_ASPECT_DEPTH_BIT);

    VkAttachmentDescription attachments[3]{};
    attachments[0].format = swapFormat; attachments[0].samples = msaaSamples; attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].format = depthFormat; attachments[1].samples = msaaSamples; attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[2].format = swapFormat; attachments[2].samples = VK_SAMPLE_COUNT_1_BIT; attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachments[2].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference resolveRef{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &colorRef; sub.pResolveAttachments = &resolveRef; sub.pDepthStencilAttachment = &depthRef;
    VkSubpassDependency dep{}; dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; ri.attachmentCount = 3; ri.pAttachments = attachments; ri.subpassCount = 1; ri.pSubpasses = &sub; ri.dependencyCount = 1; ri.pDependencies = &dep;
    vk_check(vkCreateRenderPass(device, &ri, nullptr, &renderPass), "vkCreateRenderPass");
    framebuffers.resize(imageViews.size());
    for (size_t i = 0; i < imageViews.size(); ++i) {
        VkImageView views[] = {msaaColorView, depthView, imageViews[i]};
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fi.renderPass = renderPass; fi.attachmentCount = 3; fi.pAttachments = views; fi.width = extent.width; fi.height = extent.height; fi.layers = 1;
        vk_check(vkCreateFramebuffer(device, &fi, nullptr, &framebuffers[i]), "vkCreateFramebuffer");
    }
}
void create_pipeline() {
    auto vert = shader_module(RUBIKS_VERTEX_SHADER), frag = shader_module(RUBIKS_FRAGMENT_SHADER);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vert; stages[0].pName = "main";
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag; stages[1].pName = "main";
    VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &binding; vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0, 0, (float)extent.width, (float)extent.height, 0, 1}; VkRect2D scissor{{0, 0}, extent};
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount = 1; vp.pViewports = &viewport; vp.scissorCount = 1; vp.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = msaaSamples;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_LESS; ds.minDepthBounds = 0; ds.maxDepthBounds = 1;
    VkPipelineColorBlendAttachmentState blend{}; blend.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount = 1; cb.pAttachments = &blend;
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; vk_check(vkCreatePipelineLayout(device, &li, nullptr, &pipelineLayout), "vkCreatePipelineLayout");
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; pi.stageCount = 2; pi.pStages = stages; pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia; pi.pViewportState = &vp; pi.pRasterizationState = &rs; pi.pMultisampleState = &ms; pi.pDepthStencilState = &ds; pi.pColorBlendState = &cb; pi.layout = pipelineLayout; pi.renderPass = renderPass; pi.subpass = 0;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline);
    vkDestroyShaderModule(device, frag, nullptr); vkDestroyShaderModule(device, vert, nullptr);
    vk_check(result, "vkCreateGraphicsPipelines");
}
void create_commands() {
    auto q = queue_families(physicalDevice);
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pi.queueFamilyIndex = q.graphics; pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vk_check(vkCreateCommandPool(device, &pi, nullptr, &commandPool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ai.commandPool = commandPool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    vk_check(vkAllocateCommandBuffers(device, &ai, &commandBuffer), "vkAllocateCommandBuffers");
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, 1000}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000}, {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000}, {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000}, {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};
    VkDescriptorPoolCreateInfo di{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; di.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; di.maxSets = 11000; di.poolSizeCount = 11; di.pPoolSizes = sizes;
    vk_check(vkCreateDescriptorPool(device, &di, nullptr, &descriptorPool), "vkCreateDescriptorPool");
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; vk_check(vkCreateSemaphore(device, &si, nullptr, &imageAvailable), "vkCreateSemaphore"); vk_check(vkCreateSemaphore(device, &si, nullptr, &renderFinished), "vkCreateSemaphore");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fi.flags = VK_FENCE_CREATE_SIGNALED_BIT; vk_check(vkCreateFence(device, &fi, nullptr, &frameFence), "vkCreateFence");
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = sizeof(Vertex) * kMaxVertices; bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vk_check(vkCreateBuffer(device, &bi, nullptr, &vertexBuffer), "vkCreateBuffer");
    VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(device, vertexBuffer, &req);
    VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mi.allocationSize = req.size; mi.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vk_check(vkAllocateMemory(device, &mi, nullptr, &vertexMemory), "vkAllocateMemory"); vk_check(vkBindBufferMemory(device, vertexBuffer, vertexMemory, 0), "vkBindBufferMemory");
    vk_check(vkMapMemory(device, vertexMemory, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void **>(&mappedVertices)), "vkMapMemory");
}
void init_imgui_vulkan() {
    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = instance; info.PhysicalDevice = physicalDevice; info.Device = device; info.QueueFamily = queue_families(physicalDevice).graphics;
    info.Queue = graphicsQueue; info.PipelineCache = VK_NULL_HANDLE; info.DescriptorPool = descriptorPool; info.RenderPass = renderPass;
    info.Subpass = 0; info.MinImageCount = static_cast<uint32_t>(swapImages.size()); info.ImageCount = static_cast<uint32_t>(swapImages.size()); info.MSAASamples = msaaSamples;
    if (!ImGui_ImplVulkan_Init(&info)) throw std::runtime_error("Dear ImGui Vulkan backend initialization failed");
    imguiVulkanReady = true;
}
void init_imgui() {
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); imguiContextCreated = true; ImGui::StyleColorsDark();
    ImGuiIO &io = ImGui::GetIO();
    ImFontConfig fontConfig{};
    fontConfig.SizePixels = 16.0f;
    fontConfig.OversampleH = 3;
    fontConfig.OversampleV = 2;
    fontConfig.PixelSnapH = false;
    const char *fontPaths[] = {
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    };
    for (const char *path : fontPaths) {
        std::ifstream fontFile(path, std::ios::binary);
        if (fontFile.good()) { io.FontDefault = io.Fonts->AddFontFromFileTTF(path, 16.0f, &fontConfig); break; }
    }
    if (!io.FontDefault) io.FontDefault = io.Fonts->AddFontDefault(&fontConfig);
    if (!io.Fonts->Build()) throw std::runtime_error("Could not build ImGui font atlas");
    if (!ImGui_ImplGlfw_InitForVulkan(window, true)) throw std::runtime_error("Dear ImGui GLFW backend initialization failed");
    imguiGlfwReady = true;
    init_imgui_vulkan();
}
void init_vulkan() {
    create_instance(); create_device(); create_swapchain(); create_render_targets(); create_pipeline(); create_commands(); init_imgui();
}

void recreate_swapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while ((width == 0 || height == 0) && !glfwWindowShouldClose(window)) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window, &width, &height);
    }
    if (glfwWindowShouldClose(window)) return;
    vk_check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle");

    if (imguiVulkanReady) {
        ImGui_ImplVulkan_Shutdown();
        imguiVulkanReady = false;
    }
    if (pipeline) { vkDestroyPipeline(device, pipeline, nullptr); pipeline = VK_NULL_HANDLE; }
    if (pipelineLayout) { vkDestroyPipelineLayout(device, pipelineLayout, nullptr); pipelineLayout = VK_NULL_HANDLE; }
    for (auto framebuffer : framebuffers) vkDestroyFramebuffer(device, framebuffer, nullptr);
    framebuffers.clear();
    if (renderPass) { vkDestroyRenderPass(device, renderPass, nullptr); renderPass = VK_NULL_HANDLE; }
    if (msaaColorView) { vkDestroyImageView(device, msaaColorView, nullptr); msaaColorView = VK_NULL_HANDLE; }
    if (depthView) { vkDestroyImageView(device, depthView, nullptr); depthView = VK_NULL_HANDLE; }
    if (msaaColorImage) { vkDestroyImage(device, msaaColorImage, nullptr); msaaColorImage = VK_NULL_HANDLE; }
    if (depthImage) { vkDestroyImage(device, depthImage, nullptr); depthImage = VK_NULL_HANDLE; }
    if (msaaColorMemory) { vkFreeMemory(device, msaaColorMemory, nullptr); msaaColorMemory = VK_NULL_HANDLE; }
    if (depthMemory) { vkFreeMemory(device, depthMemory, nullptr); depthMemory = VK_NULL_HANDLE; }
    for (auto view : imageViews) vkDestroyImageView(device, view, nullptr);
    imageViews.clear(); swapImages.clear();
    if (swapchain) { vkDestroySwapchainKHR(device, swapchain, nullptr); swapchain = VK_NULL_HANDLE; }

    create_swapchain();
    create_render_targets();
    create_pipeline();
    init_imgui_vulkan();
    framebufferResized = false;
    swapchainOutOfDate = false;
}

Point rotate(Point p) {
    float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    Point y{cy*p.x + sy*p.z, p.y, -sy*p.x + cy*p.z};
    return {y.x, cp*y.y - sp*y.z, sp*y.y + cp*y.z};
}
Point add(Point a, Point b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Point scale(Point a, float s) { return {a.x*s, a.y*s, a.z*s}; }
struct FaceBasis { Point normal, right, down; };
const FaceBasis faceBases[] = {
    {{0,1,0},{1,0,0},{0,0,1}}, {{1,0,0},{0,0,-1},{0,-1,0}}, {{0,0,1},{1,0,0},{0,-1,0}},
    {{0,-1,0},{1,0,0},{0,0,-1}}, {{-1,0,0},{0,0,1},{0,-1,0}}, {{0,0,-1},{-1,0,0},{0,-1,0}}
};
std::array<float, 3> sticker_rgb(char c) {
    switch (c) {
    case 'U': return {0.94f, 0.94f, 0.92f}; case 'R': return {0.82f, 0.08f, 0.10f};
    case 'F': return {0.04f, 0.62f, 0.24f}; case 'D': return {0.98f, 0.78f, 0.04f};
    case 'L': return {1.00f, 0.36f, 0.03f}; default: return {0.03f, 0.30f, 0.82f};
    }
}
float srgb_to_linear(float value) {
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}
void build_cube_vertices() {
    const float centers[] = {-0.66f, 0.0f, 0.66f};
    struct DrawPoly { float depth; std::vector<Point> corners; std::array<float,3> color; };
    std::vector<DrawPoly> drawPolys;
    auto add_poly = [&](Point center, const FaceBasis &basis, float half, std::array<float,3> rgb, float radius = 0.0f) {
        DrawPoly p{};
        if (radius <= 0.0f) {
            p.corners = {
                add(add(center, scale(basis.right,-half)), scale(basis.down,-half)),
                add(add(center, scale(basis.right, half)), scale(basis.down,-half)),
                add(add(center, scale(basis.right, half)), scale(basis.down, half)),
                add(add(center, scale(basis.right,-half)), scale(basis.down, half))
            };
        } else {
            constexpr float pi = 3.14159265358979323846f;
            const float inner = half - radius;
            const std::array<std::array<float, 3>, 4> arcs{{
                {{-inner, -inner, pi}}, {{inner, -inner, 1.5f*pi}},
                {{inner, inner, 0.0f}}, {{-inner, inner, 0.5f*pi}}
            }};
            constexpr int segmentsPerCorner = 4;
            p.corners.reserve(4 * (segmentsPerCorner + 1));
            for (const auto &arc : arcs) {
                for (int segment = 0; segment <= segmentsPerCorner; ++segment) {
                    const float angle = arc[2] + 0.5f*pi*segment/segmentsPerCorner;
                    const float x = arc[0] + radius*std::cos(angle);
                    const float y = arc[1] + radius*std::sin(angle);
                    p.corners.push_back(add(add(center, scale(basis.right, x)), scale(basis.down, y)));
                }
            }
        }
        p.color = rgb;
        for (auto corner : p.corners) p.depth += rotate(corner).z;
        p.depth /= static_cast<float>(p.corners.size());
        drawPolys.push_back(p);
    };
    const std::array<float,3> shell{0.055f,0.058f,0.063f};
    for (int ix=0; ix<3; ++ix) for (int iy=0; iy<3; ++iy) for (int iz=0; iz<3; ++iz) {
        Point cubie{centers[ix],centers[iy],centers[iz]};
        for (int f=0; f<6; ++f) {
            const auto &basis=faceBases[f];
            if (rotate(basis.normal).z <= 0.001f) continue;
            Point faceCenter=add(cubie,scale(basis.normal,0.34f));
            add_poly(faceCenter,basis,0.34f,shell);
            bool onOuterFace=(f==0&&iy==2)||(f==1&&ix==2)||(f==2&&iz==2)||
                             (f==3&&iy==0)||(f==4&&ix==0)||(f==5&&iz==0);
            if (!onOuterFace) continue;
            int row = f==0 ? iz : f==3 ? 2-iz : 2-iy;
            int col = f==1 ? 2-iz : f==4 ? iz : (f==5 ? 2-ix : ix);
            Point stickerCenter=add(cubie,scale(basis.normal,0.342f));
            auto rgb=sticker_rgb(cube.stickers[f*9+row*3+col]);
            Point normal=rotate(basis.normal);
            float light=0.72f+0.28f*std::max(0.0f,normal.x*0.35f+normal.y*0.65f+normal.z*0.68f);
            for(float &channel:rgb) channel*=light;
            add_poly(stickerCenter,basis,0.316f,rgb,0.045f);
        }
    }
    std::sort(drawPolys.begin(),drawPolys.end(),[](const auto&a,const auto&b){return a.depth<b.depth;});
    auto project = [](Point p) {
        p = rotate(p); float d = std::max(1.2f, cameraDistance - p.z); float scale = 2.7f / d;
        float aspectFix = static_cast<float>(extent.height) / static_cast<float>(extent.width);
        return Point{p.x * scale * aspectFix, -p.y * scale, std::clamp(0.78f-p.z*0.10f,0.0f,1.0f)};
    };
    size_t n = 0;
    for (const auto &poly : drawPolys) {
        auto emit = [&](int i) { Point p = project(poly.corners[i]); mappedVertices[n++] = {p.x,p.y,p.z,poly.color[0],poly.color[1],poly.color[2]}; };
        for (size_t i = 1; i + 1 < poly.corners.size(); ++i) {
            emit(0); emit(static_cast<int>(i)); emit(static_cast<int>(i + 1));
        }
    }
    activeVertexCount = static_cast<uint32_t>(n);
}
void draw_controls() {
    ImGui::SetNextWindowPos({12, 12}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({270, 0}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Cube controls", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("Rubik's Cube");
    ImGui::Separator();
    ImGui::TextWrapped("Middle-drag to orbit; Ctrl+middle-drag to dolly. Use face buttons or U R F D L B keys; hold Shift for inverse turns.");
    ImGui::Spacing();
    const char *faces[] = {"U", "R", "F", "D", "L", "B"};
    for (int i = 0; i < 6; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::Button(faces[i], {30, 30})) cube_apply_move(&cube, i * 3);
    }
    for (int i = 0; i < 6; ++i) {
        if (i) ImGui::SameLine();
        std::string label = std::string(faces[i]) + "'";
        if (ImGui::Button(label.c_str(), {30, 28})) cube_apply_move(&cube, i * 3 + 2);
    }
    if (ImGui::Button("Scramble", {-1, 32})) {
        std::uniform_int_distribution<int> faceDist(0, 5), turnDist(0, 2);
        lastScramble.clear(); int prev = -1;
        for (int i = 0; i < 20; ++i) {
            int f; do { f = faceDist(rng); } while (f == prev);
            int m = f * 3 + turnDist(rng); cube_apply_move(&cube, m);
            if (i) lastScramble += ' ';
            lastScramble += cube_move_name(m); prev = f;
        }
    }
    if (ImGui::Button("Reset cube", {-1, 30})) { cube_init(&cube); lastScramble.clear(); }
    if (ImGui::Button("Reset view", {-1, 30})) { yaw = kHomeYaw; pitch = kHomePitch; cameraDistance = kHomeCameraDistance; }
    ImGui::Text("State: %s", cube_is_solved(&cube) ? "solved" : "scrambled");
    if (!lastScramble.empty()) { ImGui::TextUnformatted("Last scramble:"); ImGui::TextWrapped("%s", lastScramble.c_str()); }
    ImGui::End();
}
void update_input() {
    ImGuiIO &io = ImGui::GetIO(); double x, y; glfwGetCursorPos(window, &x, &y);
    const bool middleDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    bool control = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    if (!middleDown) {
        dragging = false;
    } else if (!middleButtonWasDown) {
        // Only start orbiting when the press begins over the 3D view.
        dragging = !io.WantCaptureMouse;
        lastX = x;
        lastY = y;
    } else if (dragging) {
        // Once started, keep the drag active until the middle button is released,
        // even if the cursor passes over an ImGui window.
        int windowWidth = 1, windowHeight = 1;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        if (control) {
            const float deltaY = static_cast<float>(y - lastY);
            cameraDistance *= std::exp(deltaY * 1.5f / std::max(windowHeight, 1));
            cameraDistance = std::clamp(cameraDistance, 2.4f, 12.0f);
        } else {
            yaw += static_cast<float>(x - lastX) * (2.0f * 3.14159265f / std::max(windowWidth, 1));
            pitch += static_cast<float>(y - lastY) * (2.0f * 3.14159265f / std::max(windowHeight, 1));
            pitch = std::clamp(pitch, -1.55334f, 1.55334f);
        }
        lastX = x;
        lastY = y;
    }
    middleButtonWasDown = middleDown;
    struct KeyMove { int key, face; } keys[] = {{GLFW_KEY_U,0},{GLFW_KEY_R,1},{GLFW_KEY_F,2},{GLFW_KEY_D,3},{GLFW_KEY_L,4},{GLFW_KEY_B,5}};
    static bool wasDown[6]{};
    for (auto k : keys) {
        bool now = glfwGetKey(window, k.key) == GLFW_PRESS;
        if (now && !wasDown[k.face]) {
            bool inverse = shift;
            cube_apply_move(&cube, k.face * 3 + (inverse ? 2 : 0));
        }
        wasDown[k.face] = now;
    }
}
void draw_frame() {
    vkWaitForFences(device, 1, &frameFence, VK_TRUE, UINT64_MAX);
    uint32_t imageIndex;
    VkResult acquire = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) { swapchainOutOfDate = true; return; }
    if (acquire == VK_SUBOPTIMAL_KHR) swapchainOutOfDate = true;
    vk_check(acquire, "vkAcquireNextImageKHR");
    vkResetFences(device, 1, &frameFence); vkResetCommandBuffer(commandBuffer, 0);
    build_cube_vertices();
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vk_check(vkBeginCommandBuffer(commandBuffer, &bi), "vkBeginCommandBuffer");
    float background = 28.0f / 255.0f;
    if (swapFormat == VK_FORMAT_B8G8R8A8_SRGB || swapFormat == VK_FORMAT_R8G8B8A8_SRGB) background = srgb_to_linear(background);
    VkClearValue clear[2]{}; clear[0].color = {{background, background, background, 1.0f}}; clear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; ri.renderPass = renderPass; ri.framebuffer = framebuffers[imageIndex]; ri.renderArea.extent = extent; ri.clearValueCount = 2; ri.pClearValues = clear;
    vkCmdBeginRenderPass(commandBuffer, &ri, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, 0, (float)extent.width, (float)extent.height, 0, 1}; VkRect2D sc{{0,0},extent};
    vkCmdSetViewport(commandBuffer, 0, 1, &vp); vkCmdSetScissor(commandBuffer, 0, 1, &sc);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkDeviceSize offset = 0; vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);
    vkCmdDraw(commandBuffer, activeVertexCount, 1, 0, 0);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    vkCmdEndRenderPass(commandBuffer); vk_check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.waitSemaphoreCount = 1; si.pWaitSemaphores = &imageAvailable; si.pWaitDstStageMask = &waitStage; si.commandBufferCount = 1; si.pCommandBuffers = &commandBuffer; si.signalSemaphoreCount = 1; si.pSignalSemaphores = &renderFinished;
    vk_check(vkQueueSubmit(graphicsQueue, 1, &si, frameFence), "vkQueueSubmit");
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &renderFinished; pi.swapchainCount = 1; pi.pSwapchains = &swapchain; pi.pImageIndices = &imageIndex;
    VkResult presented = vkQueuePresentKHR(presentQueue, &pi);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) swapchainOutOfDate = true;
    else vk_check(presented, "vkQueuePresentKHR");
    /* Keep the single presentation semaphore safe to reuse in this one-frame loop. */
    vkQueueWaitIdle(presentQueue);
}
void cleanup() {
    if (device) vkDeviceWaitIdle(device);
    if (imguiVulkanReady) ImGui_ImplVulkan_Shutdown();
    if (imguiGlfwReady) ImGui_ImplGlfw_Shutdown();
    if (imguiContextCreated) ImGui::DestroyContext();
    if (device) {
        if (mappedVertices) vkUnmapMemory(device, vertexMemory);
        if (vertexBuffer) vkDestroyBuffer(device, vertexBuffer, nullptr); if (vertexMemory) vkFreeMemory(device, vertexMemory, nullptr);
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr); if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        for (auto f : framebuffers) vkDestroyFramebuffer(device, f, nullptr);
        if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
        if (msaaColorView) vkDestroyImageView(device, msaaColorView, nullptr); if (depthView) vkDestroyImageView(device, depthView, nullptr);
        if (msaaColorImage) vkDestroyImage(device, msaaColorImage, nullptr); if (depthImage) vkDestroyImage(device, depthImage, nullptr);
        if (msaaColorMemory) vkFreeMemory(device, msaaColorMemory, nullptr); if (depthMemory) vkFreeMemory(device, depthMemory, nullptr);
        for (auto v : imageViews) vkDestroyImageView(device, v, nullptr);
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
        if (imageAvailable) vkDestroySemaphore(device, imageAvailable, nullptr); if (renderFinished) vkDestroySemaphore(device, renderFinished, nullptr); if (frameFence) vkDestroyFence(device, frameFence, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (surface) vkDestroySurfaceKHR(instance, surface, nullptr); if (instance) vkDestroyInstance(instance, nullptr);
    if (window) glfwDestroyWindow(window); glfwTerminate();
}
} // namespace

int main() {
    try {
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(kWidth, kHeight, "Rubik's Cube — Vulkan + ImGui", nullptr, nullptr);
        if (!window) throw std::runtime_error("Could not create GLFW window");
        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        cube_init(&cube); init_vulkan();
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            if (framebufferResized || swapchainOutOfDate) recreate_swapchain();
            if (glfwWindowShouldClose(window)) break;
            ImGui_ImplVulkan_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
            update_input(); draw_controls(); ImGui::Render(); draw_frame();
        }
        cleanup();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "rubiks_viewer: %s\n", e.what()); cleanup(); return 1;
    }
    return 0;
}
