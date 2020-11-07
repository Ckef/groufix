/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/log.h"
#include "groufix/core/window.h"
#include "groufix/core.h"
#include <assert.h>
#include <stdlib.h>


/****************************/
static void _gfx_monitors_rebuild(void)
{
	// Get all GLFW monitors.
	int count;
	GLFWmonitor** handles = glfwGetMonitors(&count);

	// Place the associated groufix monitors in the configuration.
	for (size_t i = 0; (int)i < count; ++i)
	{
		_GFXMonitor* monitor = glfwGetMonitorUserPointer(handles[i]);
		*(_GFXMonitor**)gfx_vec_at(&_groufix.monitors, i) = monitor;
	}
}

/****************************/
static void _gfx_glfw_monitor(GLFWmonitor* handle, int event)
{
	int conn = (event == GLFW_CONNECTED) ? 1 : 0;

	// Get associated groufix monitor.
	_GFXMonitor* monitor = glfwGetMonitorUserPointer(handle);

	if (conn)
	{
		// On connect, allocate a new monitor and
		// attempt to insert it into the configuration.
		monitor = malloc(sizeof(_GFXMonitor));
		if (monitor == NULL || !gfx_vec_push(&_groufix.monitors, 1, &monitor))
		{
			gfx_log_fatal("Could not initialize a newly connected monitor.");
			free(monitor);

			return;
		}

		// Associate with GLFW using the user pointer and
		// initialize the monitor itself.
		glfwSetMonitorUserPointer(handle, monitor);

		monitor->base.ptr = NULL;
		monitor->handle = handle;
	}
	else
	{
		// On disconnect, shrink the configuration.
		gfx_vec_pop(&_groufix.monitors, 1);
	}

	// So we don't know if the order of the configuration array is preserved.
	// On connect, we just inserted at the end and on disconnect we popped it.
	// To fix this, we just rebuild the entire array.
	_gfx_monitors_rebuild();

	// Finally, call the event if given and free the monitor on disconnect.
	if (_groufix.monitorEvent != NULL)
		_groufix.monitorEvent(&monitor->base, conn);

	if (!conn)
		free(monitor);
}

/****************************/
int _gfx_monitors_init(void)
{
	assert(_groufix.monitors.size == 0);

	// Get all GLFW monitors.
	int count;
	GLFWmonitor** handles = glfwGetMonitors(&count);

	// Reserve some data and create groufix monitors.
	// This seems rather odd, allocating new memory for every monitor.
	// Especially as it does not store that much interesting data...
	// However we do this for API consistency and extendability.
	if (!gfx_vec_reserve(&_groufix.monitors, (size_t)count))
		goto terminate;

	for (size_t i = 0; (int)i < count; ++i)
	{
		_GFXMonitor* monitor = malloc(sizeof(_GFXMonitor));
		if (monitor == NULL)
			goto terminate;

		// Associate with GLFW using the user pointer and
		// initialize the monitor itself.
		glfwSetMonitorUserPointer(handles[i], monitor);

		monitor->base.ptr = NULL;
		monitor->handle = handles[i];

		// There should be enough room because we reserved data.
		gfx_vec_push(&_groufix.monitors, 1, &monitor);
	}

	// Make sure we get configuration change events.
	glfwSetMonitorCallback(_gfx_glfw_monitor);

	return 1;

	// Cleanup on failure.
terminate:
	gfx_log_error("Could not initialize all connected monitors.");
	_gfx_monitors_terminate();

	return 0;
}

/****************************/
void _gfx_monitors_terminate(void)
{
	// First just deallocate all monitors.
	for (size_t i = 0; i < _groufix.monitors.size; ++i)
	{
		_GFXMonitor** monitor = gfx_vec_at(&_groufix.monitors, i);

		glfwSetMonitorUserPointer((*monitor)->handle, NULL);
		free(*monitor);
	}

	// Clear data.
	glfwSetMonitorCallback(NULL);
	gfx_vec_clear(&_groufix.monitors);
}

/****************************/
GFX_API GFXMonitor* gfx_get_primary_monitor(void)
{
	GLFWmonitor* handle = glfwGetPrimaryMonitor();
	if (handle == NULL) return NULL;

	// Retrieve the groufix monitor from the GLFW one.
	_GFXMonitor* monitor = glfwGetMonitorUserPointer(handle);

	return &monitor->base;
}

/****************************/
GFX_API GFXMonitor** gfx_get_monitors(size_t* count)
{
	assert(count != NULL);

	*count = _groufix.monitors.size;
	return _groufix.monitors.data;
}

/****************************/
GFX_API void gfx_set_monitor_event(void (*event)(GFXMonitor*, int))
{
	// Yeah just set the event callback.
	_groufix.monitorEvent = event;
}
