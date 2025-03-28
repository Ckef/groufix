/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include <groufix/tools/imgui.h>
#include "test.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"


/****************************
 * Callback to render ImGui.
 */
static void render(GFXRecorder* recorder, void* ptr)
{
	TEST_CALLBACK_RENDER(recorder, NULL);

	GFXImguiDrawer* drawer = ptr;
	gfx_cmd_draw_imgui(recorder, drawer, igGetDrawData());
}


/****************************
 * Dear ImGui test.
 */
TEST_DESCRIBE(imgui, t)
{
	bool success = 0;

	// Setup ImGui.
	ImGuiContext* context = igCreateContext(NULL);
	ImGuiIO* io = igGetIO();

	// Setup ImGui input.
	GFXImguiInput input;
	if (!gfx_imgui_input(&input, t->window, io, 1))
		goto clean;

	// Setup ImGui drawer.
	GFXImguiDrawer drawer;
	if (!gfx_imgui_init(&drawer, t->renderer, t->pass, NULL))
		goto clean_input;

	if (!gfx_imgui_font(&drawer, t->dep, io->Fonts))
		goto clean_drawer;

	// Flush all memory writes.
	if (!gfx_heap_flush(t->heap))
		goto clean_drawer;

	// Setup an event loop.
	while (!gfx_window_should_close(t->window))
	{
		GFXFrame* frame = gfx_renderer_start(t->renderer);
		gfx_poll_events();

		igNewFrame();
		igShowDemoWindow(NULL);
		igRender();

		gfx_recorder_render(t->recorder, t->pass, render, &drawer);
		gfx_frame_submit(frame);
	}

	success = 1;


	// Cleanup.
clean_drawer:
	gfx_renderer_block(t->renderer);
	gfx_imgui_clear(&drawer);
clean_input:
	gfx_imgui_end(&input);
clean:
	igDestroyContext(context);

	if (!success) TEST_FAIL();
}


/****************************
 * Run the Dear ImGui test.
 */
TEST_MAIN(imgui);
