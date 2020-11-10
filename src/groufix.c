/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix.h"
#include "groufix/core.h"


/****************************
 * Callback for GLFW errors.
 */
static void _gfx_glfw_error(int error, const char* description)
{
	// Just log it as a groufix error,
	// this should already take care of threading.
	gfx_log_error(description);
}

/****************************/
GFX_API int gfx_init(void)
{
	// Already initialized, just do nothing.
	if (_groufix.initialized)
		return 1;

	// Initialize global state.
	if (!_gfx_state_init())
		goto terminate;

	gfx_log_info("Global state initialized succesfully.");

	// Ok so now we want to attach this thread as 'main' thread.
	// If this fails, undo everything...
	if (!gfx_attach())
		goto terminate;

	// For ONLY the main thread: default to logging to stdout.
	// After logging is setup, init GLFW and the Vulkan loader.
	gfx_log_set_out(1);
	glfwSetErrorCallback(_gfx_glfw_error);

	if (!glfwInit())
		goto terminate;

	if (!glfwVulkanSupported())
		goto terminate;

	gfx_log_info("GLFW initialized succesfully, Vulkan loader found.");

	// Initialize all other internal state.
	// Vulkan initialization includes devices.
	if (!_gfx_vulkan_init())
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
	if (!_groufix.initialized)
		return;

	// Terminate the contents of the engine.
	_gfx_monitors_terminate();
	_gfx_vulkan_terminate();
	glfwTerminate();

	// Detach and terminate.
	gfx_detach();
	_gfx_state_terminate();

	gfx_log_info("All internal state terminated.");
}

/****************************/
GFX_API int gfx_attach(void)
{
	// Not yet initialized, cannot attach.
	if (!_groufix.initialized)
		return 0;

	// Already attached.
	if (_gfx_state_get_local())
		return 1;

	// Create thread local state.
	if (!_gfx_state_create_local())
	{
		gfx_log_error("Could not attach a thread.");
		return 0;
	}

	return 1;
}

/****************************/
GFX_API void gfx_detach(void)
{
	// Not yet initialized or attached.
	if (!_groufix.initialized || !_gfx_state_get_local())
		return;

	_gfx_state_destroy_local();
}
