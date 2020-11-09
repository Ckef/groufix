/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/window.h"
#include "groufix/core.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>


/****************************/
GFX_API GFXWindow* gfx_create_window(size_t width, size_t height,
                                     const char* title, GFXMonitor* monitor)
{
	assert(width > 0);
	assert(height > 0);
	assert(title != NULL);

	// Allocate and set a new window.
	// Just set the user pointer and all callbacks to all 0's.
	_GFXWindow* window = malloc(sizeof(_GFXWindow));
	if (window == NULL)
		goto clean;

	memset(&window->base, 0, sizeof(GFXWindow));

	// Create a GLFW window.
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	window->handle = glfwCreateWindow(
		(int)width,
		(int)height,
		title,
		(monitor != NULL) ? ((_GFXMonitor*)monitor)->handle : NULL,
		NULL);

	if (window->handle == NULL)
		goto clean;

	// TODO: register all the callbacks.

	return &window->base;

	// Cleanup on failure.
clean:
	gfx_log_error("Could not create a new window.");
	free(window);

	return NULL;
}

/****************************/
GFX_API void gfx_destroy_window(GFXWindow* window)
{
	assert(window != NULL);

	glfwDestroyWindow(((_GFXWindow*)window)->handle);
	free(window);
}
