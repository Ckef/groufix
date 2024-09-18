/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include <groufix/tools/imgui.h>
#include <float.h>
#include "test.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"


/****************************
 * Focus callback.
 */
static void focus(GFXWindow* window)
{
	ImGuiIO* io = (ImGuiIO*)window->ptr;
	ImGuiIO_AddFocusEvent(io, 1);
}


/****************************
 * Blur callback.
 */
static void blur(GFXWindow* window)
{
	ImGuiIO* io = (ImGuiIO*)window->ptr;
	ImGuiIO_AddFocusEvent(io, 0);
}


/****************************
 * Resize callback.
 */
static void resize(GFXWindow* window, uint32_t width, uint32_t height)
{
	ImGuiIO* io = (ImGuiIO*)window->ptr;
	io->DisplaySize.x = (float)width;
	io->DisplaySize.y = (float)height;
}


/****************************
 * Key press callback.
 */
static void key_press(GFXWindow* window, GFXKey key, int scan, GFXModifier mod)
{
	ImGuiIO* io = (ImGuiIO*)window->ptr;
	ImGuiIO_AddKeyEvent(io, gfx_imgui_key(key), 1);
}


/****************************
 * Key release callback.
 */
static void key_release(GFXWindow* window, GFXKey key, int scan, GFXModifier mod)
{
	ImGuiIO* io = (ImGuiIO*)window->ptr;
	ImGuiIO_AddKeyEvent(io, gfx_imgui_key(key), 0);

	TEST_EVT_KEY_RELEASE(window, key, scan, mod);
}


/****************************
 * Key text callback.
 */
static void key_text(GFXWindow* window, uint32_t codepoint)
{
	ImGuiIO* io = (ImGuiIO*)window->ptr;
	ImGuiIO_AddInputCharacter(io, codepoint);
}


/****************************
 * Mouse leave callback.
 */
static void mouse_leave(GFXWindow* window)
{
	ImGuiIO* io = (ImGuiIO*)window->ptr;
	ImGuiIO_AddMousePosEvent(io, -FLT_MAX, -FLT_MAX);
}


/****************************
 * Mouse move callback.
 */
static void mouse_move(GFXWindow* window, double x, double y)
{
	ImGuiIO* io = (ImGuiIO*)window->ptr;
	ImGuiIO_AddMousePosEvent(io, (float)x, (float)y);
}


/****************************
 * Mouse press callback.
 */
static void mouse_press(GFXWindow* window, GFXMouseButton button, GFXModifier mod)
{
	ImGuiIO* io = (ImGuiIO*)window->ptr;
	ImGuiIO_AddMouseButtonEvent(io, gfx_imgui_button(button), 1);
}


/****************************
 * Mouse release callback.
 */
static void mouse_release(GFXWindow* window, GFXMouseButton button, GFXModifier mod)
{
	ImGuiIO* io = (ImGuiIO*)window->ptr;
	ImGuiIO_AddMouseButtonEvent(io, gfx_imgui_button(button), 0);
}


/****************************
 * Mouse scroll callback.
 */
static void mouse_scroll(GFXWindow* window, double x, double y)
{
	ImGuiIO* io = (ImGuiIO*)window->ptr;
	ImGuiIO_AddMouseWheelEvent(io, (float)x, (float)y);
}


/****************************
 * Callback to render ImGui.
 */
static void render(GFXRecorder* recorder, unsigned int frame, void* ptr)
{
	TEST_CALLBACK_RENDER(recorder, frame, NULL);

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
	t->window->events.focus = focus;
	t->window->events.blur = blur;
	t->window->events.resize = resize;
	t->window->events.key.press = key_press;
	t->window->events.key.release = key_release;
	t->window->events.key.text = key_text;
	t->window->events.mouse.leave = mouse_leave;
	t->window->events.mouse.move = mouse_move;
	t->window->events.mouse.press = mouse_press;
	t->window->events.mouse.release = mouse_release;
	t->window->events.mouse.scroll = mouse_scroll;

	GFXVideoMode mode = gfx_window_get_video(t->window);
	resize(t->window, mode.width, mode.height);

	// Setup ImGui drawer.
	GFXImguiDrawer drawer;
	if (!gfx_imgui_init(&drawer, NULL, t->renderer, t->pass))
		goto clean;

	if (!gfx_imgui_font(&drawer, t->dep, io->Fonts))
		goto clean_drawer;

	// Flush all memory writes.
	if (!gfx_heap_flush(t->heap))
		goto clean_drawer;

	// Setup an event loop.
	// We wait instead of poll, only update when an event was detected.
	while (!gfx_window_should_close(t->window))
	{
		GFXFrame* frame = gfx_renderer_acquire(t->renderer);
		gfx_frame_start(frame);
		gfx_poll_events();

		igNewFrame();
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
