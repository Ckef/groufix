/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#define TEST_ENABLE_THREADS
#include "test.h"

#if !defined (__STDC_NO_ATOMICS__)
	#include <stdatomic.h>
#endif


/****************************
 * Terminate signal for threads.
 */
#if !defined (__STDC_NO_ATOMICS__)
	static atomic_int termSig = 0;
#else
	static int termSig = 0; // Uh yeah whatever..
#endif


/****************************
 * The bit that renders.
 */
TEST_DESCRIBE(render_loop, _t)
{
	// Like the other loop, but submit the renderer :)
	while (!termSig)
	{
		GFXFrame* frame = gfx_renderer_acquire(_t->renderer);
		gfx_frame_submit(frame, 1, (GFXInject[]){ gfx_dep_wait(_t->dep) });
		gfx_heap_purge(_t->heap);
	}
}


/****************************
 * Threading test.
 */
TEST_DESCRIBE(threaded, _t)
{
	// Yeah we're gonna have maniest frames here too.
	gfx_window_set_flags(
		_t->window,
		gfx_window_get_flags(_t->window) | GFX_WINDOW_TRIPLE_BUFFER);

	// Create thread to run the render loop.
	TEST_RUN_THREAD(render_loop);

	// Setup an event loop.
	while (!gfx_window_should_close(_t->window))
		gfx_wait_events();

	// Join the render thread.
	termSig = 1;
	TEST_JOIN(render_loop);
}


/****************************
 * Run the threading test.
 */
TEST_MAIN(threaded);
