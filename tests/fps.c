/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "test.h"


/****************************
 * Test to see many frames.
 */
TEST_DESCRIBE(fps, t)
{
	// Triple buffer the window for the maniest frames per second.
	// This way we're not limited to waiting on v-sync.
	gfx_window_set_flags(
		t->window,
		gfx_window_get_flags(t->window) | GFX_WINDOW_TRIPLE_BUFFER);

	// Setup an event loop.
	while (!gfx_window_should_close(t->window))
	{
		GFXFrame* frame = gfx_renderer_acquire(t->renderer);
		gfx_poll_events();
		gfx_frame_start(frame);
		gfx_pass_inject(t->pass, 1, (GFXInject[]){ gfx_dep_wait(t->dep) });
		gfx_recorder_render(t->recorder, t->pass, TEST_CALLBACK_RENDER, NULL);
		gfx_frame_submit(frame);
	}
}


/****************************
 * Run the fps test.
 */
TEST_MAIN(fps);
