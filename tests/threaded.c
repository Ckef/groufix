/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#define TEST_ENABLE_THREADS
#include "test.h"


/****************************
 * Terminate signal for threads.
 */
static atomic_bool termSig = 0;


/****************************
 * The bit that renders.
 */
TEST_DESCRIBE(render_loop, t)
{
	// Like the other loop, but submit the renderer :)
	while (!atomic_load(&termSig))
	{
		GFXFrame* frame = gfx_renderer_start(t->renderer);
		gfx_recorder_render(t->recorder, t->pass, TEST_CALLBACK_RENDER, NULL);
		gfx_frame_submit(frame);
	}
}


/****************************
 * Threading test.
 */
TEST_DESCRIBE(threaded, t)
{
	// Yeah we're gonna have maniest frames here too.
	gfx_window_set_flags(
		t->window,
		gfx_window_get_flags(t->window) | GFX_WINDOW_TRIPLE_BUFFER);

	// Create thread to run the render loop.
	TEST_RUN_THREAD(render_loop);

	// Setup an event loop.
	while (!gfx_window_should_close(t->window))
		gfx_wait_events();

	// Join the render thread.
	atomic_store(&termSig, 1);
	TEST_JOIN(render_loop);
}


/****************************
 * Run the threading test.
 */
TEST_MAIN(threaded);
