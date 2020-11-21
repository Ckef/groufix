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
#include "groufix/core/device.h"
#include "groufix/core/log.h"
#include "groufix/core/window.h"
#include "groufix/core/threads.h"
#include <stdio.h>

#if !defined (__STDC_NO_ATOMICS__)
	#include <stdatomic.h>
#endif

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// Vulkan function pointer.
#define _GFX_PFN_VK(pName) PFN_vk##pName pName


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

		_GFX_PFN_VK(CreateInstance);
		_GFX_PFN_VK(EnumerateInstanceVersion);

#if !defined (NDEBUG)
		_GFX_PFN_VK(CreateDebugUtilsMessengerEXT);
		_GFX_PFN_VK(DestroyDebugUtilsMessengerEXT);
#endif
		_GFX_PFN_VK(CreateDevice);
		_GFX_PFN_VK(DestroyInstance);
		_GFX_PFN_VK(DestroySurfaceKHR);
		_GFX_PFN_VK(EnumeratePhysicalDeviceGroups);
		_GFX_PFN_VK(EnumeratePhysicalDevices);
		_GFX_PFN_VK(GetDeviceProcAddr);
		_GFX_PFN_VK(GetPhysicalDeviceProperties);
		_GFX_PFN_VK(GetPhysicalDeviceQueueFamilyProperties);
		_GFX_PFN_VK(GetPhysicalDeviceSurfaceCapabilitiesKHR);
		_GFX_PFN_VK(GetPhysicalDeviceSurfaceFormatsKHR);
		_GFX_PFN_VK(GetPhysicalDeviceSurfacePresentModesKHR);
		_GFX_PFN_VK(GetPhysicalDeviceSurfaceSupportKHR);

	} vk;

} _GFXState;


/**
 * Logical Vulkan queue family.
 */
typedef struct _GFXQueueFamily
{
	VkQueueFlags flags;
	int          present; // Non-zero if presentation support.
	uint32_t     index;   // Vulkan family index.
	uint32_t     count;

} _GFXQueueFamily;


/**
 * Logical Vulkan context (superset of a device).
 */
typedef struct _GFXContext
{
	// Vulkan fields.
	struct
	{
		VkDevice device;

		_GFX_PFN_VK(AcquireNextImageKHR);
		_GFX_PFN_VK(CreateSwapchainKHR);
		_GFX_PFN_VK(DestroyDevice);
		_GFX_PFN_VK(DestroySwapchainKHR);
		_GFX_PFN_VK(DeviceWaitIdle);
		_GFX_PFN_VK(GetSwapchainImagesKHR);
		_GFX_PFN_VK(QueuePresentKHR);

	} vk;

	// Created queue families.
	size_t            numFamilies;
	_GFXQueueFamily*  families;

	// Associated device group.
	size_t            numDevices;
	VkPhysicalDevice* devices;

} _GFXContext;


/****************************
 * User visible objects.
 ****************************/

/**
 * Internal physical device definition.
 */
typedef struct _GFXDevice
{
	GFXDevice    base;
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
 * Internal logical monitor definition.
 */
typedef struct _GFXMonitor
{
	GFXMonitor    base;
	GLFWmonitor*  handle;

	// Available video modes.
	size_t        numModes;
	GFXVideoMode* modes;

} _GFXMonitor;


/**
 * Internal logical window definition.
 */
typedef struct _GFXWindow
{
	GFXWindow   base;
	GLFWwindow* handle;
	_GFXDevice* device; // Associated GPU to build a swapchain on.

	// Vulkan fields.
	struct
	{
		VkSurfaceKHR   surface;
		VkSwapchainKHR swapchain;

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
 * Vulkan context, devices and logging.
 ****************************/

/**
 * Logs a Vulkan result as a readable string.
 */
void _gfx_vulkan_log(VkResult result);

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
 * This will make sure all divices and contexts are destroyed.
 * Must be called before _gfx_vulkan_terminate, on the same thread.
 */
void _gfx_devices_terminate(void);

/**
 * Retrieves the Vulkan context.
 * It will automatically be created if it did not exist yet.
 * The device will share its context with all devices in its device group.
 * @param device Cannot be NULL.
 * @return NULL if the context could not be found or created.
 *
 * This function will lock the device and lock during context creation and
 * can therefore be called on any thread.
 * Once this function returned succesfully at least once for a given device,
 * we can read device->index and device->context directly.
 */
_GFXContext* _gfx_device_get_context(_GFXDevice* device);


/****************************
 * Monitor configuration and window swapchain.
 ****************************/

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
 * (Re)creates the swapchain of a window.
 * @param window Cannot be NULL.
 * @return Non-zero on success.
 */
int _gfx_swapchain_recreate(_GFXWindow* window);


#endif
