/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include <string.h>

#define TEST_SKIP_CREATE_WINDOW
#define TEST_NUM_FRAMES 1
#include "test.h"


/****************************
 * Compute shader.
 */
static const char* glsl_compute =
	"#version 450\n"
	"layout(set = 0, binding = 0, std430) buffer Values {\n"
	"  float values[];\n"
	"};\n"
	"void main() {\n"
	"  float currVal = values[gl_GlobalInvocationID.x];\n"
	"  values[gl_GlobalInvocationID.x] = currVal * 2.0f;\n"
	"}\n";


/****************************
 * Compute callback context.
 */
typedef struct Context
{
	GFXComputable computable;
	GFXSet*       set;

} Context;


/****************************
 * Compute callback.
 */
static void compute(GFXRecorder* recorder, unsigned int frame, void* ptr)
{
	// Dispatch the compute shader.
	Context* ctx = ptr;
	gfx_cmd_bind(recorder, ctx->computable.technique, 0, 1, 0, &ctx->set, NULL);
	gfx_cmd_dispatch(recorder, &ctx->computable, 4, 1, 1);
}


/****************************
 * Compute test.
 */
TEST_DESCRIBE(compute, t)
{
	bool success = 0;

	// Create a compute shader.
	GFXShader* comp = gfx_create_shader(GFX_STAGE_COMPUTE, t->device);
	if (comp == NULL)
		goto clean;

	// Compile GLSL into the shader.
	GFXStringReader str;
	if (!gfx_shader_compile(comp, GFX_GLSL, 1,
		gfx_string_reader(&str, glsl_compute), NULL, NULL, NULL))
	{
		goto clean;
	}

	// Allocate a buffer with some values.
	const float values[] = { 0.5f, 0.1f, 0.6f, 3.1f };
	const float result[] = { 1.0f, 0.2f, 1.2f, 6.2f }; // Expected result.

	GFXBuffer* buffer = gfx_alloc_buffer(t->heap,
		GFX_MEMORY_HOST_VISIBLE | GFX_MEMORY_DEVICE_LOCAL,
		GFX_BUFFER_STORAGE, sizeof(values));

	if (buffer == NULL)
		goto clean;

	float* buffPtr = gfx_map(gfx_ref_buffer(buffer));
	memcpy(buffPtr, values, sizeof(values));

	// Add compute pass.
	GFXPass* pass = gfx_renderer_add_pass(
		t->renderer, GFX_PASS_COMPUTE_ASYNC, 0, NULL);

	if (pass == NULL)
		goto clean;

	// Create a technique.
	GFXTechnique* tech = gfx_renderer_add_tech(
		t->renderer, 1, (GFXShader*[]){ comp });

	if (tech == NULL)
		goto clean;

	// Init a computable & set using the above stuff.
	Context ctx;
	if (!gfx_computable(&ctx.computable, tech))
		goto clean;

	ctx.set = gfx_renderer_add_set(t->renderer, tech, 0,
		1, 0, 0, 0,
		(GFXSetResource[]){{
			.binding = 0,
			.index = 0,
			.ref = gfx_ref_buffer(buffer)
		}},
		NULL, NULL, NULL);

	if (ctx.set == NULL)
		goto clean;

	// Render a single 'frame'.
	GFXFrame* frame = gfx_renderer_acquire(t->renderer);
	gfx_frame_start(frame, 1, (GFXInject[]){
		gfx_dep_sigrf(t->dep,
			GFX_ACCESS_STORAGE_READ_WRITE, GFX_STAGE_COMPUTE,
			GFX_ACCESS_HOST_READ, GFX_STAGE_NONE,
			gfx_ref_buffer(buffer))
	});

	gfx_recorder_compute(t->recorder, pass, compute, &ctx);
	gfx_frame_submit(frame);

	// Acquire again to synchronize.
	gfx_renderer_acquire(t->renderer);

	// Check results.
	gfx_log_info("\n"
		"Input:\n"
		"    %f | %f | %f | %f\n"
		"Expected output:\n"
		"    %f | %f | %f | %f\n"
		"Computed output:\n"
		"    %f | %f | %f | %f\n",
		values[0], values[1], values[2], values[3],
		result[0], result[1], result[2], result[3],
		buffPtr[0], buffPtr[1], buffPtr[2], buffPtr[3]);

	if (memcmp(buffPtr, result, sizeof(result)) != 0)
		gfx_log_error("Compute shader results are not as expected!");
	else
		success = 1;


	// Cleanup.
clean:
	gfx_destroy_shader(comp);

	if (!success) TEST_FAIL();
}


/****************************
 * Run the compute test.
 */
TEST_MAIN(compute);
