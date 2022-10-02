/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include <groufix/assets/gltf.h>
#include <math.h>

#define TEST_SKIP_CREATE_SCENE
#include "test.h"


/****************************
 * Render callback context.
 */
typedef struct Context
{
	GFXRenderable renderable;
	GFXSet*       set;

} Context;


/****************************
 * Helper to load a shader.
 */
static GFXShader* load_shader(GFXShaderStage stage, const char* uri)
{
	// Open file.
	GFXFile file;
	if (!gfx_file_init(&file, uri, "r"))
		goto error;

	// Init includer.
	GFXFileIncluder inc;
	if (!gfx_file_includer_init(&inc, uri))
		goto clean_file;

	// Create shader.
	GFXShader* shader = gfx_create_shader(stage, TEST_BASE.device);
	if (shader == NULL)
		goto clean_includer;

	// Compile shader.
	if (!gfx_shader_compile(shader,
		GFX_GLSL, 1,
		&file.reader, &inc.includer, NULL, NULL))
	{
		goto clean_shader;
	}

	gfx_file_includer_clear(&inc);
	gfx_file_clear(&file);

	return shader;


	// Cleanup on failure.
clean_shader:
	gfx_destroy_shader(shader);
clean_includer:
	gfx_file_includer_clear(&inc);
clean_file:
	gfx_file_clear(&file);
error:
	gfx_log_error("Failed to load '%s'", uri);
	return NULL;
}


/****************************
 * Helper to load some glTF.
 */
static bool load_gltf(const char* uri, GFXGltfResult* result)
{
	// Open file.
	GFXFile file;
	if (!gfx_file_init(&file, uri, "r"))
		goto error;

	// Init includer.
	GFXFileIncluder inc;
	if (!gfx_file_includer_init(&inc, uri))
		goto clean_file;

	// Load glTF.
	if (!gfx_load_gltf(
		TEST_BASE.heap, TEST_BASE.dep,
		GFX_IMAGE_ANY_FORMAT, GFX_IMAGE_SAMPLED,
		&file.reader, &inc.includer, result))
	{
		goto clean_includer;
	}

	gfx_file_includer_clear(&inc);
	gfx_file_clear(&file);

	return 1;


	// Cleanup on failure.
clean_includer:
	gfx_file_includer_clear(&inc);
clean_file:
	gfx_file_clear(&file);
error:
	gfx_log_error("Failed to load '%s'", uri);
	return 0;
}


/****************************
 * Custom render callback to render the thing.
 */
static void render(GFXRecorder* recorder, unsigned int frame, void* ptr)
{
	// Rotate with some constant factor lol (it's locked to vsync).
	const float pi2 = 6.28318530718f;
	static float rot = 0.0f;
	rot = (rot >= pi2 ? rot - pi2 : rot) + 0.01f;

	// Get aspect ratio.
	uint32_t width;
	uint32_t height;
	uint32_t layers;
	gfx_recorder_get_size(recorder, &width, &height, &layers);

	float invAspect = (width != 0) ? (float)height / (float)width : 1.0f;
	float hCos = cosf(rot);
	float hSin = sinf(rot);

	// Some hardcoded matrices.
	float push[] = {
		// Model-view.
		-0.7f * hCos, 0.7f * hSin, 0.0f, 0.0f,
		 0.0f,        0.0f,        0.7f, 0.0f,
		 0.7f * hSin, 0.7f * hCos, 0.0f, 0.0f,
		 0.0f,        0.0f,        0.0f, 1.0f,

		// Projection.
		invAspect, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f,  0.0f, 0.0f,
		0.0f, 0.0f, -0.5f, 0.7f,
		0.0f, 0.0f,  0.0f, 1.0f,
	};

	// Draw the thing.
	Context* ctx = ptr;
	gfx_cmd_push(recorder, ctx->renderable.technique, 0, sizeof(push), push);
	gfx_cmd_bind(recorder, ctx->renderable.technique, 0, 1, 0, &ctx->set, NULL);
	gfx_cmd_draw_indexed(recorder, &ctx->renderable, 0, 0, 0, 0, 1);
}


/****************************
 * Loading assets test.
 */
TEST_DESCRIBE(loading, t)
{
	bool success = 0;

	// Load a vertex and fragment shader.
	const char* vUri = "tests/shaders/basic.vert";
	const char* fUri = "tests/shaders/basic.frag";
	GFXShader* vert = load_shader(GFX_STAGE_VERTEX, vUri);
	GFXShader* frag = load_shader(GFX_STAGE_FRAGMENT, fUri);

	if (vert == NULL || frag == NULL)
		goto clean;

	// Load a glTF file.
	const char* uri = "tests/assets/DamagedHelmet.gltf";
	GFXGltfResult result;

	if (!load_gltf(uri, &result))
		goto clean;

	// Flush all memory writes.
	if (!gfx_heap_flush(t->heap))
		goto clean;

	// Grab the first primitive & image from the glTF.
	GFXPrimitive* prim =
		result.numPrimitives > 0 ? result.primitives[0] : NULL;
	GFXImage* image =
		result.numImages > 0 ? result.images[0] : NULL;

	gfx_release_gltf(&result);

	// Create a technique and set immutable sampler.
	GFXTechnique* tech = gfx_renderer_add_tech(
		t->renderer, 2, (GFXShader*[]){ vert, frag });

	if (tech == NULL)
		goto clean;

	gfx_tech_immutable(tech, 0, 0); // Warns on fail.

	// Init a renderable & set using the above stuff.
	Context ctx;
	if (!gfx_renderable(&ctx.renderable, t->pass, tech, prim, NULL))
		goto clean;

	ctx.set = gfx_renderer_add_set(t->renderer, tech, 0,
		image != NULL ? 1 : 0, 0, 0, 0,
		(GFXSetResource[]){{
			.binding = 0,
			.index = 0,
			.ref = gfx_ref_image(image)
		}},
		NULL, NULL, NULL);

	if (ctx.set == NULL)
		goto clean;

	// Lastly, setup a depth buffer for our object.
	if (!gfx_renderer_attach(t->renderer, 1,
		(GFXAttachment){
			.type  = GFX_IMAGE_2D,
			.flags = GFX_MEMORY_NONE,
			.usage = GFX_IMAGE_TRANSIENT,

			.format  = GFX_FORMAT_D16_UNORM,
			.mipmaps = 1,
			.layers  = 1,

			.size = GFX_SIZE_RELATIVE,
			.ref = 0,
			.xScale = 1.0f,
			.yScale = 1.0f,
			.zScale = 1.0f
		}))
	{
		goto clean;
	}

	if (!gfx_pass_consume(t->pass, 1,
		GFX_ACCESS_ATTACHMENT_TEST | GFX_ACCESS_DISCARD, GFX_STAGE_ANY))
	{
		goto clean;
	}

	gfx_pass_clear(t->pass, 1,
		GFX_IMAGE_DEPTH, (GFXClear){ .depth = 1.0f });

	// Setup an event loop.
	while (!gfx_window_should_close(t->window))
	{
		GFXFrame* frame = gfx_renderer_acquire(t->renderer);
		gfx_poll_events();
		gfx_frame_start(frame, 1, (GFXInject[]){ gfx_dep_wait(t->dep) });
		gfx_recorder_render(t->recorder, t->pass, render, &ctx);
		gfx_frame_submit(frame);
		gfx_heap_purge(t->heap);
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
