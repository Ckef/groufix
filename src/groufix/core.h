/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef _GFX_CORE_H
#define _GFX_CORE_H

#include "groufix/containers/io.h"
#include "groufix/containers/list.h"
#include "groufix/containers/vec.h"
#include "groufix/core/threads.h"
#include "groufix.h"

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"


// Least Vulkan version that must be supported.
#define _GFX_VK_API_VERSION VK_MAKE_API_VERSION(0,1,1,0)

// Vulkan function pointer.
#define _GFX_VK_PFN(pName) PFN_vk##pName pName

// Auto log the result of a call with return type VkResult.
#define _GFX_VK_CHECK(eval, action) \
	do { \
		VkResult _gfx_vk_result = eval; \
		if (_gfx_vk_result != VK_SUCCESS) { \
			gfx_log_error("Vulkan: %s", \
				_gfx_vulkan_result_string(_gfx_vk_result)); \
			action; \
		} \
	} while (0)

// User device handle (can be NULL) to internal handle, assigned to an lvalue.
#define _GFX_GET_DEVICE(lvalue, device) \
	do { \
		lvalue = (_GFXDevice*)(device == NULL ? \
			gfx_get_primary_device() : device); \
	} while (0)

// Ensures a Vulkan context exists for a device and assignes it to an lvalue.
#define _GFX_GET_CONTEXT(lvalue, device, action) \
	do { \
		lvalue = _gfx_device_init_context((_GFXDevice*)(device == NULL ? \
			gfx_get_primary_device() : device)); \
		if (lvalue == NULL) \
			action; \
	} while (0)

// Resolves a constrained input/output format and assigns it to another lvalue.
#define _GFX_RESOLVE_FORMAT(ioFmt, vkFmt, device, props, action) \
	do { \
		VkFormatProperties _gfx_vk_props = props; \
		vkFmt = _gfx_resolve_format(device, &(ioFmt), &_gfx_vk_props); \
		if (vkFmt == VK_FORMAT_UNDEFINED) \
			action; \
	} while (0)


/**
 * Thread local data.
 */
typedef struct _GFXThreadState
{
	uintmax_t id;


	// Logging data.
	struct
	{
		GFXLogLevel level;
		GFXBufWriter out; // `dest` is NULL if disabled.

	} log;

} _GFXThreadState;


/**
 * groufix global data, i.e. groufix state.
 */
typedef struct _GFXState
{
	atomic_bool initialized;

	// Only pre-initialized field besides `initialized`.
	GFXLogLevel logDef;

	GFXVec  devices;  // Stores _GFXDevice (never changes, so not dynamic).
	GFXList contexts; // References _GFXContext.
	GFXVec  monitors; // Stores _GFXMonitor* (pointers for access by index).

	_GFXMutex contextLock;

	// Monitor configuration change.
	void (*monitorEvent)(GFXMonitor*, bool);


	// Thread local data access.
	struct
	{
		_GFXThreadKey key; // Stores _GFXThreadState*.
		_GFXMutex     ioLock;

		// Next thread id.
		atomic_uintmax_t id;

	} thread;


	// Vulkan fields.
	struct
	{
		VkInstance instance;
#if !defined (NDEBUG)
		VkDebugUtilsMessengerEXT messenger;
#endif

		_GFX_VK_PFN(CreateInstance);
		_GFX_VK_PFN(EnumerateInstanceVersion);

#if !defined (NDEBUG)
		_GFX_VK_PFN(CreateDebugUtilsMessengerEXT);
		_GFX_VK_PFN(DestroyDebugUtilsMessengerEXT);
#endif
		_GFX_VK_PFN(CreateDevice);
		_GFX_VK_PFN(DestroyInstance);
		_GFX_VK_PFN(DestroySurfaceKHR);
#if defined (GFX_USE_VK_SUBSET_DEVICES)
		_GFX_VK_PFN(EnumerateDeviceExtensionProperties);
#endif
		_GFX_VK_PFN(EnumeratePhysicalDeviceGroups);
		_GFX_VK_PFN(EnumeratePhysicalDevices);
		_GFX_VK_PFN(GetDeviceProcAddr);
		_GFX_VK_PFN(GetPhysicalDeviceFeatures);
		_GFX_VK_PFN(GetPhysicalDeviceFeatures2);
		_GFX_VK_PFN(GetPhysicalDeviceFormatProperties);
		_GFX_VK_PFN(GetPhysicalDeviceMemoryProperties);
		_GFX_VK_PFN(GetPhysicalDeviceProperties);
		_GFX_VK_PFN(GetPhysicalDeviceProperties2);
		_GFX_VK_PFN(GetPhysicalDeviceQueueFamilyProperties);
		_GFX_VK_PFN(GetPhysicalDeviceSurfaceCapabilitiesKHR);
		_GFX_VK_PFN(GetPhysicalDeviceSurfaceFormatsKHR);
		_GFX_VK_PFN(GetPhysicalDeviceSurfacePresentModesKHR);
		_GFX_VK_PFN(GetPhysicalDeviceSurfaceSupportKHR);

	} vk;

} _GFXState;


/****************************
 * Vulkan context (superset of a logical device).
 ****************************/

/**
 * Logical (actually created) Vulkan queue family.
 */
typedef struct _GFXQueueSet
{
	GFXListNode list; // Base-type.

	VkQueueFlags flags;    // Only contains flags it was chosen for.
	VkQueueFlags allFlags; // All Vulkan queue capabilities.
	bool         present;  // Non-zero if chosen for presentation.
	uint32_t     family;   // Vulkan family index.

	size_t       count;
	_GFXMutex    locks[]; // Count mutexes, one for each queue.

} _GFXQueueSet;


/**
 * Logical Vulkan queue handle.
 */
typedef struct _GFXQueue
{
	uint32_t   family; // Vulkan family index.
	uint32_t   index;  // Vulkan queue index.
	_GFXMutex* lock;


	// Vulkan fields.
	struct
	{
		VkQueue queue;

	} vk;

} _GFXQueue;


/**
 * Logical Vulkan context.
 */
typedef struct _GFXContext
{
	GFXListNode list; // Base-type.
	GFXList     sets; // References _GFXQueueSet.


	// Supported feature flags.
	enum
	{
		_GFX_SUPPORT_GEOMETRY_SHADER     = 0x0001,
		_GFX_SUPPORT_TESSELLATION_SHADER = 0x0002

	} features;


	// Allocation limits (queried once).
	struct
	{
		// Memory limit.
		uint32_t  maxAllocs;
		_GFXMutex allocLock;

		atomic_uint_fast32_t allocs;

		// Sampler limit.
		uint32_t  maxSamplers;
		_GFXMutex samplerLock;

		atomic_uint_fast32_t samplers;

		// Allocated shaders.
		atomic_uintptr_t shaders;

	} limits;


	// Vulkan fields.
	struct
	{
		VkDevice device;

		_GFX_VK_PFN(AcquireNextImageKHR);
		_GFX_VK_PFN(AllocateCommandBuffers);
		_GFX_VK_PFN(AllocateDescriptorSets);
		_GFX_VK_PFN(AllocateMemory);
		_GFX_VK_PFN(BeginCommandBuffer);
		_GFX_VK_PFN(BindBufferMemory);
		_GFX_VK_PFN(BindImageMemory);
		_GFX_VK_PFN(CmdBeginRenderPass);
		_GFX_VK_PFN(CmdBindDescriptorSets);
		_GFX_VK_PFN(CmdBindIndexBuffer);
		_GFX_VK_PFN(CmdBindPipeline);
		_GFX_VK_PFN(CmdBindVertexBuffers);
		_GFX_VK_PFN(CmdBlitImage);
		_GFX_VK_PFN(CmdCopyBuffer);
		_GFX_VK_PFN(CmdCopyImage);
		_GFX_VK_PFN(CmdCopyBufferToImage);
		_GFX_VK_PFN(CmdCopyImageToBuffer);
		_GFX_VK_PFN(CmdDispatch);
		_GFX_VK_PFN(CmdDispatchBase);
		_GFX_VK_PFN(CmdDispatchIndirect);
		_GFX_VK_PFN(CmdDraw);
		_GFX_VK_PFN(CmdDrawIndexed);
		_GFX_VK_PFN(CmdDrawIndexedIndirect);
		_GFX_VK_PFN(CmdDrawIndirect);
		_GFX_VK_PFN(CmdEndRenderPass);
		_GFX_VK_PFN(CmdExecuteCommands);
		_GFX_VK_PFN(CmdPipelineBarrier);
		_GFX_VK_PFN(CmdPushConstants);
		_GFX_VK_PFN(CmdResolveImage);
		_GFX_VK_PFN(CmdSetViewport);
		_GFX_VK_PFN(CmdSetScissor);
		_GFX_VK_PFN(CreateBuffer);
		_GFX_VK_PFN(CreateBufferView);
		_GFX_VK_PFN(CreateCommandPool);
		_GFX_VK_PFN(CreateComputePipelines);
		_GFX_VK_PFN(CreateDescriptorPool);
		_GFX_VK_PFN(CreateDescriptorSetLayout);
		_GFX_VK_PFN(CreateDescriptorUpdateTemplate);
		_GFX_VK_PFN(CreateFence);
		_GFX_VK_PFN(CreateFramebuffer);
		_GFX_VK_PFN(CreateGraphicsPipelines);
		_GFX_VK_PFN(CreateImage);
		_GFX_VK_PFN(CreateImageView);
		_GFX_VK_PFN(CreatePipelineCache);
		_GFX_VK_PFN(CreatePipelineLayout);
		_GFX_VK_PFN(CreateRenderPass);
		_GFX_VK_PFN(CreateSampler);
		_GFX_VK_PFN(CreateSemaphore);
		_GFX_VK_PFN(CreateShaderModule);
		_GFX_VK_PFN(CreateSwapchainKHR);
		_GFX_VK_PFN(DestroyBuffer);
		_GFX_VK_PFN(DestroyBufferView);
		_GFX_VK_PFN(DestroyCommandPool);
		_GFX_VK_PFN(DestroyDescriptorPool);
		_GFX_VK_PFN(DestroyDescriptorSetLayout);
		_GFX_VK_PFN(DestroyDescriptorUpdateTemplate);
		_GFX_VK_PFN(DestroyDevice);
		_GFX_VK_PFN(DestroyFence);
		_GFX_VK_PFN(DestroyFramebuffer);
		_GFX_VK_PFN(DestroyImage);
		_GFX_VK_PFN(DestroyImageView);
		_GFX_VK_PFN(DestroyPipeline);
		_GFX_VK_PFN(DestroyPipelineCache);
		_GFX_VK_PFN(DestroyPipelineLayout);
		_GFX_VK_PFN(DestroyRenderPass);
		_GFX_VK_PFN(DestroySampler);
		_GFX_VK_PFN(DestroySemaphore);
		_GFX_VK_PFN(DestroyShaderModule);
		_GFX_VK_PFN(DestroySwapchainKHR);
		_GFX_VK_PFN(DeviceWaitIdle);
		_GFX_VK_PFN(EndCommandBuffer);
		_GFX_VK_PFN(FreeCommandBuffers);
		_GFX_VK_PFN(FreeMemory);
		_GFX_VK_PFN(GetBufferMemoryRequirements);
		_GFX_VK_PFN(GetBufferMemoryRequirements2);
		_GFX_VK_PFN(GetDeviceQueue);
		_GFX_VK_PFN(GetFenceStatus);
		_GFX_VK_PFN(GetImageMemoryRequirements);
		_GFX_VK_PFN(GetImageMemoryRequirements2);
		_GFX_VK_PFN(GetPipelineCacheData);
		_GFX_VK_PFN(GetSwapchainImagesKHR);
		_GFX_VK_PFN(MapMemory);
		_GFX_VK_PFN(MergePipelineCaches);
		_GFX_VK_PFN(QueuePresentKHR);
		_GFX_VK_PFN(QueueSubmit);
		_GFX_VK_PFN(ResetCommandPool);
		_GFX_VK_PFN(ResetDescriptorPool);
		_GFX_VK_PFN(ResetFences);
		_GFX_VK_PFN(UnmapMemory);
		_GFX_VK_PFN(UpdateDescriptorSetWithTemplate);
		_GFX_VK_PFN(WaitForFences);

	} vk;


	// Associated device group.
	size_t           numDevices;
	VkPhysicalDevice devices[];

} _GFXContext;


/****************************
 * User visible objects.
 ****************************/

/**
 * Internal physical device.
 */
typedef struct _GFXDevice
{
	GFXDevice    base;
	uint32_t     api; // Vulkan API version.
	char         name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];

#if defined (GFX_USE_VK_SUBSET_DEVICES)
	bool         subset; // If it is a non-conformant Vulkan implementation.
#endif

	_GFXContext* context;
	_GFXMutex    lock; // For initial context access.

	// Stores { GFXFormat, VkFormat, VkFormatProperties }.
	GFXVec formats;


	// Vulkan fields.
	struct
	{
		VkPhysicalDevice device;

	} vk;

} _GFXDevice;


/**
 * Internal monitor.
 */
typedef struct _GFXMonitor
{
	GFXMonitor   base;
	GLFWmonitor* handle;

	size_t       numModes;
	GFXVideoMode modes[]; // Available video modes.

} _GFXMonitor;


/**
 * Internal window.
 */
typedef struct _GFXWindow
{
	GFXWindow    base;
	GLFWwindow*  handle;

	_GFXDevice*  device; // Associated GPU to build a swapchain on.
	_GFXContext* context;
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
		_GFXMutex      lock;

	} frame;


	// Vulkan fields.
	struct
	{
		VkSurfaceKHR   surface;
		VkSwapchainKHR swapchain;
		VkSwapchainKHR oldSwapchain; // Must be VK_NULL_HANDLE if swapchain is not.
		GFXVec         retired;      // Stores VkSwapchainKHR.

	} vk;

} _GFXWindow;


/****************************
 * Global and thread local state.
 ****************************/

/**
 * The only instance of global groufix data.
 */
extern _GFXState _groufix;


/**
 * The default logger (to stderr) when no thread data is known.
 */
extern GFXBufWriter _gfx_io_buf_stderr;


/**
 * Initializes global groufix state.
 * _groufix.initialized must be 0, on success it will be set to 1.
 * @return Non-zero on success.
 *
 * This will initialize everything to an empty or non-active state.
 */
bool _gfx_init(void);

/**
 * Terminates global groufix state.
 * _groufix.initialized must be 1, after this call it will be set to 0.
 * Must be called by the same thread that called _gfx_state_init.
 */
void _gfx_terminate(void);

/**
 * Allocates thread local state for the calling thread.
 * _groufix.initialized must be 1.
 * May not be called when data is already allocated on the calling thread.
 * @return Non-zero on success.
 *
 * This will initialize everything to an empty or non-active state.
 */
bool _gfx_create_local(void);

/**
 * Frees thread local state of the calling thread.
 * _groufix.initialized must be 1.
 * May not be called when no data is allocated on the calling thread.
 * All threads with local data need to call this before _gfx_state_terminate.
 */
void _gfx_destroy_local(void);

/**
 * Retrieves thread local state of the calling thread.
 * _groufix.initialized must be 1.
 * @return NULL if no state was allocated.
 */
_GFXThreadState* _gfx_get_local(void);


/****************************
 * Devices, monitors and Vulkan contexts.
 ****************************/

/**
 * Retrieves a VkResult as a readable string.
 */
const char* _gfx_vulkan_result_string(VkResult result);

/**
 * Initializes Vulkan state.
 * _groufix.vk.instance must be NULL.
 * Must be called by the same thread that called _gfx_state_init.
 * @return Non-zero on success.
 */
bool _gfx_vulkan_init(void);

/**
 * Terminates Vulkan state.
 * Must be called before _gfx_state_terminate, on the same thread.
 */
void _gfx_vulkan_terminate(void);

/**
 * Initializes internal physical device (e.g. GPU) configuration.
 * _groufix.devices.size must be 0.
 * Must be called by the same thread that called _gfx_vulkan_init.
 * @return Non-zero on success.
 */
bool _gfx_devices_init(void);

/**
 * Terminates internal device configuration.
 * This will make sure all divices AND contexts are destroyed.
 * Must be called before _gfx_vulkan_terminate, on the same thread.
 */
void _gfx_devices_terminate(void);

/**
 * Initializes internal monitor configuration.
 * _groufix.monitors.size must be 0.
 * Must be called by the same thread that called _gfx_state_init.
 * @return Non-zero on success.
 */
bool _gfx_monitors_init(void);

/**
 * Terminates internal monitor configuration.
 * This will make sure all monitors are destroyed.
 * Must be called before _gfx_state_terminate, on the same thread.
 */
void _gfx_monitors_terminate(void);

/**
 * Initializes the groufix/Vulkan format 'dictionary',
 * i.e. initializes and fills the `formats` member of the device.
 * @param device Cannot be NULL.
 * @return Non-zero on success.
 */
bool _gfx_device_init_formats(_GFXDevice* device);

/**
 * Resolves a (potentially 'fuzzy') format to a supported Vulkan format.
 * The returned format will at least support all given format properties.
 * @param device Cannot be NULL.
 * @param format Input/output format, outputs the groufix equivalent to Vulkan.
 * @param props  Must-have format properties, may be NULL.
 * @return VK_FORMAT_UNDEFINED if not supported.
 */
VkFormat _gfx_resolve_format(_GFXDevice* device,
                             GFXFormat* fmt, const VkFormatProperties* props);

/**
 * Parses a Vulkan format, returning a supported groufix format.
 * @param device Cannot be NULL.
 * @param fmt    Vulkan format, can be any.
 * @return GFX_FORMAT_EMPTY if not supported.
 */
GFXFormat _gfx_parse_format(_GFXDevice* device, VkFormat fmt);

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
_GFXContext* _gfx_device_init_context(_GFXDevice* device);

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
_GFXQueueSet* _gfx_pick_family(_GFXContext* context, uint32_t* family,
                               VkQueueFlags flags, bool present);

/**
 * Picks a queue from the queue set supporting the given abilities.
 * @param queue Outputs the handle to the picked queue, cannot be NULL.
 * @see _gfx_pick_family.
 */
_GFXQueueSet* _gfx_pick_queue(_GFXContext* context, _GFXQueue* queue,
                              VkQueueFlags flags, bool present);

/**
 * Retrieves the index of a queue supporting the given abilities.
 * Useful when no queue is used, but identification is necessary.
 * @param set Must support flags and present.
 * @see _gfx_pick_queue.
 */
uint32_t _gfx_queue_index(_GFXQueueSet* set,
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
uint32_t _gfx_filter_families(GFXMemoryFlags flags, uint32_t* families);


/****************************
 * Window's swapchain.
 ****************************/

/**
 * Image/Swapchain recreate flags.
 */
typedef enum _GFXRecreateFlags
{
	_GFX_RECREATE = 0x0001, // Always set if other flags are set.
	_GFX_REFORMAT = 0x0002,
	_GFX_RESIZE   = 0x0004,

	_GFX_RECREATE_ALL = 0x0007

} _GFXRecreateFlags;


/**
 * Attempt to 'claim' (i.e. lock) the swapchain.
 * This is used to ensure no two objects try to use the swapchain.
 * @return Non-zero if swapchain was not yet claimed.
 */
static inline bool _gfx_swapchain_try_lock(_GFXWindow* window)
{
	return !atomic_exchange_explicit(&window->swap, 1, memory_order_acquire);
}

/**
 * Atomically 'unclaims' the swapchain.
 * Used to allow other objects to claim the swapchain again.
 */
static inline void _gfx_swapchain_unlock(_GFXWindow* window)
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
bool _gfx_swapchain_format(_GFXWindow* window);

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
uint32_t _gfx_swapchain_acquire(_GFXWindow* window, VkSemaphore available,
                                _GFXRecreateFlags* flags);

/**
 * Submits presentation to a given queue for the swapchains of multiple windows.
 * _gfx_swapchain_acquire must have returned succesfully before this call.
 * @param present  Must be a queue from the same Vulkan context as all windows.
 * @param rendered Cannot be VK_NULL_HANDLE, semaphore to wait on.
 * @param num      Number of input and output params, must be > 0.
 * @param windows  Must all share the same Vulkan context.
 * @param indices  Must be indices retrieved by _gfx_swapchain_acquire.
 * @param flags    Outputs how the swapchains have been recreated.
 *
 * Not thread-affine, but also not thread-safe.
 * Recreate flags are also set if resized to 0x0 and resources are destroyed.
 */
void _gfx_swapchains_present(_GFXQueue present, VkSemaphore rendered,
                             size_t num,
                             _GFXWindow** windows, const uint32_t* indices,
                             _GFXRecreateFlags* flags);

/**
 * Destroys all retired swapchain images that are left behind when the
 * swapchain gets recreated on either acquisition or presentation.
 * @param window Cannot be NULL.
 *
 * Should be called after _gfx_swapchain_(acquire|present) to free resources.
 * Not thread-affine, but also not thread-safe.
 */
void _gfx_swapchain_purge(_GFXWindow* window);


#endif
