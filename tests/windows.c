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
		_t->device, NULL,
		(GFXVideoMode){ .width = 600, .height = 400 }, "groufix2");

	if (window2 == NULL)
		TEST_FAIL();

	// Register the default key events.
	window2->events.key.release = TEST_EVT_KEY_RELEASE;

	// Add second window to the renderer.
	if (!gfx_renderer_attach_window(_t->renderer, 1, window2))
		TEST_FAIL();

	// And create a pass writing to it.
	GFXPass* pass = gfx_renderer_add_pass(_t->renderer, 0, NULL);
	if (pass == NULL)
		TEST_FAIL();

	if (!gfx_pass_consume(pass, 1, GFX_ACCESS_ATTACHMENT_WRITE, 0))
		TEST_FAIL();

	// Make it render the thing.
	gfx_pass_use(pass, _t->primitive, _t->technique, _t->set);

	// Setup an event loop.
	// We wait instead of poll, only update when an event was detected.
	while (
		!gfx_window_should_close(_t->window) &&
		!gfx_window_should_close(window2))
	{
		GFXFrame* frame = gfx_renderer_acquire(_t->renderer);
		gfx_frame_start(frame, 1, (GFXInject[]){ gfx_dep_wait(_t->dep) });
		gfx_frame_submit(frame);
		gfx_heap_purge(_t->heap);
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
