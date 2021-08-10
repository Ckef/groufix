/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef _GFX_CORE_H
#define _GFX_CORE_H

#include "groufix/containers/list.h"
#include "groufix/containers/vec.h"
#include "groufix/core/threads.h"
#include "groufix.h"
#include <stdio.h>

#if !defined (__STDC_NO_ATOMICS__)
	#include <stdatomic.h>
#endif

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>


// Least Vulkan version that must be supported.
#define _GFX_VK_VERSION VK_MAKE_VERSION(1,1,0)

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


/**
 * Thread local data.
 */
typedef struct _GFXThreadState
{
	unsigned int id;


	// Logging data.
	struct
	{
		GFXLogLevel level;
		int         std;
		FILE*       file;

	} log;

} _GFXThreadState;


/**
 * groufix global data, i.e. groufix state.
 */
typedef struct _GFXState
{
	int initialized;

	GFXLogLevel logDef; // Only pre-initialized field besides `initialized`.

	GFXVec  devices;  // Stores _GFXDevice (never changes, so not dynamic).
	GFXList contexts; // References _GFXContext.
	GFXVec  monitors; // Stores _GFXMonitor* (pointers for access by index).

	_GFXMutex contextLock;

	// Monitor configuration change.
	void (*monitorEvent)(GFXMonitor*, int);


	// Thread local data access.
	struct
	{
#if defined (__STDC_NO_ATOMICS__)
		unsigned int  id;
		_GFXMutex     idLock;
#else
		atomic_uint   id;
#endif
		_GFXThreadKey key; // Stores _GFXThreadState*.
		_GFXMutex     ioLock;

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
		_GFX_VK_PFN(EnumeratePhysicalDeviceGroups);
		_GFX_VK_PFN(EnumeratePhysicalDevices);
		_GFX_VK_PFN(GetDeviceProcAddr);
		_GFX_VK_PFN(GetPhysicalDeviceFeatures);
		_GFX_VK_PFN(GetPhysicalDeviceMemoryProperties);
		_GFX_VK_PFN(GetPhysicalDeviceProperties);
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

	uint32_t     family;  // Vulkan family index.
	VkQueueFlags flags;   // Only contains flags it was chosen for.
	int          present; // Non-zero if chosen for presentation.

	size_t       count;
	_GFXMutex    locks[]; // Count mutexes, one for each queue.

} _GFXQueueSet;


/**
 * Logical Vulkan queue handle.
 */
typedef struct _GFXQueue
{
	uint32_t   family; // Vulkan family index.
	VkQueue    queue;
	_GFXMutex* lock;

} _GFXQueue;


/**
 * Logical Vulkan context.
 */
typedef struct _GFXContext
{
	GFXListNode list; // Base-type.
	GFXList     sets; // References _GFXQueueSet.


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
		_GFX_VK_PFN(CmdBeginRenderPass);
		_GFX_VK_PFN(CmdBindDescriptorSets);
		_GFX_VK_PFN(CmdBindIndexBuffer);
		_GFX_VK_PFN(CmdBindPipeline);
		_GFX_VK_PFN(CmdBindVertexBuffers);
		_GFX_VK_PFN(CmdDraw);
		_GFX_VK_PFN(CmdDrawIndexed);
		_GFX_VK_PFN(CmdEndRenderPass);
		_GFX_VK_PFN(CreateBuffer);
		_GFX_VK_PFN(CreateCommandPool);
		_GFX_VK_PFN(CreateDescriptorPool);
		_GFX_VK_PFN(CreateDescriptorSetLayout);
		_GFX_VK_PFN(CreateFence);
		_GFX_VK_PFN(CreateFramebuffer);
		_GFX_VK_PFN(CreateGraphicsPipelines);
		_GFX_VK_PFN(CreateImageView);
		_GFX_VK_PFN(CreatePipelineLayout);
		_GFX_VK_PFN(CreateRenderPass);
		_GFX_VK_PFN(CreateSemaphore);
		_GFX_VK_PFN(CreateShaderModule);
		_GFX_VK_PFN(CreateSwapchainKHR);
		_GFX_VK_PFN(DestroyBuffer);
		_GFX_VK_PFN(DestroyCommandPool);
		_GFX_VK_PFN(DestroyDescriptorPool);
		_GFX_VK_PFN(DestroyDescriptorSetLayout);
		_GFX_VK_PFN(DestroyDevice);
		_GFX_VK_PFN(DestroyFence);
		_GFX_VK_PFN(DestroyFramebuffer);
		_GFX_VK_PFN(DestroyImageView);
		_GFX_VK_PFN(DestroyPipeline);
		_GFX_VK_PFN(DestroyPipelineLayout);
		_GFX_VK_PFN(DestroyRenderPass);
		_GFX_VK_PFN(DestroySemaphore);
		_GFX_VK_PFN(DestroyShaderModule);
		_GFX_VK_PFN(DestroySwapchainKHR);
		_GFX_VK_PFN(DeviceWaitIdle);
		_GFX_VK_PFN(EndCommandBuffer);
		_GFX_VK_PFN(FreeCommandBuffers);
		_GFX_VK_PFN(FreeMemory);
		_GFX_VK_PFN(GetBufferMemoryRequirements);
		_GFX_VK_PFN(GetDeviceQueue);
		_GFX_VK_PFN(GetSwapchainImagesKHR);
		_GFX_VK_PFN(MapMemory);
		_GFX_VK_PFN(QueuePresentKHR);
		_GFX_VK_PFN(QueueSubmit);
		_GFX_VK_PFN(ResetFences);
		_GFX_VK_PFN(UnmapMemory);
		_GFX_VK_PFN(UpdateDescriptorSets);
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

	size_t       index; // Index into the device group.
	_GFXContext* context;
	_GFXMutex    lock;


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
	GFXVec       access; // Stores uint32_t, all Vulkan family indicies with image access.

	// Swapchain lock (window can only be used by one renderer).
#if defined (__STDC_NO_ATOMICS__)
	int        swap;
	_GFXMutex  swapLock;
#else
	atomic_int swap;
#endif


	// Frame (i.e Vulkan surface + swapchain) properties.
	struct
	{
		GFXVec   images; // Stores VkImage, only those of vk.swapchain.
		VkFormat format;
		size_t   width;
		size_t   height;

		// All new 'recreate' values are protected by a mutex.
#if defined (__STDC_NO_ATOMICS__)
		int            recreate;
#else
		atomic_int     recreate;
#endif
		size_t         rWidth;  // Future width.
		size_t         rHeight; // Future height.
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
 * Initializes global groufix state.
 * _groufix.initialized must be 0, on success it will be set to 1.
 * @return Non-zero on success.
 *
 * This will initialize everything to an empty or non-active state.
 */
int _gfx_init(void);

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
int _gfx_create_local(void);

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
int _gfx_vulkan_init(void);

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
int _gfx_devices_init(void);

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
int _gfx_monitors_init(void);

/**
 * Terminates internal monitor configuration.
 * This will make sure all monitors are destroyed.
 * Must be called before _gfx_state_terminate, on the same thread.
 */
void _gfx_monitors_terminate(void);

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
 * Picks the queue set supporting the given flag and presentation support.
 * @param context Cannot be NULL.
 * @return NULL if not found.
 */
_GFXQueueSet* _gfx_pick_queue_set(_GFXContext* context,
                                  VkQueueFlags flags, int present);

/**
 * Retrieves a handle to a queue from a queue set.
 * @param context Cannot be NULL.
 * @param set     Cannot be NULL, must be picked from context.
 * @index Index of the queue, must be < set->count.
 */
_GFXQueue _gfx_get_queue(_GFXContext* context,
                         _GFXQueueSet* set, size_t index);


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
 * Attempt to 'claim' (i.e. lock) the swapchain by atomically reading if
 * window->swap is already set to one and subsequentally setting it to 1.
 * This is used to ensure no two objects try to use the swapchain.
 * @param window Cannot be NULL.
 * @return Non-zero if swapchain was not yet claimed.
 */
int _gfx_swapchain_try_lock(_GFXWindow* window);

/**
 * Atomically 'unclaims' the swapchain by setting window->swap back to 0.
 * @param window Cannot be NULL.
 */
void _gfx_swapchain_unlock(_GFXWindow* window);

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
