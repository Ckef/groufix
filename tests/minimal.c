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
TEST_DESCRIBE(minimal, t)
{
	// Setup an event loop.
	// We wait instead of poll, only update when an event was detected.
	while (!gfx_window_should_close(t->window))
	{
		GFXFrame* frame = gfx_renderer_start(t->renderer);
		gfx_recorder_render(t->recorder, t->pass, TEST_CALLBACK_RENDER, NULL);
		gfx_frame_submit(frame);
		gfx_wait_events();
	}
}


/****************************
 * Run the minimal test.
 */
TEST_MAIN(minimal);
