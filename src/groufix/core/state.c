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

#if defined (__STDC_NO_ATOMICS__)
	if (!_gfx_mutex_init(&_groufix.thread.mutex))
	{
		_gfx_thread_key_clear(_groufix.thread.key);
		return 0;
	}
#endif

	_groufix.thread.id = 0;

	// Signal that initialization is done.
	_groufix.initialized = 1;

	return 1;
}

/****************************/
void _gfx_state_terminate(void)
{
	assert(_groufix.initialized);

	_gfx_thread_key_clear(_groufix.thread.key);
#if defined (__STDC_NO_ATOMICS__)
	_gfx_mutex_clear(&_groufix.thread.mutex);
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
	_gfx_mutex_lock(&_groufix.thread.mutex);
	state->id = _groufix.thread.id++;
	_gfx_mutex_unlock(&_groufix.thread.mutex);
#else
	state->id = _groufix.thread.id++;
#endif

	return 1;
}

/****************************/
void _gfx_state_destroy_local(void)
{
	assert(_groufix.initialized);
	assert(_gfx_thread_key_get(_groufix.thread.key));

	// Get the key and free it.
	_GFXThreadState* state = _gfx_thread_key_get(_groufix.thread.key);
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
