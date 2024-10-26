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

static const char* glsl_post_fragment_invert =
	"#version 450\n"
	"layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput iColor;\n"
	"layout(location = 0) out vec4 oColor;\n"
	"void main() {\n"
	"  oColor = vec4(1.0f) - subpassLoad(iColor).rgba;\n"
	"}\n";

static const char* glsl_post_fragment_shuffle =
	"#version 450\n"
	"layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput iColor;\n"
	"layout(location = 0) out vec4 oColor;\n"
	"void main() {\n"
	"  oColor = subpassLoad(iColor).gbra;\n"
	"}\n";


/****************************
 * Render callback context.
 */
typedef struct Context
{
	unsigned int  mode;
	GFXRenderable renderables[2];
	GFXSet*       set;

} Context;


/****************************
 * Switch post-processing mode on key-release.
 */
static bool post_key_release(GFXWindow* window,
                             GFXKey key, int scan, GFXModifier mod, void* data)
{
	Context* ctx = window->ptr;

	switch (key)
	{
	case GFX_KEY_1:
		gfx_renderer_uncull(TEST_BASE.renderer, 1);
		gfx_renderer_cull(TEST_BASE.renderer, 2);
		ctx->mode = 0;
		break;
	case GFX_KEY_2:
		gfx_renderer_cull(TEST_BASE.renderer, 1);
		gfx_renderer_uncull(TEST_BASE.renderer, 2);
		ctx->mode = 1;
		break;
	default:
		break;
	}

	return TEST_EVT_KEY_RELEASE(window, key, scan, mod, data);
}


/****************************
 * Post-processing render callback.
 */
static void post_process(GFXRecorder* recorder, unsigned int frame, void* ptr)
{
	// Draw a triangle.
	Context* ctx = ptr;
	GFXRenderable* renderable = &ctx->renderables[ctx->mode];
	gfx_cmd_bind(recorder, renderable->technique, 0, 1, 0, &ctx->set, NULL);
	gfx_cmd_draw(recorder, renderable, 3, 1, 0, 0);
}


/****************************
 * Post-processing test.
 */
TEST_DESCRIBE(post, t)
{
	bool success = 0;

	Context ctx;

	// Register post-processing key events.
	t->window->ptr = &ctx;
	t->window->events.key.release = post_key_release;

	// Create some post-processing shaders.
	GFXShader* vert =
		gfx_create_shader(GFX_STAGE_VERTEX, t->device);
	GFXShader* frags[] = {
		gfx_create_shader(GFX_STAGE_FRAGMENT, t->device),
		gfx_create_shader(GFX_STAGE_FRAGMENT, t->device)
	};

	if (vert == NULL || frags[0] == NULL || frags[1] == NULL)
		goto clean;

	// Compile GLSL into the shaders.
	GFXStringReader str;

	if (!gfx_shader_compile(vert, GFX_GLSL, 1,
		gfx_string_reader(&str, glsl_post_vertex), NULL, NULL, NULL))
	{
		goto clean;
	}

	if (!gfx_shader_compile(frags[0], GFX_GLSL, 1,
		gfx_string_reader(&str, glsl_post_fragment_invert), NULL, NULL, NULL))
	{
		goto clean;
	}

	if (!gfx_shader_compile(frags[1], GFX_GLSL, 1,
		gfx_string_reader(&str, glsl_post_fragment_shuffle), NULL, NULL, NULL))
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
			.samples = 1,
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

	// Add post-processing passes.
	// Really you don't need multiple passes to switch between renderables,
	// but where doing it here anyway, as a proof of implementation.
	// In fact it is quite inefficient to do it this way,
	// as when you change culling state, the render graph is rebuilt.
	// If we instead just pick a different renderable to render with,
	// absolutely nothing needs to be built and/or rebuilt.
	GFXPass* posts[] = {
		gfx_renderer_add_pass(
			t->renderer, GFX_PASS_RENDER,
			1, // Group 1.
			1, (GFXPass*[]){ t->pass }),
		gfx_renderer_add_pass(
			t->renderer, GFX_PASS_RENDER,
			2, // Group 2.
			1, (GFXPass*[]){ t->pass })
	};

	if (posts[0] == NULL || posts[1] == NULL)
		goto clean;

	// Move the window to the second passes, the intermediate to the first.
	gfx_pass_release(t->pass, 0);

	if (!gfx_pass_consume(t->pass, 1,
		GFX_ACCESS_ATTACHMENT_WRITE, GFX_STAGE_ANY))
	{
		goto clean;
	}

	if (!gfx_pass_consume(posts[0], 1,
		GFX_ACCESS_ATTACHMENT_INPUT, GFX_STAGE_ANY))
	{
		goto clean;
	}

	if (!gfx_pass_consume(posts[1], 1,
		GFX_ACCESS_ATTACHMENT_INPUT, GFX_STAGE_ANY))
	{
		goto clean;
	}

	if (!gfx_pass_consume(posts[0], 0,
		GFX_ACCESS_ATTACHMENT_WRITE, GFX_STAGE_ANY))
	{
		goto clean;
	}

	if (!gfx_pass_consume(posts[1], 0,
		GFX_ACCESS_ATTACHMENT_WRITE, GFX_STAGE_ANY))
	{
		goto clean;
	}

	gfx_pass_clear(t->pass, 1,
		GFX_IMAGE_COLOR, (GFXClear){{ 0.0f, 0.0f, 0.0f, 0.0f }});

	// Create the techniques.
	GFXTechnique* techs[] = {
		gfx_renderer_add_tech(
			t->renderer, 2, (GFXShader*[]){ vert, frags[0] }),
		gfx_renderer_add_tech(
			t->renderer, 2, (GFXShader*[]){ vert, frags[1] })
	};

	if (techs[0] == NULL || techs[1] == NULL)
		goto clean;

	if (!gfx_tech_lock(techs[0]) || !gfx_tech_lock(techs[1]))
		goto clean;

	// Init renderables & set using the above stuff.
	if (!gfx_renderable(&ctx.renderables[0], posts[0], techs[0], NULL, NULL))
		goto clean;

	if (!gfx_renderable(&ctx.renderables[1], posts[1], techs[1], NULL, NULL))
		goto clean;

	ctx.set = gfx_renderer_add_set(t->renderer, techs[0], 0,
		1, 0, 0, 0,
		(GFXSetResource[]){{
			.binding = 0,
			.index = 0,
			.ref = gfx_ref_attach(t->renderer, 1)
		}},
		NULL, NULL, NULL);

	if (ctx.set == NULL)
		goto clean;

	// Set initial state.
	gfx_renderer_cull(t->renderer, 2);
	ctx.mode = 0;

	// Setup an event loop.
	// We wait instead of poll, only update when an event was detected.
	while (!gfx_window_should_close(t->window))
	{
		GFXFrame* frame = gfx_renderer_acquire(t->renderer);
		GFXPass* post = posts[ctx.mode];

		gfx_frame_start(frame);
		gfx_recorder_render(t->recorder, post, post_process, &ctx);
		gfx_recorder_render(t->recorder, t->pass, TEST_CALLBACK_RENDER, NULL);
		gfx_frame_submit(frame);
		gfx_wait_events();
	}

	success = 1;


	// Cleanup.
clean:
	gfx_destroy_shader(vert);
	gfx_destroy_shader(frags[0]);
	gfx_destroy_shader(frags[1]);

	if (!success) TEST_FAIL();
}


/****************************
 * Run the post-processing test.
 */
TEST_MAIN(post);
