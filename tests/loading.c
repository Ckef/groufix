/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include <groufix/assets/gltf.h>

#define TEST_SKIP_CREATE_SCENE
#include "test.h"


/****************************
 * Loading assets test.
 */
TEST_DESCRIBE(loading, _t)
{
	// Load a glTF file.
	const char* uri = "tests/assets/DamagedHelmet.gltf";
	GFXFile file;
	GFXGltfResult result;

	if (!gfx_file_init(&file, uri, "r"))
	{
		gfx_log_error("Failed to load '%s'.", uri);
		TEST_FAIL();
	}

	if (!gfx_load_gltf(_t->heap, _t->dep, &file.reader, &result))
		TEST_FAIL();

	gfx_file_clear(&file);

	// TODO: Make stuff to render the thing.

	// Setup an event loop.
	while (!gfx_window_should_close(_t->window))
	{
		GFXFrame* frame = gfx_renderer_acquire(_t->renderer);
		gfx_poll_events();
		gfx_frame_start(frame, 1, (GFXInject[]){ gfx_dep_wait(_t->dep) });
		gfx_frame_submit(frame);
		gfx_heap_purge(_t->heap);
	}

	// Get rid of the glTF result.
	gfx_release_gltf(&result);
}


/****************************
 * Run the loading test.
 */
TEST_MAIN(loading);
