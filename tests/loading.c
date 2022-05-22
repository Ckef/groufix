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
 * Helper to load a shader.
 */
static GFXShader* test_load_shader(GFXShaderStage stage, const char* uri)
{
	// Open file.
	GFXFile file;
	if (!gfx_file_init(&file, uri, "r"))
		goto error;

	// Create shader.
	GFXShader* shader = gfx_create_shader(stage, TEST_BASE.device);
	if (shader == NULL)
	{
		gfx_file_clear(&file);
		goto error;
	}

	// Compile shader.
	if (!gfx_shader_compile(shader, GFX_GLSL, 1, &file.reader, NULL, NULL))
	{
		gfx_file_clear(&file);
		gfx_destroy_shader(shader);
		goto error;
	}

	gfx_file_clear(&file);

	return shader;


	// Error on failure.
error:
	gfx_log_error("Failed to load '%s'", uri);
	return NULL;
}


/****************************
 * Helper to load some glTF.
 */
static bool test_load_gltf(const char* uri, GFXGltfResult* result)
{
	// Open file.
	GFXFile file;
	if (!gfx_file_init(&file, uri, "r"))
		goto error;

	// Load glTF.
	if (!gfx_load_gltf(TEST_BASE.heap, TEST_BASE.dep, &file.reader, result))
	{
		gfx_file_clear(&file);
		goto error;
	}

	gfx_file_clear(&file);

	return 1;


	// Error on failure.
error:
	gfx_log_error("Failed to load '%s'", uri);
	return 0;
}


/****************************
 * Custom render callback to render the thing.
 */
static void test_render(GFXRecorder* recorder, unsigned int frame, void* ptr)
{
	gfx_cmd_draw_indexed(recorder, (GFXRenderable*)ptr, 0, 0, 0, 0, 1);
}


/****************************
 * Loading assets test.
 */
TEST_DESCRIBE(loading, _t)
{
	bool success = 0;

	// Load a vertex and fragment shader.
	const char* vUri = "tests/shaders/basic.vert";
	const char* fUri = "tests/shaders/basic.frag";
	GFXShader* vert = test_load_shader(GFX_STAGE_VERTEX, vUri);
	GFXShader* frag = test_load_shader(GFX_STAGE_FRAGMENT, fUri);

	if (vert == NULL || frag == NULL)
		goto clean;

	// Load a glTF file.
	const char* uri = "tests/assets/DamagedHelmet.gltf";
	GFXGltfResult result;

	if (!test_load_gltf(uri, &result))
		goto clean;

	// Grab the first primitive from the glTF.
	GFXPrimitive* prim =
		result.numPrimitives > 0 ? result.primitives[0] : NULL;

	gfx_release_gltf(&result);

	// Create a technique & lock it.
	GFXTechnique* tech = gfx_renderer_add_tech(
		_t->renderer, 2, (GFXShader*[]){ vert, frag });

	if (tech == NULL || !gfx_tech_lock(tech))
		goto clean;

	// Init a renderable using the above stuff.
	GFXRenderable rend;
	gfx_renderable(&rend, _t->pass, tech, prim);

	// Setup an event loop.
	while (!gfx_window_should_close(_t->window))
	{
		GFXFrame* frame = gfx_renderer_acquire(_t->renderer);
		gfx_poll_events();
		gfx_frame_start(frame, 1, (GFXInject[]){ gfx_dep_wait(_t->dep) });
		gfx_recorder_render(_t->recorder, _t->pass, test_render, &rend);
		gfx_frame_submit(frame);
		gfx_heap_purge(_t->heap);
	}

	success = 1;


	// Cleanup.
clean:
	gfx_destroy_shader(vert);
	gfx_destroy_shader(frag);

	if (!success) TEST_FAIL();
}


/****************************
 * Run the loading test.
 */
TEST_MAIN(loading);
