/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef _GFX_CORE_H
#define _GFX_CORE_H

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
	} \
	while (0)


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

	GFXVec devices;  // Stores _GFXDevice (never changes, so not dynamic)
	GFXVec contexts; // Stores _GFXContext*
	GFXVec monitors; // Stores _GFXMonitor*

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
		_GFXThreadKey key; // Stores _GFXThreadState*
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
	uint32_t     family;  // Vulkan family index.
	VkQueueFlags flags;
	int          present; // Non-zero if chosen for presentation.

	uint32_t     count;
	_GFXMutex    mutexes[]; // Count mutexes, one for each queue.

} _GFXQueueSet;


/**
 * Logical Vulkan context.
 */
typedef struct _GFXContext
{
	// Vulkan fields.
	struct
	{
		VkDevice device;

		_GFX_VK_PFN(AcquireNextImageKHR);
		_GFX_VK_PFN(AllocateCommandBuffers);
		_GFX_VK_PFN(BeginCommandBuffer);
		_GFX_VK_PFN(CmdClearColorImage);
		_GFX_VK_PFN(CmdPipelineBarrier);
		_GFX_VK_PFN(CreateCommandPool);
		_GFX_VK_PFN(CreateFence);
		_GFX_VK_PFN(CreateSemaphore);
		_GFX_VK_PFN(CreateSwapchainKHR);
		_GFX_VK_PFN(DestroyCommandPool);
		_GFX_VK_PFN(DestroyDevice);
		_GFX_VK_PFN(DestroyFence);
		_GFX_VK_PFN(DestroySemaphore);
		_GFX_VK_PFN(DestroySwapchainKHR);
		_GFX_VK_PFN(DeviceWaitIdle);
		_GFX_VK_PFN(EndCommandBuffer);
		_GFX_VK_PFN(FreeCommandBuffers);
		_GFX_VK_PFN(GetDeviceQueue);
		_GFX_VK_PFN(GetSwapchainImagesKHR);
		_GFX_VK_PFN(QueuePresentKHR);
		_GFX_VK_PFN(QueueSubmit);
		_GFX_VK_PFN(QueueWaitIdle);
		_GFX_VK_PFN(ResetCommandPool);
		_GFX_VK_PFN(ResetFences);
		_GFX_VK_PFN(WaitForFences);

	} vk;


	GFXVec sets; // Stores _GFXQueueSet*

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
 * Internal logical monitor.
 */
typedef struct _GFXMonitor
{
	GFXMonitor   base;
	GLFWmonitor* handle;

	size_t       numModes;
	GFXVideoMode modes[]; // Available video modes.

} _GFXMonitor;


/**
 * Internal logical window.
 */
typedef struct _GFXWindow
{
	GFXWindow    base;
	GLFWwindow*  handle;

	_GFXDevice*  device; // Associated GPU to build a swapchain on.
	_GFXContext* context;


	// Chosen presentation queue.
	struct
	{
		uint32_t   family;
		VkQueue    queue; // Queue chosen from the family.
		_GFXMutex* mutex;

		GFXVec access; // Stores uint32_t, all Vulkan family indices with image access.

	} present;


	// Frame (i.e Vulkan surface + swapchain) properties.
	struct
	{
		GFXVec images; // Stores VkImage.

#if defined (__STDC_NO_ATOMICS__)
		int            resized;
#else
		atomic_int     resized;
#endif
		size_t         width;
		size_t         height;
		GFXWindowFlags flags; // Determines number of images.
		_GFXMutex      lock;

	} frame;


	// Vulkan fields.
	struct
	{
		VkSurfaceKHR   surface;
		VkSwapchainKHR swapchain;

		VkSemaphore    available; // Image available, to be waited for.
		VkSemaphore    rendered;  // Presentation ready, to be signaled.
		VkFence        fence;

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
int _gfx_state_init(void);

/**
 * Terminates global groufix state.
 * _groufix.initialized must be 1, after this call it will be set to 0.
 * Must be called by the same thread that called _gfx_state_init.
 */
void _gfx_state_terminate(void);

/**
 * Allocates thread local state for the calling thread.
 * _groufix.initialized must be 1.
 * May not be called when data is already allocated on the calling thread.
 * @return Non-zero on success.
 *
 * This will initialize everything to an empty or non-active state.
 */
int _gfx_state_create_local(void);

/**
 * Frees thread local state of the calling thread.
 * _groufix.initialized must be 1.
 * May not be called when no data is allocated on the calling thread.
 * All threads with local data need to call this before _gfx_state_terminate.
 */
void _gfx_state_destroy_local(void);

/**
 * Retrieves thread local state of the calling thread.
 * _groufix.initialized must be 1.
 * @return NULL if no state was allocated.
 */
_GFXThreadState* _gfx_state_get_local(void);


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


/****************************
 * The window's swapchain.
 ****************************/

/**
 * (Re)creates the swapchain of a window, left empty at framebuffer size of 0x0.
 * @param window Cannot be NULL.
 * @return Non-zero on success.
 *
 * Can be called from any thread, but not reentrant.
 * This will destroy the old swapchain, references to it must be released.
 * Also fills window->frame.images.
 */
int _gfx_swapchain_recreate(_GFXWindow* window);

/**
 * Acquires the next available image from the swapchain of a window.
 * window->vk.swapchain cannot be VK_NULL_HANDLE.
 * @param window   Cannot be NULL.
 * @param index    Cannot be NULL, index into window->frame.images.
 * @param recreate Cannot be NULL, non-zero if swapchain has been recreated.
 * @return Non-zero on success.
 *
 * Can be called from any thread, but not reentrant.
 * This will wait until the previous image is acquired.
 * This will signal window->vk.available when the current image is acquired.
 * _gfx_swapchain_recreate is called when necessary.
 */
int _gfx_swapchain_acquire(_GFXWindow* window, uint32_t* index, int* recreate);

/**
 * Submits a present command for the swapchain of a window.
 * window->vk.swapchain cannot be VK_NULL_HANDLE.
 * window->vk.rendered must be signaled or pending.
 * @param window   Cannot be NULL.
 * @param index    Must be an index retrieved by _gfx_swapchain_acquire.
 * @param recreate Cannot be NULL, non-zero if swapchain has been recreated.
 * @return Non-zero on success.
 *
 * Can be called from any thread, but not reentrant.
 * _gfx_swapchain_recreate is called when necessary.
 */
int _gfx_swapchain_present(_GFXWindow* window, uint32_t index, int* recreate);


#endif
