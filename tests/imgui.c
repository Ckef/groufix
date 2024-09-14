/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include <groufix/tools/imgui.h>

#define TEST_SKIP_CREATE_SCENE
#include "test.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"


/****************************
 * Resize callback to resize the ImGui display size.
 */
static void resize(GFXWindow* window, uint32_t width, uint32_t height)
{
	ImGuiIO* io = (ImGuiIO*)window->ptr;
	io->DisplaySize.x = (float)width;
	io->DisplaySize.y = (float)height;
}


/****************************
 * Callback to render ImGui.
 */
static void render(GFXRecorder* recorder, unsigned int frame, void* ptr)
{
	GFXImguiDrawer* drawer = (GFXImguiDrawer*)ptr;
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
	t->window->ptr = io;
	t->window->events.resize = resize;

	GFXVideoMode mode = gfx_window_get_video(t->window);
	resize(t->window, mode.width, mode.height);

	// Setup ImGui drawer.
	GFXImguiDrawer drawer;
	if (!gfx_imgui_init(&drawer, NULL, t->dep, t->renderer, t->pass))
		goto clean;

	if (!gfx_imgui_font(&drawer, io->Fonts))
		goto clean_drawer;

	// TODO: Somehow make the drawer do this?
	// TODO: Make the color/alpha op state work for renderables too?
	GFXBlendState blend = {
		.logic = GFX_LOGIC_NO_OP,
		.color = {
			.srcFactor = GFX_FACTOR_SRC_ALPHA,
			.dstFactor = GFX_FACTOR_ONE_MINUS_SRC_ALPHA,
			.op = GFX_BLEND_ADD
		},
		.alpha = {
			.srcFactor = GFX_FACTOR_ONE,
			.dstFactor = GFX_FACTOR_ONE_MINUS_SRC_ALPHA,
			.op = GFX_BLEND_ADD
		}
	};

	gfx_pass_set_state(t->pass, (GFXRenderState){
		.raster = NULL,
		.blend = &blend,
		.depth = NULL,
		.stencil = NULL
	});

	// Flush all memory writes.
	if (!gfx_heap_flush(t->heap))
		goto clean_drawer;

	// Setup an event loop.
	// We wait instead of poll, only update when an event was detected.
	while (!gfx_window_should_close(t->window))
	{
		GFXFrame* frame = gfx_renderer_acquire(t->renderer);
		gfx_frame_start(frame);
		igNewFrame();
		gfx_poll_events();
		igShowDemoWindow(NULL);
		igRender();
		gfx_pass_inject(t->pass, 1, (GFXInject[]){ gfx_dep_wait(t->dep) });
		gfx_recorder_render(t->recorder, t->pass, render, &drawer);
		gfx_frame_submit(frame);
	}

	success = 1;


	// Cleanup.
clean_drawer:
	gfx_renderer_block(t->renderer);
	gfx_imgui_clear(&drawer);
clean:
	igDestroyContext(context);

	if (!success) TEST_FAIL();
}


/****************************
 * Run the Dear ImGui test.
 */
TEST_MAIN(imgui);
