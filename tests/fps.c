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
TEST_DESCRIBE(fps, _t)
{
	// Triple buffer the window for the maniest frames per second.
	// This way we're not limited to waiting on v-sync.
	gfx_window_set_flags(
		_t->window,
		gfx_window_get_flags(_t->window) | GFX_WINDOW_TRIPLE_BUFFER);

	// Setup an event loop.
	while (!gfx_window_should_close(_t->window))
	{
		GFXFrame* frame = gfx_renderer_acquire(_t->renderer);
		gfx_frame_submit(frame, 1, (GFXInject[]){ gfx_dep_wait(_t->dep) });
		gfx_poll_events();
	}
}


/****************************
 * Run the fps test.
 */
TEST_MAIN(fps);
