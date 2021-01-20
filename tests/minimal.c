/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include <groufix.h>
#include <stdio.h>
#include <stdlib.h>


/****************************/
void key_release(GFXWindow* window, GFXKey key, int scan, GFXModifier mod)
{
	switch (key)
	{
	// Toggle fullscreen on F11.
	case GFX_KEY_F11:
		if (gfx_window_get_monitor(window) != NULL)
		{
			gfx_window_set_monitor(
				window, NULL,
				(GFXVideoMode){ .width = 600, .height = 400 });
		}
		else
		{
			GFXMonitor* monitor = gfx_get_primary_monitor();
			gfx_window_set_monitor(
				window, monitor,
				gfx_monitor_get_current_mode(monitor));
		}
		break;

	// Close on escape.
	case GFX_KEY_ESCAPE:
		gfx_window_set_close(window, 1);
		break;

	default:
		break;
	}
}

/****************************/
int main()
{
	// Initialize.
	if (!gfx_init())
		goto fail;


	/////////////////////////
	// Create a window.
	GFXWindow* window = gfx_create_window(
		GFX_WINDOW_RESIZABLE | GFX_WINDOW_DOUBLE_BUFFER,
		NULL, NULL, (GFXVideoMode){ .width = 600, .height = 400 }, "groufix");

	if (window == NULL)
		goto fail;

	// Register key release event.
	window->events.key.release = key_release;

	// Create a renderer and add a single render pass.
	GFXRenderer* renderer = gfx_create_renderer(NULL);
	if (renderer == NULL)
		goto fail_renderer;

	if (!gfx_renderer_attach_window(renderer, 0, window))
		goto fail_renderer;

	GFXRenderPass* pass = gfx_renderer_add(renderer, 0, NULL);
	if (pass == NULL)
		goto fail_renderer;

	if (!gfx_render_pass_write(pass, 0))
		goto fail_renderer;


	/////////////////////////
	// Compile a shader.
	const char src[] =
		"#version 310 es\n"
		"#define SOME_MACRO 42\n"
		"void main() { int x = SOME_MACRO; }";

	GFXShader* shader = gfx_create_shader(NULL, src);
	if (shader == NULL)
		goto fail_shader;


	/////////////////////////
	// Setup an event loop.
	while (!gfx_window_should_close(window))
	{
		gfx_renderer_submit(renderer);
		gfx_wait_events();
	}


	/////////////////////////
	// Terminate.
	gfx_destroy_shader(shader);
	gfx_destroy_renderer(renderer);
	gfx_destroy_window(window);
	gfx_terminate();

	puts("Success!");
	exit(EXIT_SUCCESS);


	// On failure.
fail_shader:
	gfx_destroy_shader(shader);
fail_renderer:
	gfx_destroy_renderer(renderer);
	gfx_destroy_window(window);
fail:
	gfx_terminate();

	puts("Failure!");
	exit(EXIT_FAILURE);
}
