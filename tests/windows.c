/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "test.h"


/****************************
 * Multiple windows test.
 */
TEST_DESCRIBE(windows, _t)
{
	// Create a second window.
	GFXWindow* window2 = gfx_create_window(
		GFX_WINDOW_RESIZABLE | GFX_WINDOW_DOUBLE_BUFFER,
		NULL, NULL, (GFXVideoMode){ .width = 600, .height = 400 }, "groufix2");

	if (window2 == NULL)
		TEST_FAIL();

	// Add second window to the renderer.
	if (!gfx_renderer_attach_window(_t->renderer, 1, window2))
		TEST_FAIL();

	// And create a pass writing to it.
	GFXPass* pass = gfx_renderer_add(_t->renderer, 0, NULL);
	if (pass == NULL)
		TEST_FAIL();

	if (!gfx_pass_write(pass, 1))
		TEST_FAIL();

	// Make it render the thing.
	gfx_pass_use(pass, _t->primitive, _t->group);

	// Setup an event loop.
	// We wait instead of poll, only update when an event was detected.
	while (
		!gfx_window_should_close(_t->window) &&
		!gfx_window_should_close(window2))
	{
		gfx_renderer_submit(_t->renderer);
		gfx_wait_events();
	}

	// Detach window & destroy.
	gfx_renderer_detach(_t->renderer, 1);
	gfx_destroy_window(window2);
}


/****************************
 * Run the windows test.
 */
TEST_MAIN(windows);
