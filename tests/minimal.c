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
		GFXFrame* frame = gfx_renderer_acquire(_t->renderer);
		gfx_frame_start(frame, 1, (GFXInject[]){ gfx_dep_wait(_t->dep) });
		gfx_recorder_render(_t->recorder, _t->pass, TEST_CALLBACK_RENDER, NULL);
		gfx_frame_submit(frame);
		gfx_heap_purge(_t->heap);
		gfx_wait_events();
	}
}


/****************************
 * Run the minimal test.
 */
TEST_MAIN(minimal);
