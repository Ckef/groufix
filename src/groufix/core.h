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


/**
 * The Vulkan version used by groufix.
 */
#define GFX_VK_VERSION VK_API_VERSION_1_2


/**
 * Physical device definition.
 * No distinction between public and internal struct because
 * the public one does not expose any members.
 */
struct GFXDevice
{
	// Vulkan fields.
	struct
	{
		VkPhysicalDevice device;

	} vk;
};


/**
 * groufix global data, i.e. groufix state.
 */
typedef struct _GFXState
{
	int initialized;

	GFXVec devices;  // Stores GFXDevice (no user pointer, so not dynamic)
	GFXVec monitors; // Stores _GFXMonitor*
	GFXVec windows;  // Stores _GFXWindow*

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

		PFN_vkCreateInstance
			CreateInstance;
		PFN_vkDestroyInstance
			DestroyInstance;

		PFN_vkCreateDevice
			CreateDevice;
		PFN_vkEnumeratePhysicalDevices
			EnumeratePhysicalDevices;
		PFN_vkGetDeviceProcAddr
			GetDeviceProcAddr;
		PFN_vkGetPhysicalDeviceProperties
			GetPhysicalDeviceProperties;

	} vk;

} _GFXState;


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
 * Internal logical monitor definition.
 */
typedef struct _GFXMonitor
{
	GFXMonitor   base;
	GLFWmonitor* handle;

} _GFXMonitor;


/**
 * Internal logical window definition.
 */
typedef struct _GFXWindow
{
	GFXWindow   base;
	GLFWwindow* handle;

	// Vulkan fields.
	struct
	{
		VkSurfaceKHR surface;

	} vk;

} _GFXWindow;


/****************************
 * Global and local state.
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
 * Vulkan and its physical device state.
 ****************************/

/**
 * Logs a Vulkan result as a readable string.
 */
void _gfx_vulkan_log(VkResult result);

/**
 * Initializes Vulkan state, including all physical devices.
 * _groufix.vk.instance must be NULL.
 * Must be called by the same thread that called _gfx_state_init.
 * @return Non-zero on success.
 */
int _gfx_vulkan_init(void);

/**
 * Terminates Vulkan state.
 * This will make sure all physical devices will be destroyed.
 * Must be called by the same thread that called _gfx_state_init.
 */
void _gfx_vulkan_terminate(void);


/****************************
 * Monitor configuration.
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
 * This will make sure any monitors will be destroyed.
 * Must be called by the same thread that called _gfx_state_init.
 */
void _gfx_monitors_terminate(void);


#endif
