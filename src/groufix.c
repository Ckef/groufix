/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix.h"
#include "groufix/core.h"


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
	{
		gfx_terminate();
		return 0;
	}

	return 1;
}

/****************************/
GFX_API void gfx_terminate(void)
{
	// Not yet initialized, just do nothing.
	if (!_groufix.initialized)
		return;

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
	if (!_gfx_state_create_local())
		return 0;

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
