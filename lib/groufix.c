/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"


/****************************
 * Callback for GLFW errors.
 */
static void gfx_glfw_error_(int error, const char* description)
{
	// Just log it as a groufix error,
	// this should already take care of threading.
	gfx_log_error("GLFW: %s", description);
}

/****************************/
GFX_API bool gfx_init(void)
{
	// Already initialized, just do nothing.
	if (atomic_load(&groufix_.initialized))
		return 1;

	// Before anything, get the log level from env.
	gfx_log_set_default_level_();

	// Initialize global state.
	if (!gfx_init_())
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
	glfwSetErrorCallback(gfx_glfw_error_);

	if (!glfwInit())
		goto terminate;

	if (!glfwVulkanSupported())
		goto terminate;

	gfx_log_info("GLFW initialized succesfully, Vulkan loader found.");

	// Initialize all other internal state.
	if (!gfx_vulkan_init_())
		goto terminate;

	if (!gfx_devices_init_())
		goto terminate;

	if (!gfx_monitors_init_())
		goto terminate;

	if (!gfx_gamepads_init_())
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
	if (!atomic_load(&groufix_.initialized))
		return;

	// Terminate the contents of the engine.
	gfx_gamepads_terminate_();
	gfx_monitors_terminate_();
	gfx_devices_terminate_();
	gfx_vulkan_terminate_();
	glfwTerminate();

	// Detach and terminate.
	gfx_detach();
	gfx_terminate_();

	gfx_log_info("All internal state terminated.");
}

/****************************/
GFX_API bool gfx_attach(void)
{
	// Not yet initialized, cannot attach.
	if (!atomic_load(&groufix_.initialized))
		return 0;

	// Already attached.
	if (gfx_get_local_())
		return 1;

	// Create thread local state.
	if (!gfx_create_local_())
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
	if (!atomic_load(&groufix_.initialized) || !gfx_get_local_())
		return;

	// Every thread may have one last say :)
	gfx_log_info("Detaching self from groufix.");

	gfx_destroy_local_();
}

/****************************/
GFX_API int64_t gfx_time(void)
{
	assert(atomic_load(&groufix_.initialized));

	return gfx_clock_get_time_(&groufix_.clock);
}

/****************************/
GFX_API int64_t gfx_time_frequency(void)
{
	assert(atomic_load(&groufix_.initialized));

	return groufix_.clock.frequency;
}

/****************************/
GFX_API void gfx_poll_events(void)
{
	assert(atomic_load(&groufix_.initialized));

	glfwPollEvents();
}

/****************************/
GFX_API void gfx_wait_events(void)
{
	assert(atomic_load(&groufix_.initialized));

	glfwWaitEvents();
}

/****************************/
GFX_API void gfx_wake(void)
{
	assert(atomic_load(&groufix_.initialized));

	glfwPostEmptyEvent();
}
