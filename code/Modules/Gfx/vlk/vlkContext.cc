//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//  vlkContext.cc
//------------------------------------------------------------------------------
#include "Pre.h"
#include "vlk_impl.h"
#include "vlkContext.h"
#include "vlkTypes.h"

// extension function pointers
#define INST_FUNC_PTR(inst, func) \
{ \
    fp##func = (PFN_vk##func)vkGetInstanceProcAddr(inst, "vk" #func);\
    o_assert(fp##func);\
}

static PFN_vkCreateDebugReportCallbackEXT fpCreateDebugReportCallbackEXT = nullptr;
static PFN_vkDestroyDebugReportCallbackEXT fpDestroyDebugReportCallbackEXT = nullptr;
static PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR = nullptr;
static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR = nullptr;;

namespace Oryol {
namespace _priv {

//------------------------------------------------------------------------------
vlkContext::~vlkContext() {
    o_assert(nullptr == this->Instance);
    o_assert(nullptr == this->PhysicalDevice);
    o_assert(nullptr == this->Device);
    o_assert(0 == this->Surface);
    o_assert(nullptr == this->instLayers);
    o_assert(nullptr == this->instExtensions);
    o_assert(nullptr == this->physDevices);
    o_assert(nullptr == this->devLayers);
    o_assert(nullptr == this->devExtensions);
    o_assert(nullptr == this->queueProps);
    o_assert(nullptr == this->surfaceFormats);
}

//------------------------------------------------------------------------------
void
vlkContext::setup(const GfxSetup& setup, const char** requiredInstanceExtensions, int numRequiredInstanceExtensions) {
    o_assert(nullptr == this->Instance);
    o_assert(nullptr == this->PhysicalDevice);
    o_assert(nullptr == this->Device);

    #if ORYOL_DEBUG
    const char* requestedLayers[] = {
        "VK_LAYER_LUNARG_api_dump",
        "VK_LAYER_LUNARG_device_limits",
        "VK_LAYER_LUNARG_draw_state",
        "VK_LAYER_LUNARG_image",
        "VK_LAYER_LUNARG_mem_tracker",
        "VK_LAYER_LUNARG_object_tracker",
        "VK_LAYER_LUNARG_param_checker",
        "VK_LAYER_LUNARG_swapchain",
        "VK_LAYER_LUNARG_threading",
        "VK_LAYER_GOOGLE_unique_objects",
    };
    const int numRequestedLayers = int(std::size(requestedLayers));
    #else
    const char** requestedLayers = nullptr;
    int numRequestedLayers = 0;
    #endif
    const char* requiredDeviceExtensions[] = {
        "VK_KHR_swapchain"
    };
    const int numRequiredDeviceExtensions = int(std::size(requiredDeviceExtensions));

    this->setupInstanceLayers(requestedLayers, numRequestedLayers);
    this->setupInstanceExtensions(requiredInstanceExtensions, numRequiredInstanceExtensions);
    this->setupInstance(setup);
    #if ORYOL_DEBUG
    this->setupErrorReporting();
    #endif
    this->setupPhysicalDevice(setup);
    this->setupDeviceLayers(requestedLayers, numRequestedLayers);
    this->setupDeviceExtensions(requiredDeviceExtensions, numRequiredDeviceExtensions);
    this->setupQueueFamilies();
}

//------------------------------------------------------------------------------
void
vlkContext::discard() {
    this->discardCommandPoolAndBuffers();
    this->discardSurfaceFormats();
    this->discardDeviceAndSurface();
    this->discardQueueFamilies();
    this->discardDeviceExtensions();
    this->discardDeviceLayers();
    this->discardPhysicalDevice();
    #if ORYOL_DEBUG
    this->discardErrorReporting();
    #endif
    this->discardInstance();
    this->discardInstanceExtensions();
    this->discardInstanceLayers();
}

//------------------------------------------------------------------------------
int
vlkContext::findLayer(const char* name, const VkLayerProperties* layers, int numLayers) {
    o_assert_dbg(name && layers && (numLayers >= 0));
    for (int i = 0; i < numLayers; i++) {
        if (0 == std::strcmp(name, layers[i].layerName)) {
            return i;
        }
    }
    return InvalidIndex;
}

//------------------------------------------------------------------------------
void
vlkContext::dumpLayerInfo(const char* title, const VkLayerProperties* layerProps, int numLayers) {
    o_assert_dbg(title && layerProps && (numLayers >= 0));
    Log::Info(">>> %s:\n", title);
    for (int i = 0; i < numLayers; i++) {
        const VkLayerProperties& cur = layerProps[i];
        Log::Info("  Name: %s\n    spec version: %x\n    impl version: %x\n    desc: %s\n\n",
            cur.layerName, cur.specVersion, cur.implementationVersion, cur.description);
    }
}

//------------------------------------------------------------------------------
void
vlkContext::selectLayers(const char** reqLayers, int numReqLayers, 
                         const VkLayerProperties* availLayers, int numAvailLayers,
                         const char** outSelLayers, int& inOutNumSelLayers) {
    if (reqLayers) {
        o_assert(numReqLayers <= maxSelLayers);
        for (int i = 0; i < numReqLayers; i++) {
            o_assert_dbg(reqLayers[i]);
            int availLayerIndex = findLayer(reqLayers[i], availLayers, numAvailLayers);
            if (InvalidIndex != availLayerIndex) {
                outSelLayers[inOutNumSelLayers++] = availLayers[availLayerIndex].layerName;
            }
            else {
                o_warn("vlkContext: requested instance layer '%s' not found!\n", reqLayers[i]);
            }
        }
    }
}

//------------------------------------------------------------------------------
int
vlkContext::findExtension(const char* name, const VkExtensionProperties* exts, int numExts) {
    o_assert_dbg(name && exts && (numExts >= 0));
    for (int i = 0; i < numExts; i++) {
        if (0 == std::strcmp(name, exts[i].extensionName)) {
            return i;
        }
    }
    return InvalidIndex;
}

//------------------------------------------------------------------------------
void
vlkContext::dumpExtensionInfo(const char* title, const VkExtensionProperties* exts, int numExts) {
    o_assert_dbg(title && exts && (numExts >= 0));
    Log::Info(">>> %s:\n", title);
    for (int i = 0; i < numExts; i++) {
        const VkExtensionProperties& cur = exts[i];
        Log::Info("  %s (ver %x)\n", cur.extensionName, cur.specVersion);
    }
}

//------------------------------------------------------------------------------
void
vlkContext::selectExtensions(const char** reqExts, int numReqExts, 
                             const VkExtensionProperties* availExts, int numAvailExts,
                             const char** outSelExts, int& inOutNumSelExts) {
    if (reqExts) {
        o_assert(numReqExts < maxSelExtensions);
        for (int i = 0; i < numReqExts; i++) {
            o_assert_dbg(reqExts[i]);
            int availExtIndex = findExtension(reqExts[i], availExts, numAvailExts);
            if (InvalidIndex != availExtIndex) {
                outSelExts[inOutNumSelExts++] = availExts[availExtIndex].extensionName;
            }
            else {
                o_warn("vlkContext: requested instance extension '%s' not found!\n", reqExts[i]);
            }
        }
    }
}

//------------------------------------------------------------------------------
void
vlkContext::setupInstanceLayers(const char** layers, int numLayers) {
    o_assert(nullptr == this->instLayers);
    o_assert(0 == this->numInstLayers);
    o_assert(0 == this->numSelInstLayers);

    // first get number of instance layers, allocate room, and get layer properties
    VkResult err = vkEnumerateInstanceLayerProperties(&this->numInstLayers, nullptr);
    o_assert(!err);
    if (this->numInstLayers > 0) {
        const int size = this->numInstLayers * sizeof(VkLayerProperties);
        this->instLayers = (VkLayerProperties*) Memory::Alloc(size);
        err = vkEnumerateInstanceLayerProperties(&this->numInstLayers, this->instLayers);
    }

    // dump out all instance layers
    this->dumpLayerInfo("Instance Layers", this->instLayers, this->numInstLayers);

    // select requested layers
    this->selectLayers(layers, numLayers, this->instLayers, this->numInstLayers, this->selInstLayers, this->numSelInstLayers);
}

//------------------------------------------------------------------------------
void
vlkContext::discardInstanceLayers() {
    if (this->instLayers) {
        Memory::Free(this->instLayers);
        this->instLayers = nullptr;
    }
    this->numInstLayers = 0;
}

//------------------------------------------------------------------------------
void
vlkContext::setupInstanceExtensions(const char** exts, int numExts) {
    o_assert(nullptr == this->instExtensions);
    o_assert(0 == this->numInstExtensions);
    o_assert(0 == this->numSelInstExtensions);

    // enumerate available extensions
    VkResult err = vkEnumerateInstanceExtensionProperties(nullptr, &this->numInstExtensions, nullptr);
    o_assert(!err);
    if (this->numInstExtensions > 0) {
        const int size = this->numInstExtensions * sizeof(VkExtensionProperties);
        this->instExtensions = (VkExtensionProperties*) Memory::Alloc(size);
        err = vkEnumerateInstanceExtensionProperties(nullptr, &this->numInstExtensions, this->instExtensions);
    }

    // dump extension info
    dumpExtensionInfo("Instance Extensions", this->instExtensions, this->numInstExtensions); 

    // filter requested extensions against available extensions
    // FIXME: need to distinguish between optional and required extensions!
    selectExtensions(exts, numExts, this->instExtensions, this->numInstExtensions, this->selInstExtensions, this->numSelInstExtensions);
    #if ORYOL_DEBUG
    const char* dbgExts[] = {
        VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
    };
    selectExtensions(dbgExts, (int)std::size(dbgExts), this->instExtensions, this->numInstExtensions, this->selInstExtensions, this->numSelInstExtensions);
    #endif
}

//------------------------------------------------------------------------------
void
vlkContext::discardInstanceExtensions() {
    if (this->instExtensions) {
        Memory::Free(this->instExtensions);
        this->instExtensions = nullptr;
    }
    this->numInstExtensions = 0;
}

//------------------------------------------------------------------------------
void
vlkContext::setupInstance(const GfxSetup& setup) {
    o_assert(nullptr == this->Instance);

    VkApplicationInfo appInfo = { };
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pNext = nullptr;
    appInfo.pApplicationName = "ORYOL_APP";    // FIXME
    appInfo.applicationVersion = 0;
    appInfo.pEngineName = "ORYOL";
    appInfo.engineVersion = 0;
    appInfo.apiVersion = VK_API_VERSION;

    VkInstanceCreateInfo instInfo = { };
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pNext = nullptr;
    instInfo.flags = 0;
    instInfo.pApplicationInfo = &appInfo;
    instInfo.enabledLayerCount = this->numSelInstLayers;
    instInfo.ppEnabledLayerNames = this->selInstLayers;
    instInfo.enabledExtensionCount = this->numSelInstExtensions;
    instInfo.ppEnabledExtensionNames = this->selInstExtensions;
    
    VkResult err = vkCreateInstance(&instInfo, nullptr, &this->Instance);
    o_assert(!err); // FIXME: better error messages
    o_assert(this->Instance);
}

//------------------------------------------------------------------------------
void
vlkContext::discardInstance() {
    o_assert(this->Instance);
    vkDestroyInstance(this->Instance, nullptr);
    this->Instance = nullptr;
}

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL
errorReportingCallback(VkFlags msgFlags, VkDebugReportObjectTypeEXT objType,
                       uint64_t srcObject, size_t location, int32_t msgCode, 
                       const char* pLayerPrefix, const char* pMsg, void* pUserData)
{
    const char* type = "UNKNOWN";
    if (msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        type = "ERROR";
    }
    else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
        type = "WARNING";
    }
    else if (msgFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
        type = "PERFORMANCE";
    }
    else if (msgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
        type = "INFO";
    }
    else if (msgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
        type = "DEBUG";
    }
    Log::Info("VULKAN [%s] [%s] [%d]: %s\n", type, pLayerPrefix, msgCode, pMsg);
    /*
    * false indicates that layer should not bail-out of an
    * API call that had validation failures. This may mean that the
    * app dies inside the driver due to invalid parameter(s).
    * That's what would happen without validation layers, so we'll
    * keep that behavior here.
    */
    return false;
}
#endif

//------------------------------------------------------------------------------
#if ORYOL_DEBUG
void
vlkContext::setupErrorReporting() {
    o_assert(this->Instance);
    o_assert(nullptr == this->debugReportCallback);

    INST_FUNC_PTR(this->Instance, CreateDebugReportCallbackEXT);
    VkDebugReportCallbackCreateInfoEXT dbgCreateInfo = { };
    dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    dbgCreateInfo.pNext = nullptr;
    dbgCreateInfo.flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT |
                          VK_DEBUG_REPORT_ERROR_BIT_EXT |
                          VK_DEBUG_REPORT_WARNING_BIT_EXT |
                          VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
                          VK_DEBUG_REPORT_DEBUG_BIT_EXT;
    dbgCreateInfo.pfnCallback = errorReportingCallback;
    dbgCreateInfo.pUserData = nullptr;
    VkResult err = fpCreateDebugReportCallbackEXT(this->Instance, &dbgCreateInfo, nullptr, &this->debugReportCallback);
    o_assert(!err);
}
#endif

//------------------------------------------------------------------------------
#ifdef ORYOL_DEBUG
void
vlkContext::discardErrorReporting() {
    o_assert(this->Instance);
    o_assert(this->debugReportCallback);

    INST_FUNC_PTR(this->Instance, DestroyDebugReportCallbackEXT);
    fpDestroyDebugReportCallbackEXT(this->Instance, this->debugReportCallback, nullptr);
    this->debugReportCallback = nullptr;
}
#endif

//------------------------------------------------------------------------------
void
vlkContext::setupPhysicalDevice(const GfxSetup& setup) {
    o_assert(nullptr == this->PhysicalDevice);
    o_assert(nullptr == this->physDevices);
    o_assert(0 == this->numPhysDevices);
    o_assert(this->Instance);

    // get number of physical devices
    VkResult err = vkEnumeratePhysicalDevices(this->Instance, &this->numPhysDevices, nullptr);
    o_assert(!err);
    if (this->numPhysDevices <= 0) {
        o_error("No Vulkan-enabled GPUs in system!\n");
    }
    this->physDevices = (VkPhysicalDevice*) Memory::Alloc(this->numPhysDevices * sizeof(VkPhysicalDevice));
    err = vkEnumeratePhysicalDevices(this->Instance, &this->numPhysDevices, this->physDevices);
    o_assert(!err);

    // dump device info, and also pick first dedicated GPU
    int firstDiscreteGPU = InvalidIndex;
    VkPhysicalDeviceProperties props = { };
    for (uint32 i = 0; i < this->numPhysDevices; i++) {
        vkGetPhysicalDeviceProperties(this->physDevices[i], &props);
        Log::Info("GPU %d: %s (type: %s)\n", i, props.deviceName, vlkTypes::physicalDeviceTypeAsString(props.deviceType));
        if ((InvalidIndex == firstDiscreteGPU) &&
            (VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU == props.deviceType)) {
            firstDiscreteGPU = i;
        }
    }
    
    // choose the 'best' physical device
    // FIXME: might want to have more control over this
    if (InvalidIndex != firstDiscreteGPU) {
        this->PhysicalDevice = this->physDevices[firstDiscreteGPU];
    }
    else {
        this->PhysicalDevice = this->physDevices[0];
    }
}

//------------------------------------------------------------------------------
void
vlkContext::discardPhysicalDevice() {
    o_assert(this->PhysicalDevice);
    o_assert(this->physDevices);

    Memory::Free(this->physDevices);
    this->physDevices = nullptr;
    this->PhysicalDevice = nullptr;
}

//------------------------------------------------------------------------------
void
vlkContext::setupDeviceLayers(const char** layers, int numLayers) {
    o_assert(this->PhysicalDevice);
    o_assert(0 == this->numDevLayers);
    o_assert(nullptr == this->devLayers);

    VkResult err = vkEnumerateDeviceLayerProperties(this->PhysicalDevice, &this->numDevLayers, nullptr);
    o_assert(!err);
    if (this->numDevLayers > 0) {
        const int size = this->numDevLayers * sizeof(VkLayerProperties);
        this->devLayers = (VkLayerProperties*) Memory::Alloc(size);
        err = vkEnumerateDeviceLayerProperties(this->PhysicalDevice, &this->numDevLayers, this->devLayers);
    }
    dumpLayerInfo("Device Layers", this->devLayers, this->numDevLayers);
    selectLayers(layers, numLayers, this->devLayers, this->numDevLayers, this->selDevLayers, this->numSelDevLayers);
}

//------------------------------------------------------------------------------
void
vlkContext::discardDeviceLayers() {
    if (this->devLayers) {
        Memory::Free(this->devLayers);
        this->devLayers = nullptr;
    }
    this->numDevLayers = 0;
}

//------------------------------------------------------------------------------
void
vlkContext::setupDeviceExtensions(const char** exts, int numExts) {
    o_assert(nullptr == this->devExtensions);
    o_assert(0 == this->numDevExtensions);
    o_assert(0 == this->numSelDevExtensions);

    // enumerate available device extensions
    VkResult err = vkEnumerateDeviceExtensionProperties(this->PhysicalDevice, nullptr, &this->numDevExtensions, nullptr);
    o_assert(!err);
    if (this->numDevExtensions > 0) {
        const int size = this->numDevExtensions * sizeof(VkExtensionProperties);
        this->devExtensions = (VkExtensionProperties*)Memory::Alloc(size);
        err = vkEnumerateDeviceExtensionProperties(this->PhysicalDevice, nullptr, &this->numDevExtensions, this->devExtensions);
    }

    // dump device extension info
    dumpExtensionInfo("Device Extensions", this->devExtensions, this->numDevExtensions);

    // filter requested extensions against available extensions
    // FIXME: need to distinguish between optional and required extensions!
    selectExtensions(exts, numExts, this->devExtensions, this->numDevExtensions, this->selDevExtensions, this->numSelDevExtensions);
}

//------------------------------------------------------------------------------
void
vlkContext::discardDeviceExtensions() {
    if (this->devExtensions) {
        Memory::Free(this->devExtensions);
        this->devExtensions = nullptr;
    }
    this->numDevExtensions = 0;
}

//------------------------------------------------------------------------------
void
vlkContext::setupQueueFamilies() {
    o_assert(this->PhysicalDevice);
    o_assert(nullptr == this->queueProps);
    o_assert(0 == this->numQueues);

    vkGetPhysicalDeviceQueueFamilyProperties(this->PhysicalDevice, &this->numQueues, nullptr);
    o_assert(this->numQueues >= 1);
    const int size = sizeof(VkQueueFamilyProperties) * this->numQueues;
    this->queueProps = (VkQueueFamilyProperties*) Memory::Alloc(size);
    vkGetPhysicalDeviceQueueFamilyProperties(this->PhysicalDevice, &this->numQueues, this->queueProps);
}

//------------------------------------------------------------------------------
void
vlkContext::discardQueueFamilies() {
    if (this->queueProps) {
        Memory::Free(this->queueProps);
        this->queueProps = nullptr;
    }
    this->numQueues = 0;
}

//------------------------------------------------------------------------------
void
vlkContext::initQueueIndices(VkSurfaceKHR surf) {
    o_assert(this->PhysicalDevice);
    o_assert(this->Surface);
    o_assert(InvalidIndex == this->graphicsQueueIndex);
    o_assert(InvalidIndex == this->presentQueueIndex);

    // lookup required function pointers
    INST_FUNC_PTR(this->Instance, GetPhysicalDeviceSurfaceSupportKHR);

    // find a graphics and present queue, prefer one queue that supports both
    for (uint32 queueIndex = 0; queueIndex < this->numQueues; queueIndex++) {
        const auto& cur = this->queueProps[queueIndex];
        if (cur.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            if (InvalidIndex == this->graphicsQueueIndex) {
                this->graphicsQueueIndex = queueIndex;
            }
            VkBool32 supportsPresent = VK_FALSE;
            fpGetPhysicalDeviceSurfaceSupportKHR(this->PhysicalDevice, queueIndex, this->Surface, &supportsPresent);
            if (VK_TRUE == supportsPresent) {
                this->graphicsQueueIndex = queueIndex;
                this->presentQueueIndex = queueIndex;
                break;
            }
        }
    }
    if (InvalidIndex == this->presentQueueIndex) {
        // no queue which can do both graphics and present, find separate present queue
        o_warn("vlkContext: trying to find separate present queue\n");
        for (uint32 queueIndex = 0; queueIndex < this->numQueues; queueIndex++) {
            VkBool32 supportsPresent = VK_FALSE;
            fpGetPhysicalDeviceSurfaceSupportKHR(this->PhysicalDevice, queueIndex, this->Surface, &supportsPresent);
            if (VK_TRUE == supportsPresent) {
                this->presentQueueIndex = queueIndex;
                break;
            }
        }
    }
    if ((InvalidIndex == this->graphicsQueueIndex) || (InvalidIndex == this->presentQueueIndex)) {
        o_error("vlkContext: Could not find graphics or present queue\n");
    }
    // FIXME: handle separate graphics and present queue
    if (this->graphicsQueueIndex != this->presentQueueIndex) {
        o_error("vlkContext: separate graphics and present queue not supported\n");
    }
}

//------------------------------------------------------------------------------
void
vlkContext::setupDevice() {
    o_assert(this->PhysicalDevice);
    o_assert(this->Surface);
    o_assert(nullptr == this->Device);

    float queuePrios[] = { 1.0 };
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.pNext = nullptr;
    queueCreateInfo.flags = 0;
    queueCreateInfo.queueFamilyIndex = this->graphicsQueueIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = queuePrios;

    VkDeviceCreateInfo devCreateInfo = {};
    devCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devCreateInfo.pNext = nullptr;
    devCreateInfo.flags = 0;
    devCreateInfo.queueCreateInfoCount = 1;
    devCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    #if ORYOL_DEBUG
    devCreateInfo.enabledLayerCount = this->numSelDevLayers;
    devCreateInfo.ppEnabledLayerNames = this->selDevLayers;
    #else
    devCreateInfo.enabledLayerCount = 0;
    devCreateInfo.ppEnabledLayerNames = nullptr;
    #endif
    devCreateInfo.enabledExtensionCount = this->numSelDevExtensions;
    devCreateInfo.ppEnabledExtensionNames = this->selDevExtensions;
    devCreateInfo.pEnabledFeatures = nullptr;   // FIXME: enable optional features

    VkResult createDeviceError = vkCreateDevice(this->PhysicalDevice, &devCreateInfo, nullptr, &this->Device);
    o_assert(!createDeviceError);
    o_assert(this->Device);
}

//------------------------------------------------------------------------------
void
vlkContext::discardDeviceAndSurface() {
    o_assert(this->Device);
    o_assert(this->Surface);
    vkDestroyDevice(this->Device, nullptr);
    this->Device = nullptr;
    vkDestroySurfaceKHR(this->Instance, this->Surface, nullptr);
    this->Surface = 0;
}

//------------------------------------------------------------------------------
void
vlkContext::setupSurfaceFormats(const GfxSetup& setup) {
    o_assert(this->Instance);
    o_assert(this->PhysicalDevice);
    o_assert(this->Surface);
    o_assert(nullptr == this->surfaceFormats);
    o_assert(VK_FORMAT_MAX_ENUM == this->format);
    o_assert(VK_COLORSPACE_MAX_ENUM == this->colorSpace);

    // lookup required function pointers
    INST_FUNC_PTR(this->Instance, GetPhysicalDeviceSurfaceFormatsKHR);

    // get number of, and then allocate surface formats
    VkResult err = fpGetPhysicalDeviceSurfaceFormatsKHR(this->PhysicalDevice, this->Surface, &this->numSurfaceFormats, nullptr);
    o_assert(!err);
    o_assert(this->numSurfaceFormats > 0);
    this->surfaceFormats = (VkSurfaceFormatKHR*) Memory::Alloc(this->numSurfaceFormats * sizeof(VkSurfaceFormatKHR));
    o_assert(this->surfaceFormats);
    err = fpGetPhysicalDeviceSurfaceFormatsKHR(this->PhysicalDevice, this->Surface, &this->numSurfaceFormats, this->surfaceFormats);
    o_assert(!err);
    o_assert(this->numSurfaceFormats >= 1);

    // If the format list includes just one entry of VK_FORMAT_UNDEFINED,
    // the surface has no preferred format.  Otherwise, at least one
    // supported format will be returned.
    if ((1 == this->numSurfaceFormats) && (VK_FORMAT_UNDEFINED == this->surfaceFormats[0].format)) {
        this->format = VK_FORMAT_B8G8R8A8_UNORM;
    }
    else {
        this->format = this->surfaceFormats[0].format;
    }
    this->colorSpace = this->surfaceFormats[0].colorSpace;
}

//------------------------------------------------------------------------------
void
vlkContext::discardSurfaceFormats() {
    if (this->surfaceFormats) {
        Memory::Free(this->surfaceFormats);
        this->surfaceFormats = nullptr;
    }
    this->numSurfaceFormats = 0;
}

//------------------------------------------------------------------------------
void
vlkContext::setupCommandPoolAndBuffers() {
    o_assert(this->Device);
    o_assert(nullptr == this->cmdPool);
    o_assert(nullptr == this->cmdBuffers[0]);

    VkCommandPoolCreateInfo poolInfo = { };
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.pNext = nullptr;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = this->graphicsQueueIndex;
    VkResult err = vkCreateCommandPool(this->Device, &poolInfo, nullptr, &this->cmdPool);
    o_assert(!err && this->cmdPool);

    VkCommandBufferAllocateInfo bufInfo = { };
    bufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    bufInfo.pNext = nullptr;
    bufInfo.commandPool = this->cmdPool;
    bufInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    bufInfo.commandBufferCount = vlkConfig::NumFrames;
    err = vkAllocateCommandBuffers(this->Device, &bufInfo, this->cmdBuffers);
    o_assert(!err);
}

//------------------------------------------------------------------------------
void
vlkContext::discardCommandPoolAndBuffers() {
    o_assert(this->Device);
    o_assert(this->cmdPool);
    o_assert(this->cmdBuffers[0]);
    
    vkFreeCommandBuffers(this->Device, this->cmdPool, vlkConfig::NumFrames, this->cmdBuffers);
    for (int i = 0 ; i < vlkConfig::NumFrames; i++) {
        this->cmdBuffers[i] = nullptr;
    }
    vkDestroyCommandPool(this->Device, this->cmdPool, nullptr);
    this->cmdPool = nullptr;
}

//------------------------------------------------------------------------------
void
vlkContext::setupDeviceAndSwapChain(const GfxSetup& setup, const DisplayAttrs& attrs, VkSurfaceKHR surf) {
    o_assert(this->PhysicalDevice);
    o_assert(0 == this->Surface);
    o_assert(surf);
    
    this->Surface = surf;
    this->initQueueIndices(surf);
    this->setupDevice();
    vkGetDeviceQueue(this->Device, this->graphicsQueueIndex, 0, &this->Queue);
    this->setupSurfaceFormats(setup);
    this->setupCommandPoolAndBuffers();
}

} // namespace _priv
} // namespace Oryol