/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "test.h"


/****************************
 * Second render callback to draw a different renderable.
 */
static void render2(GFXRecorder* recorder, unsigned int frame, void* ptr)
{
	gfx_cmd_bind(recorder, TEST_BASE.technique, 0, 1, 0, &TEST_BASE.set, NULL);
	gfx_cmd_draw_indexed(recorder, (GFXRenderable*)ptr, 0, 0, 0, 0, 1);
}


/****************************
 * Multiple windows test.
 */
TEST_DESCRIBE(windows, t)
{
	// Create a second window.
	GFXWindow* window2 = gfx_create_window(
		GFX_WINDOW_RESIZABLE | GFX_WINDOW_DOUBLE_BUFFER,
		t->device, NULL,
		(GFXVideoMode){ .width = 600, .height = 400 }, "groufix2");

	if (window2 == NULL)
		TEST_FAIL();

	// Register the default key events.
	window2->events.key.release = TEST_EVT_KEY_RELEASE;

	// Add second window to the renderer.
	if (!gfx_renderer_attach_window(t->renderer, 1, window2))
		TEST_FAIL();

	// And create a pass writing to it.
	GFXPass* pass2 = gfx_renderer_add_pass(t->renderer, 0, NULL);
	if (pass2 == NULL)
		TEST_FAIL();

	if (!gfx_pass_consume(pass2, 1, GFX_ACCESS_ATTACHMENT_WRITE, 0))
		TEST_FAIL();

	// And of course a second renderable.
	GFXRenderable renderable2;
	gfx_renderable(&renderable2, pass2, t->technique, t->primitive);

	// Setup an event loop.
	// We wait instead of poll, only update when an event was detected.
	while (
		!gfx_window_should_close(t->window) &&
		!gfx_window_should_close(window2))
	{
		GFXFrame* frame = gfx_renderer_acquire(t->renderer);
		gfx_frame_start(frame, 1, (GFXInject[]){ gfx_dep_wait(t->dep) });
		gfx_recorder_render(t->recorder, t->pass, TEST_CALLBACK_RENDER, NULL);
		gfx_recorder_render(t->recorder, pass2, render2, &renderable2);
		gfx_frame_submit(frame);
		gfx_heap_purge(t->heap);
		gfx_wait_events();
	}

	// Detach window & destroy.
	gfx_renderer_detach(t->renderer, 1);
	gfx_destroy_window(window2);
}


/****************************
 * Run the windows test.
 */
TEST_MAIN(windows);
