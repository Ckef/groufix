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
	"layout(location = 0) out vec2 fTexCoord;\n"
	"void main() {\n"
	"  vec2 tc = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);\n"
	"  fTexCoord = tc;\n"
	"  gl_Position = vec4(tc * 2.0f + -1.0f, 0.0f, 1.0f);\n"
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
	"  oColor = subpassLoad(iColor).rbra;\n"
	"}\n";

static const char* glsl_post_fragment_blur =
	"#version 450\n"
	"layout(set = 0, binding = 0) uniform sampler2D inputTex;\n"
	"layout(push_constant) uniform Constants { vec2 invSize; };\n"
	"layout(location = 0) in vec2 fTexCoord;\n"
	"layout(location = 0) out vec4 oColor;\n"
	"const int M = 16;\n"
	"const int N = 2 * M + 1;\n"
	"const float coeffs[N] = float[N](\n"
	"  0.012318109844189502,\n"
	"  0.014381474814203989,\n"
	"  0.016623532195728208,\n"
	"  0.019024086115486723,\n"
	"  0.02155484948872149,\n"
	"  0.02417948052890078,\n"
	"  0.02685404941667096,\n"
	"  0.0295279624870386,\n"
	"  0.03214534135442581,\n"
	"  0.03464682117793548,\n"
	"  0.0369716985390341,\n"
	"  0.039060328279673276,\n"
	"  0.040856643282313365,\n"
	"  0.04231065439216247,\n"
	"  0.043380781642569775,\n"
	"  0.044035873841196206,\n"
	"  0.04425662519949865,\n"
	"  0.044035873841196206,\n"
	"  0.043380781642569775,\n"
	"  0.04231065439216247,\n"
	"  0.040856643282313365,\n"
	"  0.039060328279673276,\n"
	"  0.0369716985390341,\n"
	"  0.03464682117793548,\n"
	"  0.03214534135442581,\n"
	"  0.0295279624870386,\n"
	"  0.02685404941667096,\n"
	"  0.02417948052890078,\n"
	"  0.02155484948872149,\n"
	"  0.019024086115486723,\n"
	"  0.016623532195728208,\n"
	"  0.014381474814203989,\n"
	"  0.012318109844189502\n"
	");\n"
	"void main() {\n"
	"  vec4 sum = vec4(0.0);\n"
	"  for (int i = 0; i < N; ++i) {\n"
	"    for (int j = 0; j < N; ++j) {\n"
	"      vec2 tc = fTexCoord + invSize * vec2(float(i - M), float(j - M));\n"
	"      sum += coeffs[i] * coeffs[i] * texture(inputTex, tc);\n"
	"    }\n"
	"  }\n"
	"  oColor = sum;\n"
	"}\n";


/****************************
 * Render callback context.
 */
typedef struct Context
{
	unsigned int  mode;
	GFXRenderable renderables[3];
	GFXSet*       sets[3];

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
		gfx_renderer_cull(TEST_BASE.renderer, 3);
		ctx->mode = 0;
		break;
	case GFX_KEY_2:
		gfx_renderer_cull(TEST_BASE.renderer, 1);
		gfx_renderer_uncull(TEST_BASE.renderer, 2);
		gfx_renderer_cull(TEST_BASE.renderer, 3);
		ctx->mode = 1;
		break;
	case GFX_KEY_3:
		gfx_renderer_cull(TEST_BASE.renderer, 1);
		gfx_renderer_cull(TEST_BASE.renderer, 2);
		gfx_renderer_uncull(TEST_BASE.renderer, 3);
		ctx->mode = 2;
		break;
	default:
		break;
	}

	return TEST_EVT_KEY_RELEASE(window, key, scan, mod, data);
}


/****************************
 * Post-processing render callback.
 */
static void post_process(GFXRecorder* recorder, void* ptr)
{
	uint32_t width;
	uint32_t height;
	uint32_t layers;
	gfx_recorder_get_size(recorder, &width, &height, &layers);

	const float push[] = { 1.0f / (float)width, 1.0f / (float)height };

	// Draw a triangle.
	Context* ctx = ptr;
	GFXRenderable* renderable = &ctx->renderables[ctx->mode];
	GFXSet* set = ctx->sets[ctx->mode];

	if (gfx_tech_get_push_size(renderable->technique) > 0)
		gfx_cmd_push(recorder, renderable->technique, 0, sizeof(push), push);

	gfx_cmd_bind(recorder, renderable->technique, 0, 1, 0, &set, NULL);
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
		gfx_create_shader(GFX_STAGE_FRAGMENT, t->device),
		gfx_create_shader(GFX_STAGE_FRAGMENT, t->device)
	};

	if (vert == NULL || frags[0] == NULL || frags[1] == NULL || frags[2] == NULL)
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

	if (!gfx_shader_compile(frags[2], GFX_GLSL, 1,
		gfx_string_reader(&str, glsl_post_fragment_blur), NULL, NULL, NULL))
	{
		goto clean;
	}

	// Setup an intermediate output attachment.
	if (!gfx_renderer_attach(t->renderer, 1,
		(GFXAttachment){
			.type  = GFX_IMAGE_2D,
			.flags = GFX_MEMORY_NONE,
			.usage = GFX_IMAGE_OUTPUT | GFX_IMAGE_INPUT | GFX_IMAGE_SAMPLED,

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
			1, (GFXPass*[]){ t->pass }),
		gfx_renderer_add_pass(
			t->renderer, GFX_PASS_RENDER,
			3, // Group 3.
			1, (GFXPass*[]){ t->pass })
	};

	if (posts[0] == NULL || posts[1] == NULL || posts[2] == NULL)
		goto clean;

	// Move the window to the second passes, the intermediate to the first.
	gfx_pass_release(t->pass, 0);

	if (!gfx_pass_consume(t->pass, 1,
		GFX_ACCESS_ATTACHMENT_WRITE, GFX_STAGE_ANY))
	{
		goto clean;
	}

	if (
		!gfx_pass_consume(posts[0], 1,
			GFX_ACCESS_ATTACHMENT_INPUT | GFX_ACCESS_DISCARD, GFX_STAGE_ANY) ||
		!gfx_pass_consume(posts[0], 0,
			GFX_ACCESS_ATTACHMENT_WRITE, GFX_STAGE_ANY))
	{
		goto clean;
	}

	if (
		!gfx_pass_consume(posts[1], 1,
			GFX_ACCESS_ATTACHMENT_INPUT | GFX_ACCESS_DISCARD, GFX_STAGE_ANY) ||
		!gfx_pass_consume(posts[1], 0,
			GFX_ACCESS_ATTACHMENT_WRITE, GFX_STAGE_ANY))
	{
		goto clean;
	}

	if (
		!gfx_pass_consume(posts[2], 1,
			GFX_ACCESS_SAMPLED_READ | GFX_ACCESS_DISCARD, GFX_STAGE_ANY) ||
		!gfx_pass_consume(posts[2], 0,
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
			t->renderer, 2, (GFXShader*[]){ vert, frags[1] }),
		gfx_renderer_add_tech(
			t->renderer, 2, (GFXShader*[]){ vert, frags[2] })
	};

	if (techs[0] == NULL || techs[1] == NULL || techs[2] == NULL)
		goto clean;

	if (!gfx_tech_lock(techs[0]) || !gfx_tech_lock(techs[1]) || !gfx_tech_lock(techs[2]))
		goto clean;

	// Init renderables & set using the above stuff.
	if (!gfx_renderable(&ctx.renderables[0], posts[0], techs[0], NULL, NULL))
		goto clean;

	if (!gfx_renderable(&ctx.renderables[1], posts[1], techs[1], NULL, NULL))
		goto clean;

	if (!gfx_renderable(&ctx.renderables[2], posts[2], techs[2], NULL, NULL))
		goto clean;

	ctx.sets[0] = ctx.sets[1] = gfx_renderer_add_set(t->renderer, techs[0], 0,
		1, 0, 0, 0,
		(GFXSetResource[]){{
			.binding = 0,
			.index = 0,
			.ref = gfx_ref_attach(t->renderer, 1)
		}},
		NULL, NULL, NULL);

	if (ctx.sets[0] == NULL)
		goto clean;

	ctx.sets[2] = gfx_renderer_add_set(t->renderer, techs[2], 0,
		1, 0, 0, 0,
		(GFXSetResource[]){{
			.binding = 0,
			.index = 0,
			.ref = gfx_ref_attach(t->renderer, 1)
		}},
		NULL, NULL, NULL);

	if (ctx.sets[2] == NULL)
		goto clean;

	// Set initial state.
	gfx_renderer_cull(t->renderer, 1);
	gfx_renderer_cull(t->renderer, 2);
	ctx.mode = 2;

	// Setup an event loop.
	// We wait instead of poll, only update when an event was detected.
	while (!gfx_window_should_close(t->window))
	{
		GFXFrame* frame = gfx_renderer_start(t->renderer);
		GFXPass* post = posts[ctx.mode];

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
	gfx_destroy_shader(frags[2]);

	if (!success) TEST_FAIL();
}


/****************************
 * Run the post-processing test.
 */
TEST_MAIN(post);
