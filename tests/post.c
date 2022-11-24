/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "test.h"


/****************************
 * Post-processing shaders.
 */
static const char* glsl_post_vertex =
	"#version 450\n"
	"void main() {\n"
	"  vec2 fTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);\n"
	"  gl_Position = vec4(fTexCoord * 2.0f + -1.0f, 0.0f, 1.0f);\n"
	"}\n";

static const char* glsl_post_fragment =
	"#version 450\n"
	"layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput iColor;\n"
	"layout(location = 0) out vec4 oColor;\n"
	"void main() {\n"
	"  oColor = vec4(1.0f) - subpassLoad(iColor).rgba;\n"
	"}\n";


/****************************
 * Render callback context.
 */
typedef struct Context
{
	GFXRenderable renderable;
	GFXSet*       set;

} Context;


/****************************
 * Post-processing render callback.
 */
static void post_process(GFXRecorder* recorder, unsigned int frame, void* ptr)
{
	// Draw a triangle.
	Context* ctx = ptr;
	gfx_cmd_bind(recorder, ctx->renderable.technique, 0, 1, 0, &ctx->set, NULL);
	gfx_cmd_draw(recorder, &ctx->renderable, 0, 3, 0, 1);
}


/****************************
 * Post-processing test.
 */
TEST_DESCRIBE(post, t)
{
	bool success = 0;

	// Create some post-processing shaders.
	GFXShader* vert = gfx_create_shader(GFX_STAGE_VERTEX, t->device);
	GFXShader* frag = gfx_create_shader(GFX_STAGE_FRAGMENT, t->device);

	if (vert == NULL || frag == NULL)
		goto clean;

	// Compile GLSL into the shaders.
	GFXStringReader str;

	if (!gfx_shader_compile(vert, GFX_GLSL, 1,
		gfx_string_reader(&str, glsl_post_vertex), NULL, NULL, NULL))
	{
		goto clean;
	}

	if (!gfx_shader_compile(frag, GFX_GLSL, 1,
		gfx_string_reader(&str, glsl_post_fragment), NULL, NULL, NULL))
	{
		goto clean;
	}

	// Setup an intermediate output attachment.
	if (!gfx_renderer_attach(t->renderer, 1,
		(GFXAttachment){
			.type  = GFX_IMAGE_2D,
			.flags = GFX_MEMORY_NONE,
			.usage = GFX_IMAGE_OUTPUT | GFX_IMAGE_INPUT,

			.format  = GFX_FORMAT_B8G8R8A8_SRGB,
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

	// Add post-processing pass.
	GFXPass* post = gfx_renderer_add_pass(
		t->renderer, 1, (GFXPass*[]){ t->pass });

	if (post == NULL)
		goto clean;

	// Move the window to the second pass, the intermediate to the first.
	gfx_pass_release(t->pass, 0);

	if (!gfx_pass_consume(t->pass, 1,
		GFX_ACCESS_ATTACHMENT_WRITE, GFX_STAGE_ANY))
	{
		goto clean;
	}

	if (!gfx_pass_consume(post, 1,
		GFX_ACCESS_ATTACHMENT_INPUT, GFX_STAGE_ANY))
	{
		goto clean;
	}

	if (!gfx_pass_consume(post, 0,
		GFX_ACCESS_ATTACHMENT_WRITE, GFX_STAGE_ANY))
	{
		goto clean;
	}

	gfx_pass_clear(t->pass, 1,
		GFX_IMAGE_COLOR, (GFXClear){{ 0.0f, 0.0f, 0.0f, 0.0f }});

	// Create a technique.
	GFXTechnique* tech = gfx_renderer_add_tech(
		t->renderer, 2, (GFXShader*[]){ vert, frag });

	if (tech == NULL)
		goto clean;

	// Init a renderable & set using the above stuff.
	Context ctx;
	if (!gfx_renderable(&ctx.renderable, post, tech, NULL, NULL))
		goto clean;

	ctx.set = gfx_renderer_add_set(t->renderer, tech, 0,
		1, 0, 0, 0,
		(GFXSetResource[]){{
			.binding = 0,
			.index = 0,
			.ref = gfx_ref_attach(t->renderer, 1)
		}},
		NULL, NULL, NULL);

	if (ctx.set == NULL)
		goto clean;

	// Setup an event loop.
	// We wait instead of poll, only update when an event was detected.
	while (!gfx_window_should_close(t->window))
	{
		GFXFrame* frame = gfx_renderer_acquire(t->renderer);
		gfx_frame_start(frame, 1, (GFXInject[]){ gfx_dep_wait(t->dep) });
		gfx_recorder_render(t->recorder, post, post_process, &ctx);
		gfx_recorder_render(t->recorder, t->pass, TEST_CALLBACK_RENDER, NULL);
		gfx_frame_submit(frame);
		gfx_heap_purge(t->heap);
		gfx_wait_events();
	}

	success = 1;


	// Cleanup.
clean:
	gfx_destroy_shader(vert);
	gfx_destroy_shader(frag);

	if (!success) TEST_FAIL();
}


/****************************
 * Run the post-processing test.
 */
TEST_MAIN(post);
