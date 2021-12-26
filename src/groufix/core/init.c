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
_GFXState _groufix =
{
	.initialized = 0,
	.logDef = GFX_LOG_DEFAULT
};


/****************************/
int _gfx_init(void)
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
	if (!_gfx_mutex_init(&_groufix.contextLock))
		goto clean_local;

	gfx_vec_init(&_groufix.devices, sizeof(_GFXDevice));
	gfx_list_init(&_groufix.contexts);
	gfx_vec_init(&_groufix.monitors, sizeof(_GFXMonitor*));

	_groufix.monitorEvent = NULL;
	_groufix.vk.instance = NULL;

	// Signal that initialization is done.
	_groufix.initialized = 1;

	return 1;


	// Cleanup on failure.
clean_local:
#if defined (__STDC_NO_ATOMICS__)
	_gfx_mutex_clear(&_groufix.thread.idLock);
clean_io:
#endif
	_gfx_mutex_clear(&_groufix.thread.ioLock);
clean_key:
	_gfx_thread_key_clear(_groufix.thread.key);

	return 0;
}

/****************************/
void _gfx_terminate(void)
{
	assert(_groufix.initialized);

	gfx_vec_clear(&_groufix.devices);
	gfx_list_clear(&_groufix.contexts);
	gfx_vec_clear(&_groufix.monitors);

	_gfx_thread_key_clear(_groufix.thread.key);
	_gfx_mutex_clear(&_groufix.thread.ioLock);
#if defined (__STDC_NO_ATOMICS__)
	_gfx_mutex_clear(&_groufix.thread.idLock);
#endif
	_gfx_mutex_clear(&_groufix.contextLock);

	// Signal that termination is done.
	_groufix.initialized = 0;
}

/****************************/
int _gfx_create_local(void)
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
	state->log.level = _groufix.logDef;
	state->log.out = GFX_IO_STDERR; // For initial identification.

	return 1;
}

/****************************/
void _gfx_destroy_local(void)
{
	assert(_groufix.initialized);
	assert(_gfx_thread_key_get(_groufix.thread.key));

	// Get key and free it.
	free(_gfx_thread_key_get(_groufix.thread.key));

	// I mean this better not fail...
	_gfx_thread_key_set(_groufix.thread.key, NULL);
}

/****************************/
_GFXThreadState* _gfx_get_local(void)
{
	assert(_groufix.initialized);

	// Just return stored data.
	return _gfx_thread_key_get(_groufix.thread.key);
}
