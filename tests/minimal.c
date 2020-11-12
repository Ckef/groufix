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
		printf("\t{ .name = %s }\n", monitor->name);
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
	GFXWindow* window = gfx_create_window(600, 400, "groufix", NULL);
	if (window == NULL)
		goto fail;

	// Setup an event loop.
	while (!gfx_window_should_close(window))
		gfx_poll_events();

	// Terminate.
	gfx_destroy_window(window);
	gfx_terminate();

	puts("Success!");
	exit(EXIT_SUCCESS);

	// On failure.
fail:
	puts("Failure!");
	exit(EXIT_FAILURE);
}
