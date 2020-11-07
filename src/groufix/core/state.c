/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <assert.h>
#include <stdlib.h>


/****************************/
_GFXState _groufix = { .initialized = 0 };


/****************************/
int _gfx_state_init(void)
{
	assert(!_groufix.initialized);

	// Initialize thread local data.
	if (!_gfx_thread_key_init(&_groufix.thread.key))
		return 0;

	if (!_gfx_mutex_init(&_groufix.thread.ioLock))
		goto clean_key;

#if defined (__STDC_NO_ATOMICS__)
	if (!_gfx_mutex_init(&_groufix.thread.idLock))
		goto clean_io;
#endif

	_groufix.thread.id = 0;

	// Initialize other things...
	gfx_vec_init(&_groufix.devices, sizeof(GFXDevice));
	gfx_vec_init(&_groufix.monitors, sizeof(_GFXMonitor*));
	gfx_vec_init(&_groufix.windows, sizeof(_GFXWindow*));

	_groufix.monitorEvent = NULL;

	_groufix.vk.CreateInstance = NULL;
	_groufix.vk.DestroyInstance = NULL;

	// Signal that initialization is done.
	_groufix.initialized = 1;

	return 1;

	// Cleanup on failure.
#if defined (__STDC_NO_ATOMICS__)
clean_io:
	_gfx_mutex_clear(&_groufix.thread.ioLock);
#endif
clean_key:
	_gfx_thread_key_clear(_groufix.thread.key);

	return 0;
}

/****************************/
void _gfx_state_terminate(void)
{
	assert(_groufix.initialized);

	gfx_vec_clear(&_groufix.devices);
	gfx_vec_clear(&_groufix.monitors);
	gfx_vec_clear(&_groufix.windows);

	_gfx_thread_key_clear(_groufix.thread.key);
	_gfx_mutex_clear(&_groufix.thread.ioLock);
#if defined (__STDC_NO_ATOMICS__)
	_gfx_mutex_clear(&_groufix.thread.idLock);
#endif

	// Signal that termination is done.
	_groufix.initialized = 0;
}

/****************************/
int _gfx_state_create_local(void)
{
	assert(_groufix.initialized);
	assert(!_gfx_thread_key_get(_groufix.thread.key));

	// Allocate and set state.
	_GFXThreadState* state = malloc(sizeof(_GFXThreadState));
	if (state == NULL) return 0;

	if (!_gfx_thread_key_set(_groufix.thread.key, state))
	{
		free(state);
		return 0;
	}

	// Give it a unique id.
#if defined (__STDC_NO_ATOMICS__)
	_gfx_mutex_lock(&_groufix.thread.idLock);
	state->id = _groufix.thread.id++;
	_gfx_mutex_unlock(&_groufix.thread.idLock);
#else
	state->id = _groufix.thread.id++;
#endif

	// Initialize the logging stuff.
	state->log.level = GFX_LOG_DEFAULT;
	state->log.std = 0;
	state->log.file = NULL;

	return 1;
}

/****************************/
void _gfx_state_destroy_local(void)
{
	assert(_groufix.initialized);
	assert(_gfx_thread_key_get(_groufix.thread.key));

	// Get the key and clear all its data.
	_GFXThreadState* state = _gfx_thread_key_get(_groufix.thread.key);

	if (state->log.file != NULL)
		fclose(state->log.file);

	// Then free it.
	free(state);

	// I mean this better not fail...
	_gfx_thread_key_set(_groufix.thread.key, NULL);
}

/****************************/
_GFXThreadState* _gfx_state_get_local(void)
{
	assert(_groufix.initialized);

	// Just return stored data.
	return _gfx_thread_key_get(_groufix.thread.key);
}
