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

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>


/**
 * groufix global data, i.e. groufix state.
 */
typedef struct _GFXState
{
	int initialized;

	GFXVec monitors; // Stores _GFXMonitor*
	GFXVec windows;  // Stores _GFXWindow*

	// Monitor configuration change.
	void (*monitorEvent)(GFXMonitor*, int);

	// Vulkan fields.
	struct
	{
		VkInstance instance;

	} vk;

	// Thread local data access.
	struct
	{
#if defined (__STDC_NO_ATOMICS__)
		unsigned int  id;
		_GFXMutex     idLock;
#else
		atomic_uint   id;
#endif
		_GFXThreadKey key;
		_GFXMutex     ioLock;

	} thread;

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
 * Monitor configuration.
 ****************************/

/**
 * Initializes internal monitor configuration.
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
