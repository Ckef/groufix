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


/****************************
 * Allocates and initializes a new groufix monitor from a GLFW handle.
 * Automatically appends the monitor to _groufix.monitors.
 * @param handle Cannot be NULL.
 * @return NULL on failure.
 */
static _GFXMonitor* _gfx_alloc_monitor(GLFWmonitor* handle)
{
	assert(handle != NULL);

	int vidCount;
	const GLFWvidmode* modes = glfwGetVideoModes(handle, &vidCount);

	// Allocate an array of video modes at the end of the monitor.
	// Always allocate the video mode count GLFW reports, we might allocate
	// a bit extra, but who cares.
	_GFXMonitor* monitor = malloc(
		sizeof(_GFXMonitor) +
		sizeof(GFXVideoMode) * (unsigned int)vidCount);

	if (monitor == NULL)
		return NULL;

	if (!gfx_vec_push(&_groufix.monitors, 1, &monitor))
	{
		free(monitor);
		return NULL;
	}

	// Associate with GLFW using the user pointer and
	// initialize the monitor itself.
	glfwSetMonitorUserPointer(handle, monitor);

	monitor->base.ptr = NULL;
	monitor->base.name = glfwGetMonitorName(handle);
	monitor->handle = handle;

	monitor->numModes = 0;

	for (size_t m = 0; (int)m < vidCount; ++m)
	{
		// Try to find the video mode in the modes we are exposing.
		// We can have duplicates when the same resolution and refresh
		// rate have different options for bit depth.
		// However we are ignoring GLFW bit depth as we specify this in the
		// Vulkan swapchain...
		size_t f;
		for (f = 0; f < monitor->numModes; ++f)
			if (
				modes[m].width == (int)monitor->modes[f].width &&
				modes[m].height == (int)monitor->modes[f].height &&
				modes[m].refreshRate == (int)monitor->modes[f].refresh)
			{
				break;
			}

		// If not found, insert a new mode to expose.
		if (f >= monitor->numModes)
		{
			monitor->modes[f] = (GFXVideoMode){
				.width = (size_t)modes[m].width,
				.height = (size_t)modes[m].height,
				.refresh = (unsigned int)modes[m].refreshRate
			};

			++monitor->numModes;
		}
	}

	return monitor;
}

/****************************
 * On monitor connect or disconnect.
 * @param event Zero if it is disconnected, non-zero if it is connected.
 */
static void _gfx_glfw_monitor(GLFWmonitor* handle, int event)
{
	int conn = (event == GLFW_CONNECTED) ? 1 : 0;

	// Get associated groufix monitor.
	_GFXMonitor* monitor = glfwGetMonitorUserPointer(handle);

	if (conn)
	{
		// On connect, allocate a new monitor and
		// attempt to insert it into the configuration.
		monitor = _gfx_alloc_monitor(handle);
		if (monitor == NULL)
		{
			gfx_log_fatal("Could not initialize a newly connected monitor.");
			return;
		}
	}
	else
	{
		// On disconnect, shrink the configuration.
		gfx_vec_pop(&_groufix.monitors, 1);
	}

	// So we don't know if the order of the configuration array is preserved.
	// On connect, we just inserted at the end and on disconnect we popped it.
	// To fix this, we just rebuild the entire array from GLFW user pointers.
 	// This shuffles out deallocated monitors and fixes the primary one.
	int count;
	GLFWmonitor** handles = glfwGetMonitors(&count);

	for (size_t i = 0; (int)i < count; ++i)
	{
		_GFXMonitor* h = glfwGetMonitorUserPointer(handles[i]);
		*(_GFXMonitor**)gfx_vec_at(&_groufix.monitors, i) = h;
	}

	// Finally, call the event if given, and free the monitor on disconnect.
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
	if (!gfx_vec_reserve(&_groufix.monitors, (size_t)count))
		goto terminate;

	for (size_t i = 0; (int)i < count; ++i)
		if (_gfx_alloc_monitor(handles[i]) == NULL)
			goto terminate;

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
	// In case it did not initialize, make it a no-op.
	if (_groufix.monitors.size == 0)
		return;

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
GFX_API void gfx_set_monitor_event(void (*event)(GFXMonitor*, int))
{
	assert(_groufix.initialized);

	// Yeah just set the event callback.
	_groufix.monitorEvent = event;
}

/****************************/
GFX_API size_t gfx_get_num_monitors(void)
{
	assert(_groufix.initialized);

	return _groufix.monitors.size;
}

/****************************/
GFX_API GFXMonitor* gfx_get_monitor(size_t index)
{
	assert(_groufix.initialized);
	assert(_groufix.monitors.size > 0);
	assert(index < _groufix.monitors.size);

	return *(GFXMonitor**)gfx_vec_at(&_groufix.monitors, index);
}

/****************************/
GFX_API GFXMonitor* gfx_get_primary_monitor(void)
{
	assert(_groufix.initialized);
	assert(_groufix.monitors.size > 0);

	// Just return the first,
	// due to GLFW guarantees this should be the primary.
	return *(GFXMonitor**)gfx_vec_at(&_groufix.monitors, 0);
}

/****************************/
GFX_API size_t gfx_monitor_get_num_modes(GFXMonitor* monitor)
{
	assert(monitor != NULL);

	return ((_GFXMonitor*)monitor)->numModes;
}

/****************************/
GFX_API GFXVideoMode gfx_monitor_get_mode(GFXMonitor* monitor, size_t index)
{
	assert(monitor != NULL);
	assert(index < ((_GFXMonitor*)monitor)->numModes);

	return ((_GFXMonitor*)monitor)->modes[index];
}

/****************************/
GFX_API GFXVideoMode gfx_monitor_get_current_mode(GFXMonitor* monitor)
{
	assert(monitor != NULL);

	// We don't lookup the video mode array,
	// instead we cheat a little and take GLFW's current mode.
	const GLFWvidmode* vid =
		glfwGetVideoMode(((_GFXMonitor*)monitor)->handle);

	return (GFXVideoMode){
		.width = (size_t)vid->width,
		.height = (size_t)vid->height,
		.refresh = (unsigned int)vid->refreshRate
	};
}
