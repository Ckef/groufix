/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <assert.h>


/****************************/
int _gfx_swapchain_recreate(_GFXWindow* window)
{
	assert(window != NULL);
	assert(window->device != NULL);
	assert(window->device->context != NULL);

	// TODO: (re)create the thing.

	return 1;
}

/****************************/
int _gfx_swapchain_resized(_GFXWindow* window)
{
	int resized = 0;

	// Get the signal and set it back to 0.
#if defined (__STDC_NO_ATOMICS__)
	_gfx_mutex_lock(&window->sizeLock);
	resized = window->resized;
	window->resized = 0;
	_gfx_mutex_unlock(&window->sizeLock);
#else
	resized = atomic_exchange(&window->resized, 0);
#endif

	return resized;
}
