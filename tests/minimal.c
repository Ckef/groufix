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
	// Setup an event loop.
	// We wait instead of poll, only update when an event was detected.
	while (!gfx_window_should_close(_t->window))
	{
		gfx_frame_submit(gfx_renderer_acquire(_t->renderer));
		gfx_wait_events();
	}
}


/****************************
 * Run the minimal test.
 */
TEST_MAIN(minimal);
