/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <assert.h>


/****************************
 * Callback for GLFW errors.
 */
static void _gfx_glfw_error(int error, const char* description)
{
	// Just log it as a groufix error,
	// this should already take care of threading.
	gfx_log_error("GLFW: %s", description);
}

/****************************/
GFX_API bool gfx_init(void)
{
	// Already initialized, just do nothing.
	if (atomic_load(&_groufix.initialized))
		return 1;

	// Initialize global state.
	if (!_gfx_init())
	{
		gfx_log_fatal("Could not initialize global state.");
		return 0;
	}

	gfx_log_info("Global state initialized succesfully.");

	// Ok so now we want to attach this thread as 'main' thread.
	// If this fails, undo everything...
	if (!gfx_attach())
		goto terminate;

	// Init GLFW and the Vulkan loader.
	glfwSetErrorCallback(_gfx_glfw_error);

	if (!glfwInit())
		goto terminate;

	if (!glfwVulkanSupported())
		goto terminate;

	gfx_log_info("GLFW initialized succesfully, Vulkan loader found.");

	// Initialize all other internal state.
	if (!_gfx_vulkan_init())
		goto terminate;

	if (!_gfx_devices_init())
		goto terminate;

	if (!_gfx_monitors_init())
		goto terminate;

	gfx_log_info("All internal state initialized succesfully, ready.");

	return 1;


	// Cleanup on failure.
terminate:
	gfx_log_fatal("Could not initialize the engine.");
	gfx_terminate();

	return 0;
}

/****************************/
GFX_API void gfx_terminate(void)
{
	// Not yet initialized, just do nothing.
	if (!atomic_load(&_groufix.initialized))
		return;

	// Terminate the contents of the engine.
	_gfx_monitors_terminate();
	_gfx_devices_terminate();
	_gfx_vulkan_terminate();
	glfwTerminate();

	// Detach and terminate.
	gfx_detach();
	_gfx_terminate();

	gfx_log_info("All internal state terminated.");
}

/****************************/
GFX_API bool gfx_attach(void)
{
	// Not yet initialized, cannot attach.
	if (!atomic_load(&_groufix.initialized))
		return 0;

	// Already attached.
	if (_gfx_get_local())
		return 1;

	// Create thread local state.
	if (!_gfx_create_local())
	{
		gfx_log_error("Could not attach a thread.");
		return 0;
	}

	// Every thread identifies itself.
	gfx_log_info("Attached self to groufix.");

	return 1;
}

/****************************/
GFX_API void gfx_detach(void)
{
	// Not yet initialized or attached.
	if (!atomic_load(&_groufix.initialized) || !_gfx_get_local())
		return;

	// Every thread may have one last say :)
	gfx_log_info("Detaching self from groufix.");

	_gfx_destroy_local();
}

/****************************/
GFX_API void gfx_poll_events(void)
{
	assert(atomic_load(&_groufix.initialized));

	glfwPollEvents();
}

/****************************/
GFX_API void gfx_wait_events(void)
{
	assert(atomic_load(&_groufix.initialized));

	glfwWaitEvents();
}

/****************************/
GFX_API void gfx_wake(void)
{
	assert(atomic_load(&_groufix.initialized));

	glfwPostEmptyEvent();
}
