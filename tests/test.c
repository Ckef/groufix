/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "test.h"
#include <stdio.h>
#include <stdlib.h>


/****************************
 * The testing base program state.
 */
static GFXTestBase _t = {
	.initialized = 0,
	.window      = NULL,
	.renderer    = NULL
};


/****************************/
static void _test_clear(void)
{
	gfx_destroy_renderer(_t.renderer);
	gfx_destroy_window(_t.window);
	gfx_terminate();

	// Don't bother resetting _t as we will exit() anyway.
}

/****************************/
static void _test_key_release(GFXWindow* window,
                              GFXKey key, int scan, GFXModifier mod)
{
	switch (key)
	{
	// Toggle fullscreen on F11.
	case GFX_KEY_F11:
		if (gfx_window_get_monitor(window) != NULL)
		{
			gfx_window_set_monitor(
				window, NULL,
				(GFXVideoMode){ .width = 600, .height = 400 });
		}
		else
		{
			GFXMonitor* monitor = gfx_get_primary_monitor();
			gfx_window_set_monitor(
				window, monitor,
				gfx_monitor_get_current_mode(monitor));
		}
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
GFXTestBase* _test_init(void)
{
	if (_t.initialized)
		return &_t;

	// Initialize.
	if (!gfx_init())
		goto fail;

	// Create a window.
	_t.window = gfx_create_window(
		GFX_WINDOW_RESIZABLE | GFX_WINDOW_DOUBLE_BUFFER,
		NULL, NULL, (GFXVideoMode){ .width = 600, .height = 400 }, "groufix");

	if (_t.window == NULL)
		goto fail;

	// Register the default key release event.
	_t.window->events.key.release = _test_key_release;

	// Create a renderer and attach the window to index 0.
	_t.renderer = gfx_create_renderer(NULL);
	if (_t.renderer == NULL)
		goto fail;

	if (!gfx_renderer_attach_window(_t.renderer, 0, _t.window))
		goto fail;

	// Set to initialized and return.
	_t.initialized = 1;

	return &_t;


	// On failure, run the failed state :)
fail:
	_test_fail();

	return NULL;
}

/****************************/
void _test_fail(void)
{
	_test_clear();

	fputs("\n* TEST FAILED\n", stderr);
	exit(EXIT_FAILURE);
}

/****************************/
void _test_end(void)
{
	_test_clear();

	fputs("\n* TEST SUCCESSFUL\n", stderr);
	exit(EXIT_SUCCESS);
}
