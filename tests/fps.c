/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "test.h"


/****************************
 * Minimal test.
 */
TEST_DESCRIBE(minimal, _t)
{
	// Triple buffer the window for the maniest frames per second.
	// This way we're not limited to waiting on v-sync.
	gfx_window_set_flags(
		_t->window,
		gfx_window_get_flags(_t->window) | GFX_WINDOW_TRIPLE_BUFFER);

	// Create a single render pass that writes to the window.
	GFXRenderPass* pass = gfx_renderer_add(_t->renderer, 0, NULL);
	if (pass == NULL)
		TEST_FAIL();

	if (!gfx_render_pass_write(pass, 0))
		TEST_FAIL();

	// Setup an event loop.
	while (!gfx_window_should_close(_t->window))
	{
		gfx_renderer_submit(_t->renderer);
		gfx_poll_events();
	}
}


/****************************
 * Run the minimal test.
 */
TEST_MAIN(minimal);
