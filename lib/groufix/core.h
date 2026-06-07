/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_H_
#define GFX_CORE_H_

#include "groufix/containers/io.h"
#include "groufix/containers/list.h"
#include "groufix/containers/vec.h"
#include "groufix/core/threads.h"
#include "groufix/core/time.h"
#include "groufix.h"

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"


// Least Vulkan version that must be supported.
#define GFX_VK_API_VERSION_ VK_MAKE_API_VERSION(0,1,1,0)

// Vulkan function pointer.
#define GFX_VK_PFN_(pName) PFN_vk##pName pName

// Auto log the result of a call with return type VkResult.
#define GFX_VK_CHECK_(eval, action) \
	do { \
		VkResult gfx_vk_result_ = eval; \
		if (gfx_vk_result_ != VK_SUCCESS) { \
			gfx_log_error("Vulkan: %s", \
				gfx_vulkan_result_string_(gfx_vk_result_)); \
			action; \
		} \
	} while (0)

// User device handle (can be NULL) to internal handle, assigned to an lvalue.
#define GFX_GET_DEVICE_(lvalue, device) \
	do { \
		lvalue = (GFXDevice_*)(device == NULL ? \
			gfx_get_primary_device() : device); \
	} while (0)

// Ensures a Vulkan context exists for a device and assignes it to an lvalue.
#define GFX_GET_CONTEXT_(lvalue, device, action) \
	do { \
		lvalue = gfx_device_init_context_((GFXDevice_*)(device == NULL ? \
			gfx_get_primary_device() : device)); \
		if (lvalue == NULL) \
			action; \
	} while (0)

// Resolves a constrained input/output format and assigns it to another lvalue.
#define GFX_RESOLVE_FORMAT_(ioFmt, vkFmt, device, props, action) \
	do { \
		VkFormatProperties gfx_vk_props_ = props; \
		vkFmt = gfx_resolve_format_(device, &(ioFmt), &gfx_vk_props_); \
		if (vkFmt == VK_FORMAT_UNDEFINED) \
			action; \
	} while (0)


/**
 * Thread local data.
 */
typedef struct GFXThreadState_
{
	uintmax_t id;


	// Logging data.
	struct
	{
		GFXLogLevel level;
		GFXBufWriter out; // `dest` is NULL if disabled.

	} log;

} GFXThreadState_;


/**
 * groufix global data, i.e. groufix state.
 */
typedef struct GFXState_
{
	atomic_bool initialized;

	// Only pre-initialized field besides `initialized`.
	GFXLogLevel logDef;

	GFXClock_ clock;

	GFXVec  devices;  // Stores GFXDevice_ (never changes, so not dynamic).
	GFXList contexts; // References GFXContext_.
	GFXVec  monitors; // Stores GFXMonitor_* (pointers for access by index).
	GFXVec  gamepads; // Stores GFXGamepad_* (pointers for access by index).

	GFXMutex_ contextLock;

	// Monitor configuration change.
	void (*monitorEvent)(GFXMonitor*, bool);

	// Gamepad configuration change.
	void (*gamepadEvent)(GFXGamepad*, bool);


	// Thread local data access.
	struct
	{
		GFXThreadKey_ key; // Stores GFXThreadState_*.
		GFXMutex_     ioLock;

		// Next thread id.
		atomic_uintmax_t id;

	} thread;


	// Vulkan fields.
	struct
	{
		VkInstance instance;
#if defined (GFX_USE_VK_VALIDATION_LAYERS)
		VkDebugUtilsMessengerEXT messenger;
#endif

		GFX_VK_PFN_(CreateInstance);
		GFX_VK_PFN_(EnumerateInstanceVersion);

#if defined (GFX_USE_VK_VALIDATION_LAYERS)
		GFX_VK_PFN_(CreateDebugUtilsMessengerEXT);
		GFX_VK_PFN_(DestroyDebugUtilsMessengerEXT);
#endif
		GFX_VK_PFN_(CreateDevice);
		GFX_VK_PFN_(DestroyInstance);
		GFX_VK_PFN_(DestroySurfaceKHR);
#if defined (GFX_USE_VK_SUBSET_DEVICES)
		GFX_VK_PFN_(EnumerateDeviceExtensionProperties);
#endif
		GFX_VK_PFN_(EnumeratePhysicalDeviceGroups);
		GFX_VK_PFN_(EnumeratePhysicalDevices);
		GFX_VK_PFN_(GetDeviceProcAddr);
		GFX_VK_PFN_(GetPhysicalDeviceFeatures);
		GFX_VK_PFN_(GetPhysicalDeviceFeatures2);
		GFX_VK_PFN_(GetPhysicalDeviceFormatProperties);
		GFX_VK_PFN_(GetPhysicalDeviceMemoryProperties);
		GFX_VK_PFN_(GetPhysicalDeviceProperties);
		GFX_VK_PFN_(GetPhysicalDeviceProperties2);
		GFX_VK_PFN_(GetPhysicalDeviceQueueFamilyProperties);
		GFX_VK_PFN_(GetPhysicalDeviceSurfaceCapabilitiesKHR);
		GFX_VK_PFN_(GetPhysicalDeviceSurfaceFormatsKHR);
		GFX_VK_PFN_(GetPhysicalDeviceSurfacePresentModesKHR);
		GFX_VK_PFN_(GetPhysicalDeviceSurfaceSupportKHR);

	} vk;

} GFXState_;


/****************************
 * Vulkan context (superset of a logical device).
 ****************************/

/**
 * Logical (actually created) Vulkan queue family.
 */
typedef struct GFXQueueSet_
{
	GFXListNode list; // Base-type.

	VkQueueFlags flags;    // Only contains flags it was chosen for.
	VkQueueFlags allFlags; // All Vulkan queue capabilities.
	bool         present;  // Non-zero if chosen for presentation.
	uint32_t     family;   // Vulkan family index.

	size_t       count;
	GFXMutex_    locks[]; // Count mutexes, one for each queue.

} GFXQueueSet_;


/**
 * Logical Vulkan queue handle.
 */
typedef struct GFXQueue_
{
	uint32_t   family; // Vulkan family index.
	uint32_t   index;  // Vulkan queue index.
	GFXMutex_* lock;


	// Vulkan fields.
	struct
	{
		VkQueue queue;

	} vk;

} GFXQueue_;


/**
 * Logical Vulkan context.
 */
typedef struct GFXContext_
{
	GFXListNode list; // Base-type.
	GFXList     sets; // References GFXQueueSet_.


	// Supported feature flags.
	enum
	{
		GFX_SUPPORT_GEOMETRY_SHADER_     = 0x0001,
		GFX_SUPPORT_TESSELLATION_SHADER_ = 0x0002

	} features;


	// Memory limits (queried once).
	struct
	{
		// Limits.
		uint32_t maxAllocs;
		uint32_t maxSamplers;

		// Counters.
		atomic_uint_fast32_t allocs;
		atomic_uint_fast32_t samplers;
		atomic_uintptr_t     shaders;

	} limits;


	// Vulkan fields.
	struct
	{
		VkDevice device;

		GFX_VK_PFN_(AcquireNextImageKHR);
		GFX_VK_PFN_(AllocateCommandBuffers);
		GFX_VK_PFN_(AllocateDescriptorSets);
		GFX_VK_PFN_(AllocateMemory);
		GFX_VK_PFN_(BeginCommandBuffer);
		GFX_VK_PFN_(BindBufferMemory);
		GFX_VK_PFN_(BindImageMemory);
		GFX_VK_PFN_(CmdBeginRenderPass);
		GFX_VK_PFN_(CmdBindDescriptorSets);
		GFX_VK_PFN_(CmdBindIndexBuffer);
		GFX_VK_PFN_(CmdBindPipeline);
		GFX_VK_PFN_(CmdBindVertexBuffers);
		GFX_VK_PFN_(CmdBlitImage);
		GFX_VK_PFN_(CmdCopyBuffer);
		GFX_VK_PFN_(CmdCopyImage);
		GFX_VK_PFN_(CmdCopyBufferToImage);
		GFX_VK_PFN_(CmdCopyImageToBuffer);
		GFX_VK_PFN_(CmdDispatch);
		GFX_VK_PFN_(CmdDispatchBase);
		GFX_VK_PFN_(CmdDispatchIndirect);
		GFX_VK_PFN_(CmdDraw);
		GFX_VK_PFN_(CmdDrawIndexed);
		GFX_VK_PFN_(CmdDrawIndexedIndirect);
		GFX_VK_PFN_(CmdDrawIndirect);
		GFX_VK_PFN_(CmdEndRenderPass);
		GFX_VK_PFN_(CmdExecuteCommands);
		GFX_VK_PFN_(CmdNextSubpass);
		GFX_VK_PFN_(CmdPipelineBarrier);
		GFX_VK_PFN_(CmdPushConstants);
		GFX_VK_PFN_(CmdResolveImage);
		GFX_VK_PFN_(CmdSetLineWidth);
		GFX_VK_PFN_(CmdSetScissor);
		GFX_VK_PFN_(CmdSetViewport);
		GFX_VK_PFN_(CreateBuffer);
		GFX_VK_PFN_(CreateBufferView);
		GFX_VK_PFN_(CreateCommandPool);
		GFX_VK_PFN_(CreateComputePipelines);
		GFX_VK_PFN_(CreateDescriptorPool);
		GFX_VK_PFN_(CreateDescriptorSetLayout);
		GFX_VK_PFN_(CreateDescriptorUpdateTemplate);
		GFX_VK_PFN_(CreateFence);
		GFX_VK_PFN_(CreateFramebuffer);
		GFX_VK_PFN_(CreateGraphicsPipelines);
		GFX_VK_PFN_(CreateImage);
		GFX_VK_PFN_(CreateImageView);
		GFX_VK_PFN_(CreatePipelineCache);
		GFX_VK_PFN_(CreatePipelineLayout);
		GFX_VK_PFN_(CreateRenderPass);
		GFX_VK_PFN_(CreateSampler);
		GFX_VK_PFN_(CreateSemaphore);
		GFX_VK_PFN_(CreateShaderModule);
		GFX_VK_PFN_(CreateSwapchainKHR);
		GFX_VK_PFN_(DestroyBuffer);
		GFX_VK_PFN_(DestroyBufferView);
		GFX_VK_PFN_(DestroyCommandPool);
		GFX_VK_PFN_(DestroyDescriptorPool);
		GFX_VK_PFN_(DestroyDescriptorSetLayout);
		GFX_VK_PFN_(DestroyDescriptorUpdateTemplate);
		GFX_VK_PFN_(DestroyDevice);
		GFX_VK_PFN_(DestroyFence);
		GFX_VK_PFN_(DestroyFramebuffer);
		GFX_VK_PFN_(DestroyImage);
		GFX_VK_PFN_(DestroyImageView);
		GFX_VK_PFN_(DestroyPipeline);
		GFX_VK_PFN_(DestroyPipelineCache);
		GFX_VK_PFN_(DestroyPipelineLayout);
		GFX_VK_PFN_(DestroyRenderPass);
		GFX_VK_PFN_(DestroySampler);
		GFX_VK_PFN_(DestroySemaphore);
		GFX_VK_PFN_(DestroyShaderModule);
		GFX_VK_PFN_(DestroySwapchainKHR);
		GFX_VK_PFN_(DeviceWaitIdle);
		GFX_VK_PFN_(EndCommandBuffer);
		GFX_VK_PFN_(FreeCommandBuffers);
		GFX_VK_PFN_(FreeMemory);
		GFX_VK_PFN_(GetBufferMemoryRequirements);
		GFX_VK_PFN_(GetBufferMemoryRequirements2);
		GFX_VK_PFN_(GetDeviceQueue);
		GFX_VK_PFN_(GetFenceStatus);
		GFX_VK_PFN_(GetImageMemoryRequirements);
		GFX_VK_PFN_(GetImageMemoryRequirements2);
		GFX_VK_PFN_(GetPipelineCacheData);
		GFX_VK_PFN_(GetSwapchainImagesKHR);
		GFX_VK_PFN_(MapMemory);
		GFX_VK_PFN_(MergePipelineCaches);
		GFX_VK_PFN_(QueuePresentKHR);
		GFX_VK_PFN_(QueueSubmit);
		GFX_VK_PFN_(ResetCommandPool);
		GFX_VK_PFN_(ResetDescriptorPool);
		GFX_VK_PFN_(ResetFences);
		GFX_VK_PFN_(UnmapMemory);
		GFX_VK_PFN_(UpdateDescriptorSetWithTemplate);
		GFX_VK_PFN_(WaitForFences);

	} vk;


	// Associated device group.
	size_t           numDevices;
	VkPhysicalDevice devices[];

} GFXContext_;


/****************************
 * User visible objects.
 ****************************/

/**
 * Internal physical device.
 */
typedef struct GFXDevice_
{
	GFXDevice    base;
	uint32_t     api; // Vulkan API version.
	char         name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
	char         driverName[VK_MAX_DRIVER_NAME_SIZE];
	char         driverInfo[VK_MAX_DRIVER_INFO_SIZE];

#if defined (GFX_USE_VK_SUBSET_DEVICES)
	bool         subset; // If it is a non-conformant Vulkan implementation.
#endif

	GFXContext_* context;
	GFXMutex_    lock; // For initial context access.

	// Stores { GFXFormat, VkFormat, VkFormatProperties }.
	GFXVec formats;


	// Vulkan fields.
	struct
	{
		VkPhysicalDevice device;

	} vk;

} GFXDevice_;


/**
 * Internal monitor.
 */
typedef struct GFXMonitor_
{
	GFXMonitor   base;
	GLFWmonitor* handle;

	size_t       numModes;
	GFXVideoMode modes[]; // Available video modes.

} GFXMonitor_;


/**
 * Internal gamepad.
 */
typedef struct GFXGamepad_
{
	GFXGamepad base;
	int        jid;

} GFXGamepad_;


/**
 * Internal window.
 */
typedef struct GFXWindow_
{
	GFXWindow    base;
	GLFWwindow*  handle;
	GFXList      events; // References { GFXListNode, GFXWindowEvents, ... }.

	GFXDevice_*  device; // Associated GPU to build a swapchain on.
	GFXContext_* context;
	uint32_t     access[2]; // All Vulkan families with image access (UINT32_MAX for empty).

	// Swapchain 'lock' (window can only be used by one renderer).
	atomic_bool swap;


	// Frame (i.e Vulkan surface + swapchain) properties.
	struct
	{
		GFXVec   images; // Stores VkImage, only those of vk.swapchain.
		VkFormat format;
		uint32_t width;
		uint32_t height;

		// Recreate signal.
		atomic_bool recreate;

		// All new 'recreate' values are protected by a mutex.
		uint32_t       rWidth;  // Future width.
		uint32_t       rHeight; // Future height.
		GFXWindowFlags flags;   // Determines number of images.
		GFXMutex_      lock;

	} frame;


	// Vulkan fields.
	struct
	{
		VkSurfaceKHR   surface;
		VkSwapchainKHR swapchain;
		VkSwapchainKHR oldSwapchain; // Must be VK_NULL_HANDLE if swapchain is not.
		GFXVec         retired;      // Stores VkSwapchainKHR.

	} vk;

} GFXWindow_;


/****************************
 * Global and thread local state.
 ****************************/

/**
 * The only instance of global groufix state data.
 */
extern GFXState_ groufix_;


/**
 * The default logger (defaults to stderr) when no groufix state is known.
 */
extern GFXBufWriter gfx_io_buf_def_;


/**
 * Reads the default log level from the
 * GROUFIX_DEFAULT_LOG_LEVEL environment variable,
 * if present, it overwrites groufix_.logDef.
 */
void gfx_log_set_default_level_(void);

/**
 * Initializes global groufix state.
 * groufix_.initialized must be 0, on success it will be set to 1.
 * @return Non-zero on success.
 *
 * This will initialize everything to an empty or non-active state.
 */
bool gfx_init_(void);

/**
 * Terminates global groufix state.
 * groufix_.initialized must be 1, after this call it will be set to 0.
 * Must be called by the same thread that called gfx_state_init_.
 */
void gfx_terminate_(void);

/**
 * Allocates thread local state for the calling thread.
 * groufix_.initialized must be 1.
 * May not be called when data is already allocated on the calling thread.
 * @return Non-zero on success.
 *
 * This will initialize everything to an empty or non-active state.
 */
bool gfx_create_local_(void);

/**
 * Frees thread local state of the calling thread.
 * groufix_.initialized must be 1.
 * May not be called when no data is allocated on the calling thread.
 * All threads with local data need to call this before gfx_state_terminate_.
 */
void gfx_destroy_local_(void);

/**
 * Retrieves thread local state of the calling thread.
 * groufix_.initialized must be 1.
 * @return NULL if no state was allocated.
 */
GFXThreadState_* gfx_get_local_(void);


/****************************
 * Devices, monitors, gamepads and Vulkan contexts.
 ****************************/

/**
 * Retrieves a VkResult as a readable string.
 */
const char* gfx_vulkan_result_string_(VkResult result);

/**
 * Initializes Vulkan state.
 * groufix_.vk.instance must be NULL.
 * Must be called by the same thread that called gfx_state_init_.
 * @return Non-zero on success.
 */
bool gfx_vulkan_init_(void);

/**
 * Terminates Vulkan state.
 * Must be called before gfx_state_terminate_, on the same thread.
 */
void gfx_vulkan_terminate_(void);

/**
 * Initializes internal physical device (e.g. GPU) configuration.
 * groufix_.devices.size must be 0.
 * Must be called by the same thread that called gfx_vulkan_init_.
 * @return Non-zero on success.
 */
bool gfx_devices_init_(void);

/**
 * Terminates internal device configuration.
 * This will make sure all divices AND contexts are destroyed.
 * Must be called before gfx_vulkan_terminate_, on the same thread.
 */
void gfx_devices_terminate_(void);

/**
 * Initializes internal monitor configuration.
 * groufix_.monitors.size must be 0.
 * Must be called by the same thread that called gfx_state_init_.
 * @return Non-zero on success.
 */
bool gfx_monitors_init_(void);

/**
 * Terminates internal monitor configuration.
 * This will make sure all monitors are destroyed.
 * Must be called before gfx_state_terminate_, on the same thread.
 */
void gfx_monitors_terminate_(void);

/**
 * Initializes internal gamepad configuration.
 * groufix_.gamepads.size must be 0.
 * Must be called by the same thread that called gfx_state_init_.
 * @return Non-zero on success.
 */
bool gfx_gamepads_init_(void);

/**
 * Terminates internal gamepad configuration.
 * This will make sure all gamepads are destroyed.
 * Must be called before gfx_state_terminate_, on the same thread.
 */
void gfx_gamepads_terminate_(void);

/**
 * Initializes the groufix/Vulkan format 'dictionary',
 * i.e. initializes and fills the `formats` member of the device.
 * @param device Cannot be NULL.
 * @return Non-zero on success.
 */
bool gfx_device_init_formats_(GFXDevice_* device);

/**
 * Resolves a (potentially 'fuzzy') format to a supported Vulkan format.
 * The returned format will at least support all given format properties.
 * @param device Cannot be NULL.
 * @param format Input/output format, outputs the groufix equivalent to Vulkan.
 * @param props  Must-have format properties, may be NULL.
 * @return VK_FORMAT_UNDEFINED if not supported.
 */
VkFormat gfx_resolve_format_(GFXDevice_* device,
                             GFXFormat* fmt, const VkFormatProperties* props);

/**
 * Parses a Vulkan format, returning a supported groufix format.
 * @param device Cannot be NULL.
 * @param fmt    Vulkan format, can be any.
 * @return GFX_FORMAT_EMPTY if not supported.
 */
GFXFormat gfx_parse_format_(GFXDevice_* device, VkFormat fmt);

/**
 * Initializes the Vulkan context, no-op if it already exists
 * The device will share its context with all devices in its device group.
 * @param device Cannot be NULL.
 * @return NULL on failure.
 *
 * Completely thread-safe.
 * Once this function returned succesfully at least once for a given device,
 * we can read device->index and device->context directly without locking.
 */
GFXContext_* gfx_device_init_context_(GFXDevice_* device);

/**
 * Picks a queue set (i.e. Vulkan family) supporting the given abilities.
 * @param context Cannot be NULL.
 * @param family  Outputs the Vulkan family index, cannot be NULL.
 * @return The queue set it was picked from, NULL if none found.
 *
 * When only the presentation bit is set OR it is not and only one of the
 * VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_TRANSFER_BIT or VK_QUEUE_COMPUTE_BIT
 * flags are set, this function is guaranteed to succeed.
 */
GFXQueueSet_* gfx_pick_family_(GFXContext_* context, uint32_t* family,
                               VkQueueFlags flags, bool present);

/**
 * Picks a queue from the queue set supporting the given abilities.
 * @param queue Outputs the handle to the picked queue, cannot be NULL.
 * @see gfx_pick_family_.
 */
GFXQueueSet_* gfx_pick_queue_(GFXContext_* context, GFXQueue_* queue,
                              VkQueueFlags flags, bool present);

/**
 * Retrieves the index of a queue supporting the given abilities.
 * Useful when no queue is used, but identification is necessary.
 * @param set Must support flags and present.
 * @see gfx_pick_queue_.
 */
uint32_t gfx_queue_index_(GFXQueueSet_* set,
                          VkQueueFlags flags, bool present);

/**
 * Retrieves the Vulkan queue family indices to share with,
 * based on the memory flags used for a resource.
 * @param families Input/output array storing { graphics, compute, transfer }.
 * @return The number of unique families returned.
 *
 * families is overwritten with the Vulkan family indices to use.
 * If less than 3 families are to be used, trailing UINT32_MAXs are inserted.
 */
uint32_t gfx_filter_families_(GFXMemoryFlags flags, uint32_t* families);

/**
 * Atomically checks and increases a memory limit of a context.
 * @param limit Must be the limit associated with count, cannot change.
 * @return Non-zero if the limit could be increased.
 */
bool gfx_check_limit_(atomic_uint_fast32_t* count, uint32_t limit);


/****************************
 * Window's swapchain.
 ****************************/

/**
 * Image/Swapchain recreate flags.
 */
typedef enum GFXRecreateFlags_
{
	GFX_RECREATE_ = 0x0001, // Always set if other flags are set.
	GFX_REFORMAT_ = 0x0002,
	GFX_RESIZE_   = 0x0004,

	GFX_RECREATE_ALL_ = 0x0007

} GFXRecreateFlags_;


/**
 * Attempt to 'claim' (i.e. lock) the swapchain.
 * This is used to ensure no two objects try to use the swapchain.
 * @return Non-zero if swapchain was not yet claimed.
 */
static inline bool gfx_swapchain_try_lock_(GFXWindow_* window)
{
	return !atomic_exchange_explicit(&window->swap, 1, memory_order_acquire);
}

/**
 * Atomically 'unclaims' the swapchain.
 * Used to allow other objects to claim the swapchain again.
 */
static inline void gfx_swapchain_unlock_(GFXWindow_* window)
{
	atomic_store_explicit(&window->swap, 0, memory_order_release);
}

/**
 * Makes sure `window->frame.format` is set to the current device properties.
 * @param window Cannot be NULL.
 * @return Zero if no format was found.
 *
 * Not thread-affine, but also not thread-safe.
 */
bool gfx_swapchain_format_(GFXWindow_* window);

/**
 * Acquires the next available image from the swapchain of a window.
 * @param window    Cannot be NULL.
 * @param available Cannot be VK_NULL_HANDLE, semaphore to be signaled.
 * @param flags     Encodes how the swapchain has been recreated.
 * @return The index into window->frame.images, or UINT32_MAX if none available.
 *
 * Not thread-affine, but also not thread-safe.
 * Recreate flags are also set if resized to 0x0 and resources are destroyed.
 */
uint32_t gfx_swapchain_acquire_(GFXWindow_* window, VkSemaphore available,
                                GFXRecreateFlags_* flags);

/**
 * Submits presentation to a given queue for the swapchains of multiple windows.
 * gfx_swapchain_acquire_ must have returned succesfully before this call.
 * @param present  Must be a queue from the same Vulkan context as all windows.
 * @param rendered Cannot be VK_NULL_HANDLE, semaphore to wait on.
 * @param num      Number of input and output params, must be > 0.
 * @param windows  Must all share the same Vulkan context.
 * @param indices  Must be indices retrieved by gfx_swapchain_acquire_.
 * @param flags    Outputs how the swapchains have been recreated.
 *
 * Not thread-affine, but also not thread-safe.
 * Recreate flags are also set if resized to 0x0 and resources are destroyed.
 */
void gfx_swapchains_present_(GFXQueue_ present, VkSemaphore rendered,
                             size_t num,
                             GFXWindow_** windows, const uint32_t* indices,
                             GFXRecreateFlags_* flags);

/**
 * Destroys all retired swapchain images that are left behind when the
 * swapchain gets recreated on either acquisition or presentation.
 * @param window Cannot be NULL.
 *
 * Should be called after gfx_swapchain_(acquire|present)_ to free resources.
 * Not thread-affine, but also not thread-safe.
 */
void gfx_swapchain_purge_(GFXWindow_* window);


#endif
