/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <stdlib.h>


/****************************/
GFXState_ groufix_ =
{
	.initialized = 0,
	.logDef = GFX_LOG_DEFAULT
};


/****************************/
bool gfx_init_(void)
{
	assert(!atomic_load(&groufix_.initialized));

	// Initialize thread local data.
	if (!gfx_thread_key_init_(&groufix_.thread.key))
		return 0;

	if (!gfx_mutex_init_(&groufix_.thread.ioLock))
		goto clean_key;

	atomic_store_explicit(&groufix_.thread.id, 0, memory_order_relaxed);

	// Initialize other things.
	if (!gfx_mutex_init_(&groufix_.contextLock))
		goto clean_io;

	gfx_vec_init(&groufix_.devices, sizeof(GFXDevice_));
	gfx_list_init(&groufix_.contexts);
	gfx_vec_init(&groufix_.monitors, sizeof(GFXMonitor_*));
	gfx_vec_init(&groufix_.gamepads, sizeof(GFXGamepad_*));

	groufix_.monitorEvent = NULL;
	groufix_.gamepadEvent = NULL;
	groufix_.vk.instance = NULL;

#if defined (GFX_USE_VK_VALIDATION_LAYERS)
	groufix_.vk.useValidationLayers = 1;
#endif

	// Start clock as last.
	gfx_clock_(&groufix_.clock);

	// Signal that initialization is done.
	atomic_store(&groufix_.initialized, 1);

	return 1;


	// Cleanup on failure.
clean_io:
	gfx_mutex_clear_(&groufix_.thread.ioLock);
clean_key:
	gfx_thread_key_clear_(groufix_.thread.key);

	return 0;
}

/****************************/
void gfx_terminate_(void)
{
	assert(atomic_load(&groufix_.initialized));

	gfx_vec_clear(&groufix_.devices);
	gfx_list_clear(&groufix_.contexts);
	gfx_vec_clear(&groufix_.monitors);
	gfx_vec_clear(&groufix_.gamepads);

	gfx_thread_key_clear_(groufix_.thread.key);
	gfx_mutex_clear_(&groufix_.thread.ioLock);
	gfx_mutex_clear_(&groufix_.contextLock);

	// Signal that termination is done.
	atomic_store(&groufix_.initialized, 0);
}

/****************************/
bool gfx_create_local_(void)
{
	assert(atomic_load(&groufix_.initialized));
	assert(!gfx_thread_key_get_(groufix_.thread.key));

	// Allocate and set state.
	GFXThreadState_* state = malloc(sizeof(GFXThreadState_));
	if (state == NULL) return 0;

	if (!gfx_thread_key_set_(groufix_.thread.key, state))
	{
		free(state);
		return 0;
	}

	// Give it a unique id.
	state->id =
		atomic_fetch_add_explicit(&groufix_.thread.id, 1, memory_order_relaxed);

	// Initialize the logging stuff.
	state->log.level = groufix_.logDef;
	gfx_buf_writer(&state->log.out, gfx_io_buf_def_.dest);

	return 1;
}

/****************************/
void gfx_destroy_local_(void)
{
	assert(atomic_load(&groufix_.initialized));
	assert(gfx_thread_key_get_(groufix_.thread.key));

	// Get key and free it.
	free(gfx_thread_key_get_(groufix_.thread.key));

	// I mean this better not fail...
	gfx_thread_key_set_(groufix_.thread.key, NULL);
}

/****************************/
GFXThreadState_* gfx_get_local_(void)
{
	assert(atomic_load(&groufix_.initialized));

	// Just return stored data.
	return gfx_thread_key_get_(groufix_.thread.key);
}
