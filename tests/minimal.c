/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include <groufix.h>
#include <stdio.h>
#include <stdlib.h>


/****************************/
static void print_info(void)
{
	// Enumerate devices.
	size_t numDevices = gfx_get_num_devices();
	printf("#devices: %u\n", (unsigned int)numDevices);

	for (size_t i = 0; i < numDevices; ++i)
	{
		GFXDevice* device = gfx_get_device(i);
		printf("\t{ .type = %u, .name = %s }\n", device->type, device->name);
	}

	// Enumerate monitors.
	size_t numMonitors = gfx_get_num_monitors();
	printf("#monitors: %u\n", (unsigned int)numMonitors);

	for (size_t i = 0; i < numMonitors; ++i)
	{
		GFXMonitor* monitor = gfx_get_monitor(i);
		printf("\t{ .name = %s, .modes = [ ", monitor->name);

		// Enumerate monitor modes.
		size_t numModes = gfx_monitor_get_num_modes(monitor);

		for (size_t m = 0; m < numModes; ++m)
		{
			GFXVideoMode mode = gfx_monitor_get_mode(monitor, m);
			printf("%ux%u @ %uHz",
				(unsigned int)mode.width,
				(unsigned int)mode.height,
				mode.refresh);

			if (m < numModes-1) printf(", ");
		}

		printf(" ] }\n");
	}
}

/****************************/
void key_release(GFXWindow* window, GFXKey key, int scan, GFXModifier mod)
{
	switch (key)
	{
	// Toggle fullscreen on F11.
	case GFX_KEY_F11:
		if (*(int*)window->ptr)
		{
			GFXVideoMode mode = { .width = 600, .height = 400 };
			gfx_window_set_monitor(window, NULL, mode);
		}
		else
		{
			GFXMonitor* monitor = gfx_get_primary_monitor();
			GFXVideoMode mode = gfx_monitor_get_current_mode(monitor);
			gfx_window_set_monitor(window, monitor, mode);
		}

		*(int*)window->ptr ^= 1;
		break;

	// Close on escape.
	case GFX_KEY_ESCAPE:
		gfx_window_set_close(window, 1);
		break;

	default:
		break;
	}
}

/****************************/
int main()
{
	// Initialize.
	if (!gfx_init())
		goto fail;

	// Enumerate devices and monitors.
	print_info();

	// Create a window.
	GFXWindow* window = gfx_create_window(GFX_WINDOW_RESIZABLE,
		NULL, (GFXVideoMode){ .width = 600, .height = 400 }, "groufix");

	if (window == NULL)
		goto fail_terminate;

	// Register key release event.
	int isFullscreen = 0;
	window->ptr = &isFullscreen;
	window->events.key.release = key_release;

	// Setup an event loop.
	while (!gfx_window_should_close(window))
		gfx_poll_events();

	// Terminate.
	gfx_destroy_window(window);
	gfx_terminate();

	puts("Success!");
	exit(EXIT_SUCCESS);

	// On failure.
fail_terminate:
	gfx_terminate();
fail:
	puts("Failure!");
	exit(EXIT_FAILURE);
}
