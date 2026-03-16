#define TINYOBJLOADER_IMPLEMENTATION
#include <tinyobjloader/tiny_obj_loader.h>

#include <vulkan/vulkan.h>

#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <optional>
#include <set>
#include <limits>
#include <algorithm>
#include <fstream>
#include <array>

bool exitWindow = false;
static bool isMoveForward = false, isMoveBack = false;
static bool isTurnLeft = false, isTurnRight = false;

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;
const std::string MODEL_PATH = "./static/models/cube_scene.obj";

std::vector<const char *> instanceLayers = {"VK_LAYER_KHRONOS_validation"};
std::vector<const char *> instanceExtensions = {"VK_KHR_win32_surface", "VK_KHR_surface", "VK_EXT_debug_utils"};

std::vector<const char *> deviceExtensions = {
    "VK_KHR_ray_tracing_pipeline",
    "VK_KHR_acceleration_structure",
    "VK_EXT_descriptor_indexing",
    "VK_KHR_buffer_device_address",
    "VK_KHR_deferred_host_operations",
    "VK_KHR_maintenance3",
    "VK_KHR_swapchain"};

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else
    {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks *pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        func(instance, debugMessenger, pAllocator);
    }
}

struct QueueFamilyIndices
{
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete()
    {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct UniformBufferObject
{
    glm::vec4 cameraPosition;
    glm::vec4 cameraRight;
    glm::vec4 cameraUp;
    glm::vec4 cameraForward;

    uint32_t frameCount;
} uniformBufferObject;

struct Material
{
    float ambient[4] = {0, 0, 0, 0};
    float diffuse[4] = {0, 0, 0, 0};
    float specular[4] = {0, 0, 0, 0};
    float emission[4] = {0, 0, 0, 0};
};

class HelloVulkanRayTracingApplication
{
public:
    void run()
    {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow *window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    // 交换链
    VkSwapchainKHR swapChain;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;

    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers = std::vector<VkCommandBuffer>(16);

    VkDescriptorPool descriptorPool;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSetLayout materialDescriptorSetLayout;
    std::vector<VkDescriptorSet> descriptorSets;
    VkPipelineLayout pipelineLayout;
    VkPipeline rayTracingPipeline;

    // 光线追踪扩展函数指针
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = nullptr;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR = nullptr;

    uint32_t primitiveCount = 0;   // 总面数
    std::vector<float> vertices;   // 顶点数组
    std::vector<uint32_t> indices; // 索引数组

    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkDeviceAddress vertexBufferDeviceAddress;

    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    VkDeviceAddress indexBufferDeviceAddress;

    VkBuffer bottomLevelAccelerationStructureBuffer;
    VkDeviceMemory bottomLevelAccelerationStructureBufferMemory;
    VkAccelerationStructureKHR bottomLevelAccelerationStructure;

    VkBuffer topLevelAccelerationStructureBuffer;
    VkDeviceMemory topLevelAccelerationStructureBufferMemory;
    VkAccelerationStructureKHR topLevelAccelerationStructure;

    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;

    VkImage rayTracingImage;
    VkDeviceMemory rayTracingImageMemory;
    VkImageView rayTracingImageView;

    std::vector<uint32_t> materialIndex;
    VkBuffer materialIndexBuffer;
    VkDeviceMemory materialIndexBufferMemory;
    std::vector<Material> materialList;
    VkBuffer materialBuffer;
    VkDeviceMemory materialBufferMemory;

    VkBuffer shaderBindingTableBuffer;
    VkDeviceMemory shaderBindingTableBufferMemory;

    // closest hit shader group
    VkStridedDeviceAddressRegionKHR rchitShaderBindingTable;
    // ray gen shader group
    VkStridedDeviceAddressRegionKHR rgenShaderBindingTable;
    // miss shader group
    VkStridedDeviceAddressRegionKHR rmissShaderBindingTable;
    // callable shader group
    VkStridedDeviceAddressRegionKHR callableShaderBindingTable;

    std::vector<VkFence> imageAvailableFence;
    std::vector<VkSemaphore> acquireImageSemaphore;
    std::vector<VkSemaphore> writeImageSemaphore;

    uint32_t currentFrame = 0;

    void initWindow()
    {
        glfwInit();                                   // 初始化GLFW库
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // 不创建OpenGL上下文
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);   // 窗口不可调整大小
        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    }

    void initVulkan()
    {
        // Core Initialization
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        devicePointerFunctions();

        // SwapChain
        createSwapchain();
        createSwapChainImageViews();

        // Command System
        createCommandPool();
        createCommandBuffer();

        // Resources
        loadModel();
        createVertexBuffer();
        createIndexBuffer();
        createMaterialIndexBuffer();
        createMaterialBuffer();

        createUniformBuffer();

        createRayTracingImage();

        createBottomLevelAccelerationStructure();
        createTopLevelAccelerationStructure();

        createDescriptorSetLayout();
        createMaterialDescriptorSetLayout();
        createDescriptorPool();
        createDescriptorSet();
        createRayTracingPipeline();
        createShaderBindingTable();
    }

    void createInstance()
    {
        VkApplicationInfo applicationInfo = {};
        applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        applicationInfo.pApplicationName = "Hello Vulkan Ray Tracing";
        applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        applicationInfo.pEngineName = "No Engine";
        applicationInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        applicationInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &applicationInfo;
        createInfo.enabledExtensionCount =
            static_cast<uint32_t>(instanceExtensions.size());
        createInfo.ppEnabledExtensionNames = instanceExtensions.data();
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
        createInfo.enabledLayerCount =
            static_cast<uint32_t>(instanceLayers.size());
        createInfo.ppEnabledLayerNames = instanceLayers.data();

        if (vkCreateInstance(&createInfo, NULL, &instance) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create instance!");
        }
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, // 消息严重程度
        VkDebugUtilsMessageTypeFlagsEXT messageType,            // 消息类型
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData)
    {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

        return VK_FALSE; // 返回 VK_FALSE 以继续 Vulkan 操作
    }

    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo)
    {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
        createInfo.pUserData = nullptr; // 可选的用户数据
    }

    void createSurface()
    {
        if (glfwCreateWindowSurface(instance, window, NULL, &surface) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create window surface!");
        }
    }

    void pickPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
        if (deviceCount == 0)
        {
            throw std::runtime_error("failed to find GPUs with Vulkan support!");
        }
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        for (const auto &device : devices)
        {
            if (isDeviceSuitable(device))
            {
                physicalDevice = device;
                break;
            }
        }

        if (physicalDevice == VK_NULL_HANDLE)
        {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
    }

    bool isDeviceSuitable(VkPhysicalDevice physicalDevice)
    {
        // 检查设备是否满足应用程序需求（例如支持必要的队列簇、设备扩展和特性）
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice); // 检查队列簇支持

        bool extensionsSupported = checkDeviceExtensionSupport(physicalDevice);
        bool swapChainAdequate = false;
        if (extensionsSupported)
        {
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }
        return indices.isComplete() && extensionsSupported && swapChainAdequate;
    }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device)
    {
        QueueFamilyIndices indices;
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto &queueFamily : queueFamilies)
        {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                indices.graphicsFamily = i;
            }
            // 查找支持显示操作的队列簇
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if (presentSupport)
            {
                indices.presentFamily = i;
            }
            if (indices.isComplete())
            {
                break;
            }
            i++;
        }
        return indices;
    }

    bool checkDeviceExtensionSupport(VkPhysicalDevice device)
    {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
        // 从可用扩展列表中移除已找到的扩展，最终检查是否所有所需扩展都已找到
        for (const auto &extension : availableExtensions)
        {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    void createLogicalDevice()
    {
        // 设备功能集，启用各个功能以满足应用程序需求（例如纹理采样器各向异性过滤）
        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = VK_TRUE;
        deviceFeatures.geometryShader = VK_TRUE;
        // 队列创建信息，指定逻辑设备需要创建的队列信息（多队列则配置多个VkDeviceQueueCreateInfo结构体）
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        std::set<uint32_t> uniqueQueueFamilies = {
            indices.graphicsFamily.value(),
            indices.presentFamily.value()};
        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies)
        {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        //  Buffer Device Address features，用于启用缓冲区设备地址功能，允许在着色器中直接访问缓冲区内存地址
        VkPhysicalDeviceBufferDeviceAddressFeatures physicalDeviceBufferDeviceAddressFeatures = {};
        physicalDeviceBufferDeviceAddressFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        physicalDeviceBufferDeviceAddressFeatures.pNext = NULL;
        physicalDeviceBufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;               // 启用缓冲区设备地址功能
        physicalDeviceBufferDeviceAddressFeatures.bufferDeviceAddressCaptureReplay = VK_FALSE; // 不启用捕获重放功能
        physicalDeviceBufferDeviceAddressFeatures.bufferDeviceAddressMultiDevice = VK_FALSE;   // 不启用多设备功能

        // Acceleration Structure features，用于启用加速结构功能，支持光线追踪中的加速结构构建和使用
        VkPhysicalDeviceAccelerationStructureFeaturesKHR physicalDeviceAccelerationStructureFeatures = {};
        physicalDeviceAccelerationStructureFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        physicalDeviceAccelerationStructureFeatures.pNext = &physicalDeviceBufferDeviceAddressFeatures;
        physicalDeviceAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
        physicalDeviceAccelerationStructureFeatures.accelerationStructureCaptureReplay = VK_FALSE;
        physicalDeviceAccelerationStructureFeatures.accelerationStructureIndirectBuild = VK_FALSE;
        physicalDeviceAccelerationStructureFeatures.accelerationStructureHostCommands = VK_FALSE;
        physicalDeviceAccelerationStructureFeatures.descriptorBindingAccelerationStructureUpdateAfterBind = VK_FALSE;

        // Ray Tracing Pipeline features，用于启用光线追踪管线功能，支持光线追踪管线的创建和使用
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR physicalDeviceRayTracingPipelineFeatures = {};
        physicalDeviceRayTracingPipelineFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        physicalDeviceRayTracingPipelineFeatures.pNext = &physicalDeviceAccelerationStructureFeatures;
        physicalDeviceRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
        physicalDeviceRayTracingPipelineFeatures.rayTracingPipelineShaderGroupHandleCaptureReplay = VK_FALSE;
        physicalDeviceRayTracingPipelineFeatures.rayTracingPipelineShaderGroupHandleCaptureReplayMixed = VK_FALSE;
        physicalDeviceRayTracingPipelineFeatures.rayTracingPipelineTraceRaysIndirect = VK_FALSE;
        physicalDeviceRayTracingPipelineFeatures.rayTraversalPrimitiveCulling = VK_FALSE;

        // 逻辑设备创建信息
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &physicalDeviceRayTracingPipelineFeatures; // 将启用的功能链式连接到pNext字段
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();
        createInfo.enabledLayerCount = 0;
        createInfo.ppEnabledLayerNames = nullptr;
        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create logical device!");
        }

        vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
        vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
    }

    void devicePointerFunctions()
    {
        // Device Pointer Functions
        vkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddressKHR");
        vkCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesKHR");
        vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR");
        vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR");
        vkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR");
        vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR");
        vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR");
        vkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesKHR");
        vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR");
    }

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device)
    {
        // 基本表面功能
        SwapChainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);
        // 表面格式
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
        if (formatCount != 0)
        {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
        }
        // 呈现模式
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
        if (presentModeCount != 0)
        {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats)
    {
        for (const auto &availableFormat : availableFormats)
        {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM &&
                availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return availableFormat;
            }
        }
        return availableFormats[0];
    };

    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes)
    {
        for (const auto &availablePresentMode : availablePresentModes)
        {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return availablePresentMode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    };

    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities)
    {
        if (capabilities.currentExtent.width != UINT32_MAX)
        {
            return capabilities.currentExtent;
        }
        else
        {
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);

            VkExtent2D actualExtent = {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)};

            actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
            actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

            return actualExtent;
        }
    };

    void createSwapchain()
    {
        // 查询交换链支持细节，选择最佳的表面格式、呈现模式和交换链扩展
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);
        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
        {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        // 创建交换链
        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        if (indices.graphicsFamily != indices.presentFamily)
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;     // Optional
            createInfo.pQueueFamilyIndices = nullptr; // Optional
        }

        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create swap chain!");
        }

        vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
        swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());
        swapChainImageFormat = surfaceFormat.format;
        swapChainExtent = extent;
    }

    void createSwapChainImageViews()
    {
        swapChainImageViews.resize(swapChainImages.size());
        for (size_t i = 0; i < swapChainImages.size(); i++)
        {
            swapChainImageViews[i] = createImageView(swapChainImages[i], swapChainImageFormat,
                                                     VK_IMAGE_ASPECT_COLOR_BIT, 1);
        }
    }

    void createImage(uint32_t width, uint32_t height,
                     uint32_t mipLevels, VkSampleCountFlagBits numSamples,
                     VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                     VkImage &image, VkDeviceMemory &imageMemory)
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = tiling;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = numSamples;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create image!");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate image memory!");
        }

        vkBindImageMemory(device, image, imageMemory, 0);
    }

    VkImageView createImageView(VkImage image, VkFormat format,
                                VkImageAspectFlags aspectFlags, uint32_t mipLevels)
    {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = image;                    // 视图对应的图像
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; // 视图类型，如2D
        createInfo.format = format;                  // 视图格式，与VkImage格式一致
        // 视图的组件映射Swizzle，用于重映射 RGBA 通道
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY; // 该参数代表不做任何重映射
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = aspectFlags;
        // mipmap 访问范围
        createInfo.subresourceRange.baseMipLevel = 0;       // 从第 0 级 mipmap 开始
        createInfo.subresourceRange.levelCount = mipLevels; // 包含的 mipmap 级别数量
        // 图像层数
        createInfo.subresourceRange.baseArrayLayer = 0; // 从第 0 层开始（对于非数组纹理）
        createInfo.subresourceRange.layerCount = 1;     // 只使用 1 层（对于非立方体贴图/数组）

        VkImageView imageView;
        if (vkCreateImageView(device, &createInfo, nullptr, &imageView) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create image views!");
        }
        return imageView;
    };

    void createCommandPool()
    {
        QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create command pool!");
        }
    }

    void createCommandBuffer()
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

        if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate command buffer!");
        }
    }

    void createDescriptorSetLayout()
    {
        // Descriptor Set Layout
        // 定义描述符集布局绑定，指定每个绑定点的描述符类型、数量和着色器阶段
        // uniform acceleration structure
        VkDescriptorSetLayoutBinding accelerationStructureLayoutBinding{};
        accelerationStructureLayoutBinding.binding = 0;
        accelerationStructureLayoutBinding.descriptorType =
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        accelerationStructureLayoutBinding.descriptorCount = 1;
        accelerationStructureLayoutBinding.stageFlags =
            VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        accelerationStructureLayoutBinding.pImmutableSamplers = NULL;

        // uniform camera
        VkDescriptorSetLayoutBinding cameraUniformBufferLayoutBinding{};
        cameraUniformBufferLayoutBinding.binding = 1;
        cameraUniformBufferLayoutBinding.descriptorType =
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        cameraUniformBufferLayoutBinding.descriptorCount = 1;
        cameraUniformBufferLayoutBinding.stageFlags =
            VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        cameraUniformBufferLayoutBinding.pImmutableSamplers = NULL;

        // index buffer
        VkDescriptorSetLayoutBinding indexBufferStorageBufferLayoutBinding{};
        indexBufferStorageBufferLayoutBinding.binding = 2;
        indexBufferStorageBufferLayoutBinding.descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        indexBufferStorageBufferLayoutBinding.descriptorCount = 1;
        indexBufferStorageBufferLayoutBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        indexBufferStorageBufferLayoutBinding.pImmutableSamplers = NULL;

        // vertex buffer
        VkDescriptorSetLayoutBinding vertexBufferStorageBufferLayoutBinding{};
        vertexBufferStorageBufferLayoutBinding.binding = 3;
        vertexBufferStorageBufferLayoutBinding.descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vertexBufferStorageBufferLayoutBinding.descriptorCount = 1;
        vertexBufferStorageBufferLayoutBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        vertexBufferStorageBufferLayoutBinding.pImmutableSamplers = NULL;

        // storage image
        VkDescriptorSetLayoutBinding storageImageLayoutBinding{};
        storageImageLayoutBinding.binding = 4;
        storageImageLayoutBinding.descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        storageImageLayoutBinding.descriptorCount = 1;
        storageImageLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        storageImageLayoutBinding.pImmutableSamplers = NULL;

        std::vector<VkDescriptorSetLayoutBinding>
            bindings = {
                accelerationStructureLayoutBinding,
                cameraUniformBufferLayoutBinding,
                indexBufferStorageBufferLayoutBinding,
                vertexBufferStorageBufferLayoutBinding,
                storageImageLayoutBinding};

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
        descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutCreateInfo.pNext = NULL;
        descriptorSetLayoutCreateInfo.flags = 0;
        descriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        descriptorSetLayoutCreateInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &descriptorSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create descriptor set layout!");
        }
    }

    void createMaterialDescriptorSetLayout()
    {
        // Material Descriptor Set Layout
        VkDescriptorSetLayoutBinding materialIndexStorageBufferLayoutBinding{};
        materialIndexStorageBufferLayoutBinding.binding = 0;
        materialIndexStorageBufferLayoutBinding.descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        materialIndexStorageBufferLayoutBinding.descriptorCount = 1;
        materialIndexStorageBufferLayoutBinding.stageFlags =
            VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        materialIndexStorageBufferLayoutBinding.pImmutableSamplers = NULL;

        VkDescriptorSetLayoutBinding materialStorageBufferLayoutBinding{};
        materialStorageBufferLayoutBinding.binding = 1;
        materialStorageBufferLayoutBinding.descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        materialStorageBufferLayoutBinding.descriptorCount = 1;
        materialStorageBufferLayoutBinding.stageFlags =
            VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        materialStorageBufferLayoutBinding.pImmutableSamplers = NULL;

        std::vector<VkDescriptorSetLayoutBinding>
            bindings = {
                materialIndexStorageBufferLayoutBinding,
                materialStorageBufferLayoutBinding};

        VkDescriptorSetLayoutCreateInfo materialDescriptorSetLayoutCreateInfo{};
        materialDescriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        materialDescriptorSetLayoutCreateInfo.pNext = NULL;
        materialDescriptorSetLayoutCreateInfo.flags = 0;
        materialDescriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        materialDescriptorSetLayoutCreateInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &materialDescriptorSetLayoutCreateInfo, NULL, &materialDescriptorSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create material descriptor set layout!");
        }
    }

    void createDescriptorPool()
    {
        // 描述符池大小，指定每种类型的描述符数量，以满足应用程序的需求
        VkDescriptorPoolSize accelerationStructurePoolSize{};
        accelerationStructurePoolSize.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        accelerationStructurePoolSize.descriptorCount = 1;

        VkDescriptorPoolSize uniformBufferPoolSize{};
        uniformBufferPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformBufferPoolSize.descriptorCount = 1;

        VkDescriptorPoolSize storageBufferPoolSize{};
        storageBufferPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        storageBufferPoolSize.descriptorCount = 4;

        VkDescriptorPoolSize storageImagePoolSize{};
        storageImagePoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        storageImagePoolSize.descriptorCount = 1;

        std::array<VkDescriptorPoolSize, 4> poolSizes = {
            accelerationStructurePoolSize,
            uniformBufferPoolSize,
            storageBufferPoolSize,
            storageImagePoolSize};

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 2;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create descriptor pool!");
        }
    }

    void createDescriptorSet()
    {
        // Allocate Descriptor Sets
        std::vector<VkDescriptorSetLayout> layouts = {descriptorSetLayout, materialDescriptorSetLayout};

        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = descriptorPool;
        allocateInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocateInfo.pSetLayouts = layouts.data();

        descriptorSets.resize(layouts.size());
        if (vkAllocateDescriptorSets(device, &allocateInfo, descriptorSets.data()) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate descriptor sets!");
        }

        // Update Descriptor Set
        VkWriteDescriptorSetAccelerationStructureKHR accelerationStructureDescriptorInfo{};
        accelerationStructureDescriptorInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        accelerationStructureDescriptorInfo.pNext = NULL;
        accelerationStructureDescriptorInfo.accelerationStructureCount = 1;
        accelerationStructureDescriptorInfo.pAccelerationStructures = &topLevelAccelerationStructure;

        VkDescriptorBufferInfo uniformDescriptorInfo{};
        uniformDescriptorInfo.buffer = uniformBuffer;
        uniformDescriptorInfo.offset = 0;
        uniformDescriptorInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo indexDescriptorInfo{};
        indexDescriptorInfo.buffer = indexBuffer;
        indexDescriptorInfo.offset = 0;
        indexDescriptorInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo vertexDescriptorInfo{};
        vertexDescriptorInfo.buffer = vertexBuffer;
        vertexDescriptorInfo.offset = 0;
        vertexDescriptorInfo.range = VK_WHOLE_SIZE;

        VkDescriptorImageInfo rayTraceImageDescriptorInfo{};
        rayTraceImageDescriptorInfo.sampler = VK_NULL_HANDLE;
        rayTraceImageDescriptorInfo.imageView = rayTracingImageView;
        rayTraceImageDescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        std::array<VkWriteDescriptorSet, 5> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].pNext = &accelerationStructureDescriptorInfo;
        descriptorWrites[0].dstSet = descriptorSets[0];
        descriptorWrites[0].dstBinding = 0; // 加速结构绑定到绑定点0
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        descriptorWrites[0].pImageInfo = NULL;
        descriptorWrites[0].pBufferInfo = NULL;
        descriptorWrites[0].pTexelBufferView = NULL;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].pNext = NULL;
        descriptorWrites[1].dstSet = descriptorSets[0];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].pImageInfo = NULL;
        descriptorWrites[1].pBufferInfo = &uniformDescriptorInfo;
        descriptorWrites[1].pTexelBufferView = NULL;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].pNext = NULL;
        descriptorWrites[2].dstSet = descriptorSets[0];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[2].pImageInfo = NULL;
        descriptorWrites[2].pBufferInfo = &indexDescriptorInfo;
        descriptorWrites[2].pTexelBufferView = NULL;

        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].pNext = NULL;
        descriptorWrites[3].dstSet = descriptorSets[0];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[3].pImageInfo = NULL;
        descriptorWrites[3].pBufferInfo = &vertexDescriptorInfo;
        descriptorWrites[3].pTexelBufferView = NULL;

        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].pNext = NULL;
        descriptorWrites[4].dstSet = descriptorSets[0];
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].dstArrayElement = 0;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        descriptorWrites[4].pImageInfo = &rayTraceImageDescriptorInfo;
        descriptorWrites[4].pBufferInfo = NULL;
        descriptorWrites[4].pTexelBufferView = NULL;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, NULL);

        // Update Material Descriptor Set
        VkDescriptorBufferInfo materialIndexDescriptorInfo{};
        materialIndexDescriptorInfo.buffer = materialIndexBuffer;
        materialIndexDescriptorInfo.offset = 0;
        materialIndexDescriptorInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo materialDescriptorInfo{};
        materialDescriptorInfo.buffer = materialBuffer;
        materialDescriptorInfo.offset = 0;
        materialDescriptorInfo.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, 2> materialDescriptorWrites{};
        materialDescriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        materialDescriptorWrites[0].pNext = NULL;
        materialDescriptorWrites[0].dstSet = descriptorSets[1];
        materialDescriptorWrites[0].dstBinding = 0;
        materialDescriptorWrites[0].dstArrayElement = 0;
        materialDescriptorWrites[0].descriptorCount = 1;
        materialDescriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        materialDescriptorWrites[0].pImageInfo = NULL;
        materialDescriptorWrites[0].pBufferInfo = &materialIndexDescriptorInfo;
        materialDescriptorWrites[0].pTexelBufferView = NULL;

        materialDescriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        materialDescriptorWrites[1].pNext = NULL;
        materialDescriptorWrites[1].dstSet = descriptorSets[1];
        materialDescriptorWrites[1].dstBinding = 1;
        materialDescriptorWrites[1].dstArrayElement = 0;
        materialDescriptorWrites[1].descriptorCount = 1;
        materialDescriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        materialDescriptorWrites[1].pImageInfo = NULL;
        materialDescriptorWrites[1].pBufferInfo = &materialDescriptorInfo;
        materialDescriptorWrites[1].pTexelBufferView = NULL;

        vkUpdateDescriptorSets(device, materialDescriptorWrites.size(),
                               materialDescriptorWrites.data(), 0, NULL);
    }

    static std::vector<char>
    readFile(const std::string &filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("failed to open file!");
        }
        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    }

    VkShaderModule createShaderModule(const std::vector<char> &code)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create shader module!");
        }

        return shaderModule;
    }

    void createRayTracingPipeline()
    {
        auto rayClosestHitShaderCode = readFile("./src/shaders/shader.rchit.spv");
        auto rayGenerateShaderCode = readFile("./src/shaders/shader.rgen.spv");
        auto rayMissShaderCode = readFile("./src/shaders/shader.rmiss.spv");
        auto rayMissShadowShaderCode = readFile("./src/shaders/shader_shadow.rmiss.spv");

        VkShaderModule rayClosestHitShaderModule = createShaderModule(rayClosestHitShaderCode);
        VkShaderModule rayGenerateShaderModule = createShaderModule(rayGenerateShaderCode);
        VkShaderModule rayMissShaderModule = createShaderModule(rayMissShaderCode);
        VkShaderModule rayMissShadowShaderModule = createShaderModule(rayMissShadowShaderCode);

        // Ray Tracing Pipeline
        // Shader stages
        VkPipelineShaderStageCreateInfo rayGenerateShaderStageCreateInfo{};
        rayGenerateShaderStageCreateInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        rayGenerateShaderStageCreateInfo.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        rayGenerateShaderStageCreateInfo.module = rayGenerateShaderModule;
        rayGenerateShaderStageCreateInfo.pName = "main";
        rayGenerateShaderStageCreateInfo.pSpecializationInfo = NULL;

        VkPipelineShaderStageCreateInfo rayClosestHitShaderStageCreateInfo{};
        rayClosestHitShaderStageCreateInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        rayClosestHitShaderStageCreateInfo.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        rayClosestHitShaderStageCreateInfo.module = rayClosestHitShaderModule;
        rayClosestHitShaderStageCreateInfo.pName = "main";
        rayClosestHitShaderStageCreateInfo.pSpecializationInfo = NULL;

        VkPipelineShaderStageCreateInfo rayMissShaderStageCreateInfo{};
        rayMissShaderStageCreateInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        rayMissShaderStageCreateInfo.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
        rayMissShaderStageCreateInfo.module = rayMissShaderModule;
        rayMissShaderStageCreateInfo.pName = "main";
        rayMissShaderStageCreateInfo.pSpecializationInfo = NULL;

        VkPipelineShaderStageCreateInfo rayMissShadowShaderStageCreateInfo{};
        rayMissShadowShaderStageCreateInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        rayMissShadowShaderStageCreateInfo.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
        rayMissShadowShaderStageCreateInfo.module = rayMissShadowShaderModule;
        rayMissShadowShaderStageCreateInfo.pName = "main";
        rayMissShadowShaderStageCreateInfo.pSpecializationInfo = NULL;

        VkPipelineShaderStageCreateInfo shaderStages[] = {
            rayGenerateShaderStageCreateInfo,
            rayClosestHitShaderStageCreateInfo,
            rayMissShaderStageCreateInfo,
            rayMissShadowShaderStageCreateInfo};

        // Shader groups,指定每个着色器阶段所属的着色器组，以及着色器组的类型
        VkRayTracingShaderGroupCreateInfoKHR rayGenerateShaderGroupCreateInfo{};
        rayGenerateShaderGroupCreateInfo.sType =
            VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        rayGenerateShaderGroupCreateInfo.pNext = NULL;
        // ray generation着色器属于general组
        rayGenerateShaderGroupCreateInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        // ray generation索引为0
        rayGenerateShaderGroupCreateInfo.generalShader = 0;
        rayGenerateShaderGroupCreateInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
        rayGenerateShaderGroupCreateInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
        rayGenerateShaderGroupCreateInfo.intersectionShader = VK_SHADER_UNUSED_KHR;
        rayGenerateShaderGroupCreateInfo.pShaderGroupCaptureReplayHandle = NULL;

        VkRayTracingShaderGroupCreateInfoKHR rayClosestHitShaderGroupCreateInfo{};
        rayClosestHitShaderGroupCreateInfo.sType =
            VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        rayClosestHitShaderGroupCreateInfo.pNext = NULL;
        // closest hit着色器属于hit group，类型为triangles hit group
        rayClosestHitShaderGroupCreateInfo.type =
            VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        rayClosestHitShaderGroupCreateInfo.generalShader = VK_SHADER_UNUSED_KHR;
        // closest hit索引为1
        rayClosestHitShaderGroupCreateInfo.closestHitShader = 1;
        // 无透明度，any hit着色器未使用
        rayClosestHitShaderGroupCreateInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
        // 不需要交点着色器，硬件自动处理三角形求交
        rayClosestHitShaderGroupCreateInfo.intersectionShader = VK_SHADER_UNUSED_KHR;
        rayClosestHitShaderGroupCreateInfo.pShaderGroupCaptureReplayHandle = NULL;

        VkRayTracingShaderGroupCreateInfoKHR rayMissShaderGroupCreateInfo{};
        rayMissShaderGroupCreateInfo.sType =
            VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        rayMissShaderGroupCreateInfo.pNext = NULL;
        // 当主光线（相机发出的光线）未击中任何物体时执行，属于general组
        rayMissShaderGroupCreateInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        rayMissShaderGroupCreateInfo.generalShader = 2; // ray miss索引为2
        rayMissShaderGroupCreateInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
        rayMissShaderGroupCreateInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
        rayMissShaderGroupCreateInfo.intersectionShader = VK_SHADER_UNUSED_KHR;
        rayMissShaderGroupCreateInfo.pShaderGroupCaptureReplayHandle = NULL;

        VkRayTracingShaderGroupCreateInfoKHR rayMissShadowShaderGroupCreateInfo{};
        rayMissShadowShaderGroupCreateInfo.sType =
            VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        rayMissShadowShaderGroupCreateInfo.pNext = NULL;
        // 当阴影光线未击中任何物体时执行，属于general组
        rayMissShadowShaderGroupCreateInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        // shadow ray miss索引为3，当阴影光线未击中任何物体时执行
        rayMissShadowShaderGroupCreateInfo.generalShader = 3;
        rayMissShadowShaderGroupCreateInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
        rayMissShadowShaderGroupCreateInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
        rayMissShadowShaderGroupCreateInfo.intersectionShader = VK_SHADER_UNUSED_KHR;
        rayMissShadowShaderGroupCreateInfo.pShaderGroupCaptureReplayHandle = NULL;

        VkRayTracingShaderGroupCreateInfoKHR shaderGroups[] = {
            rayGenerateShaderGroupCreateInfo,
            rayClosestHitShaderGroupCreateInfo,
            rayMissShaderGroupCreateInfo,
            rayMissShadowShaderGroupCreateInfo}; // 决定了SBT中每个着色器的布局和索引

        // pipeline layout,指定管线布局，包含描述符集布局
        std::vector<VkDescriptorSetLayout> descriptorSetLayouts = {
            descriptorSetLayout,
            materialDescriptorSetLayout};
        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
        pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.pNext = NULL;
        pipelineLayoutCreateInfo.flags = 0;
        pipelineLayoutCreateInfo.setLayoutCount = descriptorSetLayouts.size();
        pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();

        if (vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, NULL, &pipelineLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create pipeline layout!");
        }

        // 光线追踪管线创建信息
        VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCreateInfo{};
        rayTracingPipelineCreateInfo.sType =
            VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        rayTracingPipelineCreateInfo.pNext = NULL;
        rayTracingPipelineCreateInfo.flags = 0;
        rayTracingPipelineCreateInfo.stageCount = 4;
        rayTracingPipelineCreateInfo.pStages = shaderStages; // 着色器阶段
        rayTracingPipelineCreateInfo.groupCount = 4;
        rayTracingPipelineCreateInfo.pGroups = shaderGroups; // 着色器组
        rayTracingPipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
        rayTracingPipelineCreateInfo.pLibraryInfo = NULL;
        rayTracingPipelineCreateInfo.pLibraryInterface = NULL;
        rayTracingPipelineCreateInfo.pDynamicState = NULL;
        rayTracingPipelineCreateInfo.layout = pipelineLayout; // 管线布局，所使用的描述符集布局
        rayTracingPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
        rayTracingPipelineCreateInfo.basePipelineIndex = 0;

        if (vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1,
                                           &rayTracingPipelineCreateInfo, NULL, &rayTracingPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create ray tracing pipeline!");
        }

        vkDestroyShaderModule(device, rayMissShadowShaderModule, NULL);
        vkDestroyShaderModule(device, rayMissShaderModule, NULL);
        vkDestroyShaderModule(device, rayGenerateShaderModule, NULL);
        vkDestroyShaderModule(device, rayClosestHitShaderModule, NULL);
    }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        throw std::runtime_error("failed to find suitable memory type!");
    }

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer, VkDeviceMemory &bufferMemory, bool useDeviceAddress = false)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create buffer!");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
        VkMemoryAllocateFlagsInfo allocFlagsInfo{};
        if (useDeviceAddress)
        {
            allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
            // GPU 代码（Shader）能够像 CPU 使用指针一样，直接通过“地址”去访问显存中的数据
            allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT; // 允许 GPU 获取这块内存的Device Address
            // 将 flagsInfo 链接到 allocInfo
            allocInfo.pNext = &allocFlagsInfo;
        }
        if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate buffer memory!");
        }

        vkBindBufferMemory(device, buffer, bufferMemory, 0);
    }

    void loadModel()
    {
        // OBJ Model
        tinyobj::ObjReaderConfig reader_config;
        tinyobj::ObjReader reader;

        if (!reader.ParseFromFile(MODEL_PATH.c_str(), reader_config))
        {
            if (!reader.Error().empty())
            {
                std::cerr << "TinyObjReader: " << reader.Error();
            }
            exit(1);
        }

        if (!reader.Warning().empty())
        {
            std::cout << "TinyObjReader: " << reader.Warning();
        }

        // 一个 OBJ 文件包含多个形状（Shapes/Objects）；每一个形状代表一个独立的网格（Mesh）
        // 每一个网格由许多面（Faces）组成；每一个面由 3 个或更多的顶点索引构成
        const tinyobj::attrib_t &attrib = reader.GetAttrib();                      // 顶点属性数据，包括顶点位置、法线、纹理坐标等
        const std::vector<tinyobj::shape_t> &shapes = reader.GetShapes();          // 形状数据
        const std::vector<tinyobj::material_t> &materials = reader.GetMaterials(); // 材质数据，包含模型的材质定义（如漫反射颜色、高光颜色等，来自配套的 .mtl 文件）

        vertices = attrib.vertices;

        // 遍历每个形状，统计primitiveCount总面数，并将每个面的顶点索引和材质索引分别存储在indices和materialIndex中
        for (tinyobj::shape_t shape : shapes)
        {
            // 统计总面数，shape.mesh.num_face_vertices是一个数组，记录了每个面的顶点数量，将这些数量累加得到总面数
            primitiveCount += shape.mesh.num_face_vertices.size();
            // 每个面的顶点索引，shape.mesh.indices是一个数组，记录了每个面的顶点索引
            for (tinyobj::index_t index : shape.mesh.indices)
            {
                indices.push_back(index.vertex_index);
            }
            // 每个面的材质索引，shape.mesh.material_ids是一个数组，记录了每个面的材质索引
            for (int index : shape.mesh.material_ids)
            {
                materialIndex.push_back(index);
            }
        }

        // 将材质数据存储在自定义Material结构体的materialList中
        materialList.resize(materials.size());
        for (uint32_t x = 0; x < materials.size(); x++)
        {
            memcpy(materialList[x].ambient, materials[x].ambient, sizeof(float) * 3);
            memcpy(materialList[x].diffuse, materials[x].diffuse, sizeof(float) * 3);
            memcpy(materialList[x].specular, materials[x].specular, sizeof(float) * 3);
            memcpy(materialList[x].emission, materials[x].emission, sizeof(float) * 3);
        }
    }

    void createVertexBuffer()
    {
        VkDeviceSize vertexBufferSize = sizeof(float) * vertices.size();
        createBuffer(vertexBufferSize,
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | // 允许 GPU 获取这块内存的Device Address
                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     vertexBuffer, vertexBufferMemory, true);

        // 将顶点数据复制到顶点缓冲区
        void *data;
        vkMapMemory(device, vertexBufferMemory, 0, vertexBufferSize, 0, &data);
        memcpy(data, vertices.data(), vertexBufferSize);
        vkUnmapMemory(device, vertexBufferMemory);

        // 获取顶点缓冲区的设备地址
        VkBufferDeviceAddressInfo vertexBufferDeviceAddressInfo{};
        vertexBufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        vertexBufferDeviceAddressInfo.buffer = vertexBuffer;
        vertexBufferDeviceAddressInfo.pNext = nullptr;
        vertexBufferDeviceAddress = vkGetBufferDeviceAddressKHR(device, &vertexBufferDeviceAddressInfo);
    }

    void createIndexBuffer()
    {
        VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
        createBuffer(indexBufferSize,
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     indexBuffer, indexBufferMemory, true);

        // 将索引数据复制到索引缓冲区
        void *data;
        vkMapMemory(device, indexBufferMemory, 0, indexBufferSize, 0, &data);
        memcpy(data, indices.data(), indexBufferSize);
        vkUnmapMemory(device, indexBufferMemory);

        // 获取索引缓冲区的设备地址
        VkBufferDeviceAddressInfo indexBufferDeviceAddressInfo{};
        indexBufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        indexBufferDeviceAddressInfo.buffer = indexBuffer;
        indexBufferDeviceAddressInfo.pNext = nullptr;
        indexBufferDeviceAddress = vkGetBufferDeviceAddressKHR(device, &indexBufferDeviceAddressInfo);
    }

    void createMaterialIndexBuffer()
    {
        VkDeviceSize materialIndexBufferSize = sizeof(uint32_t) * materialIndex.size();
        // Material Index Buffer
        createBuffer(materialIndexBufferSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     materialIndexBuffer, materialIndexBufferMemory, true);
        // 将材质索引数据复制到材质索引缓冲区
        void *data;
        vkMapMemory(device, materialIndexBufferMemory, 0, materialIndexBufferSize, 0, &data);
        memcpy(data, materialIndex.data(), materialIndexBufferSize);
        vkUnmapMemory(device, materialIndexBufferMemory);
    }

    void createMaterialBuffer()
    {
        VkDeviceSize materialBufferSize = sizeof(Material) * materialList.size();
        // Material Buffer
        createBuffer(materialBufferSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     materialBuffer, materialBufferMemory, true);
        // 将材质数据复制到Material Buffer
        void *data;
        vkMapMemory(device, materialBufferMemory, 0, materialBufferSize, 0, &data);
        memcpy(data, materialList.data(), materialBufferSize);
        vkUnmapMemory(device, materialBufferMemory);
    }

    void createBottomLevelAccelerationStructure()
    {
        // 底层加速结构几何数据，指定几何类型（如三角形）、顶点格式、顶点数据地址、索引格式和索引数据地址等信息
        VkAccelerationStructureGeometryDataKHR bottomLevelAccelerationStructureGeometryData = {};
        bottomLevelAccelerationStructureGeometryData.triangles.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        bottomLevelAccelerationStructureGeometryData.triangles.pNext = NULL;
        bottomLevelAccelerationStructureGeometryData.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        bottomLevelAccelerationStructureGeometryData.triangles.vertexData.deviceAddress = vertexBufferDeviceAddress;
        bottomLevelAccelerationStructureGeometryData.triangles.vertexStride = sizeof(float) * 3;
        bottomLevelAccelerationStructureGeometryData.triangles.maxVertex = (uint32_t)vertices.size() / 3;
        bottomLevelAccelerationStructureGeometryData.triangles.indexType = VK_INDEX_TYPE_UINT32;
        bottomLevelAccelerationStructureGeometryData.triangles.indexData.deviceAddress = indexBufferDeviceAddress;
        bottomLevelAccelerationStructureGeometryData.triangles.transformData.deviceAddress = 0;
        // 如果有多个几何体，可以创建多个VkAccelerationStructureGeometryDataKHR结构体，并在构建信息中指定它们的数量和指针
        VkAccelerationStructureGeometryKHR bottomLevelAccelerationStructureGeometry = {};
        bottomLevelAccelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        bottomLevelAccelerationStructureGeometry.pNext = NULL;
        bottomLevelAccelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        bottomLevelAccelerationStructureGeometry.geometry = bottomLevelAccelerationStructureGeometryData;
        bottomLevelAccelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR; // 不透明物体，不会执行any hit着色器

        // 底层加速结构构建信息，指定构建模式（构建/更新）、源加速结构和目标加速结构、几何数据等信息
        VkAccelerationStructureBuildGeometryInfoKHR bottomLevelAccelerationStructureBuildGeometryInfo = {};
        bottomLevelAccelerationStructureBuildGeometryInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        bottomLevelAccelerationStructureBuildGeometryInfo.pNext = NULL;
        bottomLevelAccelerationStructureBuildGeometryInfo.type =
            VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR; // 加速结构类型，表示这是一个底层加速结构
        bottomLevelAccelerationStructureBuildGeometryInfo.flags = 0;
        bottomLevelAccelerationStructureBuildGeometryInfo.mode =
            VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR; // 构建模式，表示这是一个新的加速结构构建，而不是更新现有的加速结构
        bottomLevelAccelerationStructureBuildGeometryInfo.srcAccelerationStructure =
            VK_NULL_HANDLE;
        bottomLevelAccelerationStructureBuildGeometryInfo.dstAccelerationStructure =
            VK_NULL_HANDLE;
        bottomLevelAccelerationStructureBuildGeometryInfo.geometryCount = 1; // 几何体数量，这里只有一个几何体，即一个三角形网格
        bottomLevelAccelerationStructureBuildGeometryInfo.pGeometries =
            &bottomLevelAccelerationStructureGeometry;
        bottomLevelAccelerationStructureBuildGeometryInfo.ppGeometries = NULL;
        bottomLevelAccelerationStructureBuildGeometryInfo.scratchData.deviceAddress = 0;

        // 查询构建底层加速结构所需的内存大小
        VkAccelerationStructureBuildSizesInfoKHR bottomLevelAccelerationStructureBuildSizesInfo = {};
        bottomLevelAccelerationStructureBuildSizesInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        bottomLevelAccelerationStructureBuildSizesInfo.pNext = NULL;
        // 构建完成后的加速结构所需的内存大小
        bottomLevelAccelerationStructureBuildSizesInfo.accelerationStructureSize = 0;
        // 更新加速结构所需的scratch buffer大小，构建模式为BUILD时不需要更新，所以设置为0
        bottomLevelAccelerationStructureBuildSizesInfo.updateScratchSize = 0;
        // 构建加速结构所需的scratch buffer大小
        bottomLevelAccelerationStructureBuildSizesInfo.buildScratchSize = 0;
        std::vector<uint32_t>
            bottomLevelMaxPrimitiveCountList = {primitiveCount};
        vkGetAccelerationStructureBuildSizesKHR(
            device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &bottomLevelAccelerationStructureBuildGeometryInfo, // 底层加速结构构建信息
            bottomLevelMaxPrimitiveCountList.data(),            // 每个几何体的最大原始图元数量
            &bottomLevelAccelerationStructureBuildSizesInfo);

        // 创建BLAS底层加速结构缓冲区，用于存储底层加速结构数据
        createBuffer(bottomLevelAccelerationStructureBuildSizesInfo.accelerationStructureSize,
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     bottomLevelAccelerationStructureBuffer,
                     bottomLevelAccelerationStructureBufferMemory, true);

        // 创建构建底层加速结构的临时缓冲区，作为构建过程中使用的scratch buffer
        VkBuffer bottomLevelAccelerationStructureScratchBuffer;
        VkDeviceMemory bottomLevelAccelerationStructureScratchBufferMemory;
        createBuffer(bottomLevelAccelerationStructureBuildSizesInfo.buildScratchSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     bottomLevelAccelerationStructureScratchBuffer,
                     bottomLevelAccelerationStructureScratchBufferMemory, true);
        // 获取临时缓冲区的设备地址
        VkBufferDeviceAddressInfo bottomLevelAccelerationStructureScratchBufferDeviceAddressInfo{};
        bottomLevelAccelerationStructureScratchBufferDeviceAddressInfo.sType =
            VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        bottomLevelAccelerationStructureScratchBufferDeviceAddressInfo.pNext = NULL;
        bottomLevelAccelerationStructureScratchBufferDeviceAddressInfo.buffer =
            bottomLevelAccelerationStructureScratchBuffer;
        VkDeviceAddress bottomLevelAccelerationStructureScratchBufferDeviceAddress =
            vkGetBufferDeviceAddressKHR(device, &bottomLevelAccelerationStructureScratchBufferDeviceAddressInfo);

        // 创建底层加速结构
        VkAccelerationStructureCreateInfoKHR bottomLevelAccelerationStructureCreateInfo = {};
        bottomLevelAccelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        bottomLevelAccelerationStructureCreateInfo.pNext = NULL;
        bottomLevelAccelerationStructureCreateInfo.createFlags = 0;
        bottomLevelAccelerationStructureCreateInfo.buffer = bottomLevelAccelerationStructureBuffer;
        bottomLevelAccelerationStructureCreateInfo.offset = 0;
        bottomLevelAccelerationStructureCreateInfo.size = bottomLevelAccelerationStructureBuildSizesInfo.accelerationStructureSize;
        bottomLevelAccelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        bottomLevelAccelerationStructureCreateInfo.deviceAddress = 0;
        if (vkCreateAccelerationStructureKHR(device, &bottomLevelAccelerationStructureCreateInfo, NULL,
                                             &bottomLevelAccelerationStructure) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create bottom level acceleration structure!");
        }

        // 指定构建完成后要存储结果的目标加速结构
        bottomLevelAccelerationStructureBuildGeometryInfo.dstAccelerationStructure =
            bottomLevelAccelerationStructure;
        // 指定构建过程中使用的scratch buffer的设备地址
        bottomLevelAccelerationStructureBuildGeometryInfo.scratchData.deviceAddress =
            bottomLevelAccelerationStructureScratchBufferDeviceAddress;

        // 构建底层加速结构范围信息，指定构建过程中每个几何体的原始图元数量、顶点偏移量、索引偏移量等信息
        VkAccelerationStructureBuildRangeInfoKHR bottomLevelAccelerationStructureBuildRangeInfo{};
        bottomLevelAccelerationStructureBuildRangeInfo.primitiveCount = primitiveCount;
        bottomLevelAccelerationStructureBuildRangeInfo.primitiveOffset = 0;
        bottomLevelAccelerationStructureBuildRangeInfo.firstVertex = 0;
        bottomLevelAccelerationStructureBuildRangeInfo.transformOffset = 0;

        const VkAccelerationStructureBuildRangeInfoKHR
            *bottomLevelAccelerationStructureBuildRangeInfos =
                &bottomLevelAccelerationStructureBuildRangeInfo;

        // 在命令缓冲区中记录命令，执行底层加速结构的构建操作
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        vkCmdBuildAccelerationStructuresKHR(
            commandBuffer, 1,
            &bottomLevelAccelerationStructureBuildGeometryInfo, // 底层加速结构构建信息
            &bottomLevelAccelerationStructureBuildRangeInfos    // 底层加速结构构建范围信息
        );
        endSingleTimeCommands(commandBuffer);

        vkFreeMemory(device, bottomLevelAccelerationStructureScratchBufferMemory, NULL);
        vkDestroyBuffer(device, bottomLevelAccelerationStructureScratchBuffer, NULL);
    }

    void createTopLevelAccelerationStructure()
    {
        // 获取底层加速结构的设备地址
        VkAccelerationStructureDeviceAddressInfoKHR bottomLevelAccelerationStructureDeviceAddressInfo{};
        bottomLevelAccelerationStructureDeviceAddressInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        bottomLevelAccelerationStructureDeviceAddressInfo.pNext = NULL;
        bottomLevelAccelerationStructureDeviceAddressInfo.accelerationStructure = bottomLevelAccelerationStructure; // 指定要查询设备地址的加速结构
        VkDeviceAddress bottomLevelAccelerationStructureDeviceAddress =
            vkGetAccelerationStructureDeviceAddressKHR(device, &bottomLevelAccelerationStructureDeviceAddressInfo);

        // 底层加速结构实例
        VkAccelerationStructureInstanceKHR bottomLevelAccelerationStructureInstance = {};
        // 一个3x4的矩阵，用于将底层加速结构中的几何体变换到世界空间中
        bottomLevelAccelerationStructureInstance.transform.matrix[0][0] = 1.0f; // 设置为单位矩阵，表示没有变换
        bottomLevelAccelerationStructureInstance.transform.matrix[1][1] = 1.0f;
        bottomLevelAccelerationStructureInstance.transform.matrix[2][2] = 1.0f;
        // 一个24位的用户自定义索引，可以在着色器中通过InstanceID访问，设置为0表示这是第一个实例
        bottomLevelAccelerationStructureInstance.instanceCustomIndex = 0;
        // mask用于在特定的光线追踪通道中包含或排除该实例，0xFF表示在所有通道中都包含该实例
        bottomLevelAccelerationStructureInstance.mask = 0xFF;
        // 用于指定在着色器绑定表中与该实例关联的着色器记录的偏移量，0表示使用着色器绑定表中的第一个记录
        bottomLevelAccelerationStructureInstance.instanceShaderBindingTableRecordOffset = 0;
        // flags用于控制实例的行为，如是否启用面剔除等
        bottomLevelAccelerationStructureInstance.flags =
            VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR; // 禁用面剔除，确保无论三角形的朝向如何，都能被检测到
        // 底层加速结构的设备地址
        bottomLevelAccelerationStructureInstance.accelerationStructureReference =
            bottomLevelAccelerationStructureDeviceAddress;

        // 创建底层加速结构实例缓冲区，用于存储底层加速结构实例数据
        VkBuffer bottomLevelGeometryInstanceBuffer;
        VkDeviceMemory bottomLevelGeometryInstanceBufferMemory;
        createBuffer(sizeof(VkAccelerationStructureInstanceKHR),
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     bottomLevelGeometryInstanceBuffer,
                     bottomLevelGeometryInstanceBufferMemory, true);

        // 将底层加速结构实例数据复制到实例缓冲区
        void *data;
        vkMapMemory(device, bottomLevelGeometryInstanceBufferMemory, 0,
                    sizeof(VkAccelerationStructureInstanceKHR), 0, &data);
        memcpy(data, &bottomLevelAccelerationStructureInstance,
               sizeof(VkAccelerationStructureInstanceKHR));
        vkUnmapMemory(device, bottomLevelGeometryInstanceBufferMemory);

        // 获取实例缓冲区的设备地址，以便在构建顶层加速结构时使用
        VkBufferDeviceAddressInfo bottomLevelGeometryInstanceDeviceAddressInfo{};
        bottomLevelGeometryInstanceDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        bottomLevelGeometryInstanceDeviceAddressInfo.pNext = NULL;
        bottomLevelGeometryInstanceDeviceAddressInfo.buffer = bottomLevelGeometryInstanceBuffer;
        VkDeviceAddress bottomLevelGeometryInstanceDeviceAddress =
            vkGetBufferDeviceAddressKHR(device, &bottomLevelGeometryInstanceDeviceAddressInfo);

        // 顶层加速结构几何数据
        VkAccelerationStructureGeometryDataKHR topLevelAccelerationStructureGeometryData = {};
        topLevelAccelerationStructureGeometryData.instances.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        topLevelAccelerationStructureGeometryData.instances.pNext = NULL;
        topLevelAccelerationStructureGeometryData.instances.arrayOfPointers = VK_FALSE;
        topLevelAccelerationStructureGeometryData.instances.data.deviceAddress =
            bottomLevelGeometryInstanceDeviceAddress; // 指定底层加速结构实例数据的设备地址
        // 顶层加速结构几何信息，指定几何类型（如实例）、几何数据等信息
        VkAccelerationStructureGeometryKHR topLevelAccelerationStructureGeometry = {};
        topLevelAccelerationStructureGeometry.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        topLevelAccelerationStructureGeometry.pNext = NULL;
        topLevelAccelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        topLevelAccelerationStructureGeometry.geometry = topLevelAccelerationStructureGeometryData;
        topLevelAccelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

        // 顶层加速结构构建信息，指定构建模式（构建/更新）、源加速结构和目标加速结构、几何数据等信息
        VkAccelerationStructureBuildGeometryInfoKHR topLevelAccelerationStructureBuildGeometryInfo = {};
        topLevelAccelerationStructureBuildGeometryInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        topLevelAccelerationStructureBuildGeometryInfo.pNext = NULL;
        topLevelAccelerationStructureBuildGeometryInfo.type =
            VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        topLevelAccelerationStructureBuildGeometryInfo.flags = 0;
        topLevelAccelerationStructureBuildGeometryInfo.mode =
            VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        topLevelAccelerationStructureBuildGeometryInfo.srcAccelerationStructure =
            VK_NULL_HANDLE;
        topLevelAccelerationStructureBuildGeometryInfo.dstAccelerationStructure =
            VK_NULL_HANDLE;
        topLevelAccelerationStructureBuildGeometryInfo.geometryCount = 1;
        topLevelAccelerationStructureBuildGeometryInfo.pGeometries =
            &topLevelAccelerationStructureGeometry;
        topLevelAccelerationStructureBuildGeometryInfo.ppGeometries = NULL;
        topLevelAccelerationStructureBuildGeometryInfo.scratchData.deviceAddress = 0;

        // 查询构建顶层加速结构所需的内存大小，指定构建模式、几何数据和每个几何体的最大原始图元数量等信息
        VkAccelerationStructureBuildSizesInfoKHR
            topLevelAccelerationStructureBuildSizesInfo = {};
        topLevelAccelerationStructureBuildSizesInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        topLevelAccelerationStructureBuildSizesInfo.pNext = NULL;
        // 构建完成后的加速结构所需的内存大小
        topLevelAccelerationStructureBuildSizesInfo.accelerationStructureSize = 0;
        // 更新加速结构所需的scratch buffer大小，构建模式为BUILD时不需要更新
        topLevelAccelerationStructureBuildSizesInfo.updateScratchSize = 0;
        // 构建加速结构所需的scratch buffer大小
        topLevelAccelerationStructureBuildSizesInfo.buildScratchSize = 0;

        // 每个几何体的最大原始图元数量列表，顶层加速结构只有一个实例，所以设置为1
        std::vector<uint32_t> topLevelMaxPrimitiveCountList = {1};
        vkGetAccelerationStructureBuildSizesKHR(
            device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &topLevelAccelerationStructureBuildGeometryInfo, // 顶层加速结构构建信息
            topLevelMaxPrimitiveCountList.data(),            // 每个几何体的最大原始图元数量列表
            &topLevelAccelerationStructureBuildSizesInfo);

        // 创建顶层加速结构缓冲区，用于存储顶层加速结构数据
        createBuffer(topLevelAccelerationStructureBuildSizesInfo.accelerationStructureSize,
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     topLevelAccelerationStructureBuffer,
                     topLevelAccelerationStructureBufferMemory, true);

        // 创建构建顶层加速结构的临时缓冲区，作为构建过程中使用的scratch buffer
        VkBuffer topLevelAccelerationStructureScratchBuffer;
        VkDeviceMemory topLevelAccelerationStructureScratchBufferMemory;
        createBuffer(topLevelAccelerationStructureBuildSizesInfo.buildScratchSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     topLevelAccelerationStructureScratchBuffer,
                     topLevelAccelerationStructureScratchBufferMemory, true);

        // 获取临时缓冲区的设备地址，以便在构建顶层加速结构时使用
        VkBufferDeviceAddressInfo topLevelAccelerationStructureScratchBufferDeviceAddressInfo{};
        topLevelAccelerationStructureScratchBufferDeviceAddressInfo.sType =
            VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        topLevelAccelerationStructureScratchBufferDeviceAddressInfo.pNext = NULL;
        topLevelAccelerationStructureScratchBufferDeviceAddressInfo.buffer =
            topLevelAccelerationStructureScratchBuffer;
        VkDeviceAddress topLevelAccelerationStructureScratchBufferDeviceAddress =
            vkGetBufferDeviceAddressKHR(device, &topLevelAccelerationStructureScratchBufferDeviceAddressInfo);

        // 创建顶层加速结构
        VkAccelerationStructureCreateInfoKHR topLevelAccelerationStructureCreateInfo{};
        topLevelAccelerationStructureCreateInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        topLevelAccelerationStructureCreateInfo.pNext = NULL;
        topLevelAccelerationStructureCreateInfo.createFlags = 0;
        topLevelAccelerationStructureCreateInfo.buffer = topLevelAccelerationStructureBuffer;
        topLevelAccelerationStructureCreateInfo.offset = 0;
        topLevelAccelerationStructureCreateInfo.size =
            topLevelAccelerationStructureBuildSizesInfo.accelerationStructureSize;
        topLevelAccelerationStructureCreateInfo.type =
            VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        topLevelAccelerationStructureCreateInfo.deviceAddress = 0;
        if (vkCreateAccelerationStructureKHR(device, &topLevelAccelerationStructureCreateInfo,
                                             NULL, &topLevelAccelerationStructure) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create top level acceleration structure!");
        }

        // 设置顶层加速结构构建信息，指出所使用的scratch buffer和目标加速结构
        topLevelAccelerationStructureBuildGeometryInfo.dstAccelerationStructure =
            topLevelAccelerationStructure;
        topLevelAccelerationStructureBuildGeometryInfo.scratchData.deviceAddress =
            topLevelAccelerationStructureScratchBufferDeviceAddress;

        // 构建顶层加速结构范围信息，指定构建过程中每个几何体的原始图元数量、顶点偏移量、索引偏移量等信息
        VkAccelerationStructureBuildRangeInfoKHR topLevelAccelerationStructureBuildRangeInfo{};
        topLevelAccelerationStructureBuildRangeInfo.primitiveCount = 1;
        topLevelAccelerationStructureBuildRangeInfo.primitiveOffset = 0;
        topLevelAccelerationStructureBuildRangeInfo.firstVertex = 0;
        topLevelAccelerationStructureBuildRangeInfo.transformOffset = 0;

        const VkAccelerationStructureBuildRangeInfoKHR
            *topLevelAccelerationStructureBuildRangeInfos =
                &topLevelAccelerationStructureBuildRangeInfo;

        // 在命令缓冲区中记录命令，执行顶层加速结构的构建操作
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        vkCmdBuildAccelerationStructuresKHR(
            commandBuffer, 1,
            &topLevelAccelerationStructureBuildGeometryInfo,
            &topLevelAccelerationStructureBuildRangeInfos);

        endSingleTimeCommands(commandBuffer);

        vkFreeMemory(device, bottomLevelGeometryInstanceBufferMemory, NULL);
        vkDestroyBuffer(device, bottomLevelGeometryInstanceBuffer, NULL);
        vkFreeMemory(device, topLevelAccelerationStructureScratchBufferMemory, NULL);
        vkDestroyBuffer(device, topLevelAccelerationStructureScratchBuffer, NULL);
    }

    VkCommandBuffer beginSingleTimeCommands()
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate one-time command buffer!");
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to begin one-time command buffer!");
        }

        return commandBuffer;
    }

    void endSingleTimeCommands(VkCommandBuffer commandBuffer)
    {
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to end one-time command buffer!");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        VkFenceCreateInfo fenceCreateInfo{};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(device, &fenceCreateInfo, nullptr, &fence) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create one-time command fence!");
        }

        vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence);
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    void createUniformBuffer()
    {
        // 初始化统一缓冲区
        uniformBufferObject.cameraPosition = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        uniformBufferObject.cameraRight = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        uniformBufferObject.cameraUp = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
        uniformBufferObject.cameraForward = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        uniformBufferObject.frameCount = 0;

        createBuffer(sizeof(UniformBufferObject),
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     uniformBuffer, uniformBufferMemory, true);

        // 将统一缓冲区对象数据复制到统一缓冲区
        void *data;
        vkMapMemory(device, uniformBufferMemory, 0, sizeof(UniformBufferObject), 0, &data);
        memcpy(data, &uniformBufferObject, sizeof(UniformBufferObject));
        vkUnmapMemory(device, uniformBufferMemory);
    }

    void createRayTracingImage()
    {
        // 创建一个Ray Tracing Image来存储光线追踪的结果
        VkFormat surfaceFormat = swapChainImageFormat;
        createImage(swapChainExtent.width, swapChainExtent.height,
                    1, VK_SAMPLE_COUNT_1_BIT,
                    surfaceFormat,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    rayTracingImage, rayTracingImageMemory);
        rayTracingImageView = createImageView(rayTracingImage, surfaceFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

        // 图像布局转换，将图像从UNDEFINED未定义布局转换为GENERAL通用布局，以便在光线追踪着色器中写入数据
        transitionImageLayout(rayTracingImage, surfaceFormat,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_GENERAL,
                              1);
    }

    void transitionImageLayout(VkImage image, VkFormat format,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               uint32_t mipLevels)
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                 newLayout == VK_IMAGE_LAYOUT_GENERAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                 newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else
        {
            throw std::invalid_argument("unsupported layout transition!");
        }

        vkCmdPipelineBarrier(
            commandBuffer,
            sourceStage,
            destinationStage,
            0,
            0, NULL,
            0, NULL,
            1, &barrier);

        endSingleTimeCommands(commandBuffer);
    }

    void createShaderBindingTable()
    {
        VkPhysicalDeviceProperties physicalDeviceProperties;
        vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
        // 获取物理设备的光线追踪管线属性
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR physicalDeviceRayTracingPipelineProperties{};
        physicalDeviceRayTracingPipelineProperties.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        physicalDeviceRayTracingPipelineProperties.pNext = NULL;
        VkPhysicalDeviceProperties2 physicalDeviceProperties2{};
        physicalDeviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        physicalDeviceProperties2.pNext = &physicalDeviceRayTracingPipelineProperties;
        physicalDeviceProperties2.properties = physicalDeviceProperties;

        vkGetPhysicalDeviceProperties2(physicalDevice, &physicalDeviceProperties2);

        // 对齐步长。SBT 缓冲区中，每个条目（Entry）必须占据的最小内存空间，以满足 GPU 的硬件对齐要求
        VkDeviceSize progSize = physicalDeviceRayTracingPipelineProperties.shaderGroupBaseAlignment;
        // 每个着色器组的实际句柄大小，通常小于或等于progSize
        VkDeviceSize shaderGroupHandleSize = physicalDeviceRayTracingPipelineProperties.shaderGroupHandleSize;

        // Shader Binding Table
        VkDeviceSize shaderBindingTableSize = progSize * 4; // SBT大小 = Group总数 × progSize
        createBuffer(shaderBindingTableSize,
                     VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     shaderBindingTableBuffer, shaderBindingTableBufferMemory, true);

        // 获取光线追踪管线中每个着色器组的句柄
        char *shaderHandleBuffer = new char[shaderBindingTableSize];
        if (vkGetRayTracingShaderGroupHandlesKHR(device, rayTracingPipeline, 0, 4,
                                                 shaderBindingTableSize, shaderHandleBuffer) !=
            VK_SUCCESS)
        {
            throw std::runtime_error("failed to get ray tracing shader group handles!");
        }

        // 将获取到的着色器组句柄复制到着色器绑定表缓冲区中
        void *data;
        vkMapMemory(device, shaderBindingTableBufferMemory, 0, shaderBindingTableSize, 0, &data);

        for (uint32_t x = 0; x < 4; x++)
        {
            // shaderGroupHandleSize表示每个着色器组句柄的大小，progSize表示每个着色器组的句柄大小
            memcpy(data, shaderHandleBuffer + x * shaderGroupHandleSize, shaderGroupHandleSize);
            data = reinterpret_cast<char *>(data) + progSize;
        }
        vkUnmapMemory(device, shaderBindingTableBufferMemory);

        // 获取着色器绑定表缓冲区的设备地址
        VkBufferDeviceAddressInfo shaderBindingTableBufferDeviceAddressInfo{};
        shaderBindingTableBufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        shaderBindingTableBufferDeviceAddressInfo.pNext = NULL;
        shaderBindingTableBufferDeviceAddressInfo.buffer = shaderBindingTableBuffer;

        VkDeviceAddress shaderBindingTableBufferDeviceAddress =
            vkGetBufferDeviceAddressKHR(device, &shaderBindingTableBufferDeviceAddressInfo);

        // 定义着色器绑定表的设备地址区域，指定每个着色器组在着色器绑定表缓冲区中的偏移量、跨度和大小等信息
        VkDeviceSize rayGenOffset = 0u * progSize;
        VkDeviceSize hitGroupOffset = 1u * progSize;
        VkDeviceSize missOffset = 2u * progSize;

        // 获取不同类型的shader对应的着色器绑定表信息
        // ray gen shader group
        rgenShaderBindingTable.deviceAddress = shaderBindingTableBufferDeviceAddress + rayGenOffset;
        rgenShaderBindingTable.stride = progSize;
        rgenShaderBindingTable.size = progSize;
        // closest hit shader group
        rchitShaderBindingTable.deviceAddress = shaderBindingTableBufferDeviceAddress + hitGroupOffset;
        rchitShaderBindingTable.stride = progSize;
        rchitShaderBindingTable.size = progSize;
        // miss shader group
        rmissShaderBindingTable.deviceAddress = shaderBindingTableBufferDeviceAddress + missOffset;
        rmissShaderBindingTable.stride = progSize;
        rmissShaderBindingTable.size = progSize * 2;
        // callable shader group
        callableShaderBindingTable.deviceAddress = 0;
        callableShaderBindingTable.stride = 0;
        callableShaderBindingTable.size = 0;

        delete[] shaderHandleBuffer;
    }

    void mainLoop()
    {
        createSyncObjects();
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
            drawFrame();
        }

        vkDeviceWaitIdle(device);
    }

    void createSyncObjects()
    {
        imageAvailableFence.resize(swapChainImages.size());
        acquireImageSemaphore.resize(swapChainImages.size());
        writeImageSemaphore.resize(swapChainImages.size());

        VkFenceCreateInfo fenceCreateInfo{};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        for (uint32_t x = 0; x < swapChainImages.size(); x++)
        {
            if (vkCreateFence(device, &fenceCreateInfo, nullptr, &imageAvailableFence[x]) != VK_SUCCESS ||
                vkCreateSemaphore(device, &semaphoreInfo, nullptr, &acquireImageSemaphore[x]) != VK_SUCCESS ||
                vkCreateSemaphore(device, &semaphoreInfo, nullptr, &writeImageSemaphore[x]) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create synchronization objects for a frame!");
            }
        }
    }

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        beginInfo.pInheritanceInfo = NULL;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to begin recording command buffer!");
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracingPipeline);
        vkCmdBindDescriptorSets(commandBuffer,
                                VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                pipelineLayout,
                                0,
                                (uint32_t)descriptorSets.size(),
                                descriptorSets.data(),
                                0, NULL);

        vkCmdTraceRaysKHR(commandBuffer,
                          &rgenShaderBindingTable,
                          &rmissShaderBindingTable,
                          &rchitShaderBindingTable,
                          &callableShaderBindingTable,
                          swapChainExtent.width,
                          swapChainExtent.height, 1);

        // 将swapchainImage的布局从未定义布局转换为传输目标布局，以便将光线追踪的结果复制到交换链图像中
        VkImageMemoryBarrier swapchainCopyMemoryBarrier{};
        swapchainCopyMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        swapchainCopyMemoryBarrier.pNext = NULL;
        swapchainCopyMemoryBarrier.srcAccessMask = 0;
        swapchainCopyMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        swapchainCopyMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        swapchainCopyMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        swapchainCopyMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapchainCopyMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapchainCopyMemoryBarrier.image = swapChainImages[imageIndex];
        swapchainCopyMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        swapchainCopyMemoryBarrier.subresourceRange.baseMipLevel = 0;
        swapchainCopyMemoryBarrier.subresourceRange.levelCount = 1;
        swapchainCopyMemoryBarrier.subresourceRange.baseArrayLayer = 0;
        swapchainCopyMemoryBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                             NULL, 1, &swapchainCopyMemoryBarrier);

        // 将rayTracingImage的布局从通用布局转换为传输源布局，以便将光线追踪的结果复制到交换链图像中
        VkImageMemoryBarrier rayTraceCopyMemoryBarrier{};
        rayTraceCopyMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        rayTraceCopyMemoryBarrier.pNext = NULL;
        rayTraceCopyMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        rayTraceCopyMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        rayTraceCopyMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        rayTraceCopyMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        rayTraceCopyMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        rayTraceCopyMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        rayTraceCopyMemoryBarrier.image = rayTracingImage;
        rayTraceCopyMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        rayTraceCopyMemoryBarrier.subresourceRange.baseMipLevel = 0;
        rayTraceCopyMemoryBarrier.subresourceRange.levelCount = 1;
        rayTraceCopyMemoryBarrier.subresourceRange.baseArrayLayer = 0;
        rayTraceCopyMemoryBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                             NULL, 1, &rayTraceCopyMemoryBarrier);

        // 将光线追踪的结果从rayTracingImage复制到swapchainImage
        VkImageCopy imageCopy{};
        imageCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopy.srcSubresource.mipLevel = 0;
        imageCopy.srcSubresource.baseArrayLayer = 0;
        imageCopy.srcSubresource.layerCount = 1;
        imageCopy.srcOffset = {0, 0, 0};
        imageCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopy.dstSubresource.mipLevel = 0;
        imageCopy.dstSubresource.baseArrayLayer = 0;
        imageCopy.dstSubresource.layerCount = 1;
        imageCopy.dstOffset = {0, 0, 0};
        imageCopy.extent.width = swapChainExtent.width;
        imageCopy.extent.height = swapChainExtent.height;
        imageCopy.extent.depth = 1;
        vkCmdCopyImage(commandBuffer, rayTracingImage,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swapChainImages[imageIndex],
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopy);

        // 将swapchainImage的布局从传输目标布局转换为呈现源布局，以便将图像呈现到屏幕上
        VkImageMemoryBarrier swapchainPresentMemoryBarrier{};
        swapchainPresentMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        swapchainPresentMemoryBarrier.pNext = NULL;
        swapchainPresentMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        swapchainPresentMemoryBarrier.dstAccessMask = 0;
        swapchainPresentMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        swapchainPresentMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        swapchainPresentMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapchainPresentMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapchainPresentMemoryBarrier.image = swapChainImages[imageIndex];
        swapchainPresentMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        swapchainPresentMemoryBarrier.subresourceRange.baseMipLevel = 0;
        swapchainPresentMemoryBarrier.subresourceRange.levelCount = 1;
        swapchainPresentMemoryBarrier.subresourceRange.baseArrayLayer = 0;
        swapchainPresentMemoryBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                             NULL, 1, &swapchainPresentMemoryBarrier);

        // 将rayTracingImage的布局从传输源布局转换回通用布局，以便在下一帧继续使用该图像进行光线追踪
        VkImageMemoryBarrier rayTraceWriteMemoryBarrier{};
        rayTraceWriteMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        rayTraceWriteMemoryBarrier.pNext = NULL;
        rayTraceWriteMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        rayTraceWriteMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        rayTraceWriteMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        rayTraceWriteMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        rayTraceWriteMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        rayTraceWriteMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        rayTraceWriteMemoryBarrier.image = rayTracingImage;
        rayTraceWriteMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        rayTraceWriteMemoryBarrier.subresourceRange.baseMipLevel = 0;
        rayTraceWriteMemoryBarrier.subresourceRange.levelCount = 1;
        rayTraceWriteMemoryBarrier.subresourceRange.baseArrayLayer = 0;
        rayTraceWriteMemoryBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                             NULL, 1, &rayTraceWriteMemoryBarrier);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to record command buffer!");
        }
    }

    void updateUniformBuffer(uint32_t imageIndex)
    {
        static bool isCameraMoved = true;
        static float cameraPosition[3] = {1.5, 4.0, 10.0};
        static float cameraYaw = 0.0;
        static float cameraPitch = 0.0;

        bool moveForward = isMoveForward || glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
        bool moveBack = isMoveBack || glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
        bool turnLeft = isTurnLeft || glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
        bool turnRight = isTurnRight || glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;

        if (moveForward)
        {
            cameraPosition[0] += cos(-cameraYaw - (3.141592 / 2)) * 0.005f;
            cameraPosition[2] += sin(-cameraYaw - (3.141592 / 2)) * 0.005f;
            isCameraMoved = true;
        }
        if (moveBack)
        {
            cameraPosition[0] -= cos(-cameraYaw - (3.141592 / 2)) * 0.005f;
            cameraPosition[2] -= sin(-cameraYaw - (3.141592 / 2)) * 0.005f;
            isCameraMoved = true;
        }
        if (turnLeft)
        {
            cameraYaw += 0.0025f;
            isCameraMoved = true;
        }
        if (turnRight)
        {
            cameraYaw -= 0.0025f;
            isCameraMoved = true;
        }

        if (isCameraMoved)
        {
            uniformBufferObject.cameraPosition[0] = cameraPosition[0];
            uniformBufferObject.cameraPosition[1] = cameraPosition[1];
            uniformBufferObject.cameraPosition[2] = cameraPosition[2];

            uniformBufferObject.cameraForward[0] =
                cosf(cameraPitch) * cosf(-cameraYaw - (3.141592 / 2.0));
            uniformBufferObject.cameraForward[1] = sinf(cameraPitch);
            uniformBufferObject.cameraForward[2] =
                cosf(cameraPitch) * sinf(-cameraYaw - (3.141592 / 2.0));

            uniformBufferObject.cameraRight[0] =
                uniformBufferObject.cameraForward[1] * uniformBufferObject.cameraUp[2] -
                uniformBufferObject.cameraForward[2] * uniformBufferObject.cameraUp[1];
            uniformBufferObject.cameraRight[1] =
                uniformBufferObject.cameraForward[2] * uniformBufferObject.cameraUp[0] -
                uniformBufferObject.cameraForward[0] * uniformBufferObject.cameraUp[2];
            uniformBufferObject.cameraRight[2] =
                uniformBufferObject.cameraForward[0] * uniformBufferObject.cameraUp[1] -
                uniformBufferObject.cameraForward[1] * uniformBufferObject.cameraUp[0];

            uniformBufferObject.frameCount = 0;

            isCameraMoved = false;
        }
        else
        {
            uniformBufferObject.frameCount += 1;
        }

        void *data;
        vkMapMemory(device, uniformBufferMemory, 0, sizeof(UniformBufferObject), 0, &data);
        memcpy(data, &uniformBufferObject, sizeof(UniformBufferObject));
        vkUnmapMemory(device, uniformBufferMemory);
    }

    void drawFrame()
    {
        vkWaitForFences(device, 1, &imageAvailableFence[currentFrame], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT32_MAX,
                                                acquireImageSemaphore[currentFrame],
                                                VK_NULL_HANDLE, &imageIndex);

        updateUniformBuffer(imageIndex);

        vkResetCommandBuffer(commandBuffers[currentFrame], 0);
        recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = NULL;
        submitInfo.waitSemaphoreCount = 1;
        VkSemaphore waitSemaphores[] = {acquireImageSemaphore[currentFrame]};
        submitInfo.pWaitSemaphores = waitSemaphores;
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
        submitInfo.signalSemaphoreCount = 1;
        VkSemaphore signalSemaphores[] = {writeImageSemaphore[currentFrame]};
        submitInfo.pSignalSemaphores = signalSemaphores;
        vkResetFences(device, 1, &imageAvailableFence[currentFrame]);
        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, imageAvailableFence[currentFrame]) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to submit draw command buffer!");
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pNext = NULL;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        VkSwapchainKHR swapChains[] = {swapChain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;
        presentInfo.pResults = NULL;
        if (vkQueuePresentKHR(presentQueue, &presentInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to present swap chain image!");
        }

        currentFrame = (currentFrame + 1) % swapChainImages.size();
    }

    void cleanup()
    {
        for (uint32_t x = 0; x < swapChainImages.size(); x++)
        {
            vkDestroySemaphore(device, writeImageSemaphore[x], NULL);
            vkDestroySemaphore(device, acquireImageSemaphore[x], NULL);
            vkDestroyFence(device, imageAvailableFence[x], NULL);
        }

        vkFreeMemory(device, shaderBindingTableBufferMemory, NULL);
        vkDestroyBuffer(device, shaderBindingTableBuffer, NULL);

        vkFreeMemory(device, materialBufferMemory, NULL);
        vkDestroyBuffer(device, materialBuffer, NULL);

        vkFreeMemory(device, materialIndexBufferMemory, NULL);
        vkDestroyBuffer(device, materialIndexBuffer, NULL);

        vkDestroyImageView(device, rayTracingImageView, NULL);
        vkFreeMemory(device, rayTracingImageMemory, NULL);
        vkDestroyImage(device, rayTracingImage, NULL);

        vkFreeMemory(device, uniformBufferMemory, NULL);
        vkDestroyBuffer(device, uniformBuffer, NULL);

        vkDestroyAccelerationStructureKHR(device, topLevelAccelerationStructure, NULL);
        vkFreeMemory(device, topLevelAccelerationStructureBufferMemory, NULL);
        vkDestroyBuffer(device, topLevelAccelerationStructureBuffer, NULL);

        vkDestroyAccelerationStructureKHR(device, bottomLevelAccelerationStructure, NULL);
        vkFreeMemory(device, bottomLevelAccelerationStructureBufferMemory, NULL);
        vkDestroyBuffer(device, bottomLevelAccelerationStructureBuffer, NULL);

        vkFreeMemory(device, indexBufferMemory, NULL);
        vkDestroyBuffer(device, indexBuffer, NULL);
        vkFreeMemory(device, vertexBufferMemory, NULL);
        vkDestroyBuffer(device, vertexBuffer, NULL);
        vkDestroyPipeline(device, rayTracingPipeline, NULL);

        vkDestroyPipelineLayout(device, pipelineLayout, NULL);

        vkDestroyDescriptorSetLayout(device, materialDescriptorSetLayout, NULL);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, NULL);
        vkDestroyDescriptorPool(device, descriptorPool, NULL);

        for (uint32_t x = 0; x < swapChainImages.size(); x++)
        {
            vkDestroyImageView(device, swapChainImageViews[x], NULL);
        }
        vkDestroySwapchainKHR(device, swapChain, NULL);

        vkDestroyCommandPool(device, commandPool, NULL);
        vkDestroyDevice(device, NULL);
        vkDestroySurfaceKHR(instance, surface, NULL);

        vkDestroyInstance(instance, NULL);

        glfwDestroyWindow(window);

        glfwTerminate();
    }
};

int main()
{
    HelloVulkanRayTracingApplication app;

    try
    {
        app.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
