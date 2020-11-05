/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix.h"
#include "groufix/core.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>


/****************************/
static inline void _gfx_glfw_error(int error, const char* description)
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
		return 0;

	// Ok so now we want to attach this thread as 'main' thread.
	// If this fails, undo everything...
	if (!gfx_attach())
		goto terminate;

	// For ONLY the main thread: default to logging to stdout.
	// After logging is setup, init contents of the engine.
	gfx_log_set_out(1);
	glfwSetErrorCallback(_gfx_glfw_error);

	if (!glfwInit())
		goto terminate;

	if (!glfwVulkanSupported())
		goto terminate;

	return 1;

	// Cleanup on failure.
terminate:
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
	glfwTerminate();

	// Detach and terminate.
	gfx_detach();
	_gfx_state_terminate();
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
	return _gfx_state_create_local();
}

/****************************/
GFX_API void gfx_detach(void)
{
	// Not yet initialized or attached.
	if (!_groufix.initialized || !_gfx_state_get_local())
		return;

	_gfx_state_destroy_local();
}
