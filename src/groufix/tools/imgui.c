/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/tools/imgui.h"
#include <assert.h>

#define CIMGUI_NO_EXPORT
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"


/****************************/
GFX_API bool gfx_imgui_init(GFXImguiDrawer* drawer,
                            GFXHeap* heap, GFXDependency* dep,
                            GFXRenderer* renderer, GFXPass* pass)
{
	assert(drawer != NULL);
	assert(dep != NULL);
	assert(renderer != NULL);
	assert(pass != NULL);
	assert(gfx_pass_get_renderer(pass) == renderer);
	assert(gfx_pass_get_type(pass) == GFX_PASS_RENDER);

	// User the renderer's heap if none is given.
	if (heap == NULL)
		heap = gfx_renderer_get_heap(renderer);

	drawer->heap = heap;
	drawer->dep = dep;
	drawer->renderer = renderer;
	drawer->pass = pass;

	// TODO: Implement.

	return 0;
}

/****************************/
GFX_API void gfx_imgui_clear(GFXImguiDrawer* drawer)
{
	assert(drawer != NULL);

	// TODO: Implement.
}

/****************************/
GFX_API void gfx_cmd_draw_imgui(GFXRecorder* recorder,
                                GFXImguiDrawer* drawer, const void* igDrawData)
{
	assert(recorder != NULL);
	assert(drawer != NULL);
	assert(gfx_recorder_get_renderer(recorder) == drawer->renderer);
	assert(igDrawData != NULL);

	// TODO: Implement.
}
