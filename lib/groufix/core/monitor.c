/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <stdlib.h>


/****************************
 * Allocates and initializes a new groufix monitor from a GLFW handle.
 * Automatically appends the monitor to groufix_.monitors.
 * @param handle Cannot be NULL.
 * @return NULL on failure.
 */
static GFXMonitor_* gfx_alloc_monitor_(GLFWmonitor* handle)
{
	assert(handle != NULL);

	int vidCount;
	const GLFWvidmode* modes = glfwGetVideoModes(handle, &vidCount);

	// Allocate an array of video modes at the end of the monitor.
	// Always allocate the video mode count GLFW reports, we might allocate
	// a bit extra, but who cares.
	GFXMonitor_* monitor = malloc(
		sizeof(GFXMonitor_) +
		sizeof(GFXVideoMode) * (size_t)vidCount);

	if (monitor == NULL)
		return NULL;

	if (!gfx_vec_push(&groufix_.monitors, 1, &monitor))
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

	for (size_t m = 0; m < (size_t)vidCount; ++m)
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
				.width = (uint32_t)modes[m].width,
				.height = (uint32_t)modes[m].height,
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
static void gfx_glfw_monitor_(GLFWmonitor* handle, int event)
{
	const bool conn = (event == GLFW_CONNECTED);
	GFXMonitor_* monitor;

	if (conn)
	{
		// On connect, allocate a new monitor and
		// attempt to insert it into the configuration.
		monitor = gfx_alloc_monitor_(handle);
		if (monitor == NULL)
		{
			gfx_log_fatal("Could not initialize a newly connected monitor.");
			return;
		}

		// Wanna know about it?
		const GLFWvidmode* vid = glfwGetVideoMode(monitor->handle);

		gfx_log_info(
			"Monitor connected:\n"
			"    [ %s ] (%dx%d @ %d)\n",
			monitor->base.name,
			vid->width, vid->height, vid->refreshRate);
	}
	else
	{
		// On disconnect, get associated groufix monitor.
		monitor = glfwGetMonitorUserPointer(handle);

		// Then shrink the configuration.
		gfx_vec_pop(&groufix_.monitors, 1);

		// Wanna know about it?
		gfx_log_info(
			"Monitor disconnected:\n"
			"    [ %s ]\n",
			monitor->base.name);
	}

	// So we don't know if the order of the configuration array is preserved.
	// On connect, we just inserted at the end and on disconnect we popped it.
	// To fix this, we just rebuild the entire array from GLFW user pointers.
	// This shuffles out deallocated monitors and fixes the primary one.
	int count;
	GLFWmonitor** handles = glfwGetMonitors(&count);

	for (size_t i = 0; i < (size_t)count; ++i)
	{
		GFXMonitor_* h = glfwGetMonitorUserPointer(handles[i]);
		*(GFXMonitor_**)gfx_vec_at(&groufix_.monitors, i) = h;
	}

	// Finally, call the event if given, and free the monitor on disconnect.
	if (groufix_.monitorEvent != NULL)
		groufix_.monitorEvent(&monitor->base, conn);

	if (!conn)
		free(monitor);
}

/****************************/
bool gfx_monitors_init_(void)
{
	assert(groufix_.monitors.size == 0);

	// Get all GLFW monitors.
	int count;
	GLFWmonitor** handles = glfwGetMonitors(&count);

	// Reserve some data and create groufix monitors.
	if (!gfx_vec_reserve(&groufix_.monitors, (size_t)count))
		goto terminate;

	for (size_t i = 0; i < (size_t)count; ++i)
		if (gfx_alloc_monitor_(handles[i]) == NULL)
			goto terminate;

	if (groufix_.monitors.size > 0)
	{
		// Let's see the connected monitors :)
		GFXBufWriter* logger = gfx_logger_info();
		if (logger != NULL)
		{
			gfx_io_writef(logger, "Detected monitors:\n");

			for (size_t i = 0; i < groufix_.monitors.size; ++i)
			{
				GFXMonitor_** monitor = gfx_vec_at(&groufix_.monitors, i);
				const GLFWvidmode* vid = glfwGetVideoMode((*monitor)->handle);

				gfx_io_writef(logger,
					"    [ %s ] (%dx%d @ %d)\n",
					(*monitor)->base.name,
					vid->width, vid->height, vid->refreshRate);
			}

			gfx_logger_end(logger);
		}
	}

	// Make sure we get configuration change events.
	glfwSetMonitorCallback(gfx_glfw_monitor_);

	return 1;


	// Cleanup on failure.
terminate:
	gfx_log_error("Could not initialize all connected monitors.");
	gfx_monitors_terminate_();

	return 0;
}

/****************************/
void gfx_monitors_terminate_(void)
{
	// In case it did not initialize, make it a no-op.
	if (groufix_.monitors.size == 0)
		return;

	// First just deallocate all monitors.
	for (size_t i = 0; i < groufix_.monitors.size; ++i)
	{
		GFXMonitor_** monitor = gfx_vec_at(&groufix_.monitors, i);

		glfwSetMonitorUserPointer((*monitor)->handle, NULL);
		free(*monitor);
	}

	// Clear data.
	glfwSetMonitorCallback(NULL);
	gfx_vec_clear(&groufix_.monitors);
}

/****************************/
GFX_API void gfx_monitor_event_set(void (*event)(GFXMonitor*, bool))
{
	assert(atomic_load(&groufix_.initialized));

	// Yeah just set the event callback.
	groufix_.monitorEvent = event;
}

/****************************/
GFX_API size_t gfx_get_num_monitors(void)
{
	assert(atomic_load(&groufix_.initialized));

	return groufix_.monitors.size;
}

/****************************/
GFX_API GFXMonitor* gfx_get_monitor(size_t index)
{
	assert(atomic_load(&groufix_.initialized));
	assert(index < groufix_.monitors.size);

	return *(GFXMonitor**)gfx_vec_at(&groufix_.monitors, index);
}

/****************************/
GFX_API GFXMonitor* gfx_get_primary_monitor(void)
{
	assert(atomic_load(&groufix_.initialized));
	assert(groufix_.monitors.size > 0);

	// Just return the first,
	// due to GLFW guarantees this should be the primary.
	return *(GFXMonitor**)gfx_vec_at(&groufix_.monitors, 0);
}

/****************************/
GFX_API size_t gfx_monitor_get_num_modes(GFXMonitor* monitor)
{
	assert(monitor != NULL);

	return ((GFXMonitor_*)monitor)->numModes;
}

/****************************/
GFX_API GFXVideoMode gfx_monitor_get_mode(GFXMonitor* monitor, size_t index)
{
	assert(monitor != NULL);
	assert(index < ((GFXMonitor_*)monitor)->numModes);

	return ((GFXMonitor_*)monitor)->modes[index];
}

/****************************/
GFX_API GFXVideoMode gfx_monitor_get_current_mode(GFXMonitor* monitor)
{
	assert(monitor != NULL);

	// We don't lookup the video mode array,
	// instead we cheat a little and take GLFW's current mode.
	const GLFWvidmode* vid =
		glfwGetVideoMode(((GFXMonitor_*)monitor)->handle);

	return (GFXVideoMode){
		.width = (uint32_t)vid->width,
		.height = (uint32_t)vid->height,
		.refresh = (unsigned int)vid->refreshRate
	};
}
