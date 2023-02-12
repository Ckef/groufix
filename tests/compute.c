/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#define TEST_SKIP_CREATE_WINDOW
#include "test.h"


/****************************
 * Compute shader.
 */
static const char* glsl_compute =
	"#version 450\n"
	"layout(set = 0, binding = 0, std430) buffer Positions {\n"
	"  vec2 pos[];\n"
	"} positions;\n"
	"layout(set = 0, binding = 1, std430) readonly buffer Velocity {\n"
	"  vec2 vel[];\n"
	"} velocities;\n"
	"void main() {\n"
	"  vec2 current_pos = positions.pos[gl_GlobalInvocationID.x];\n"
	"  vec2 velocity = velocities.vel[gl_GlobalInvocationID.x];\n"
	"  current_pos += velocity;\n"
	"  if (\n"
	"    current_pos.x >  0.95 ||\n"
	"    current_pos.x < -0.95 ||\n"
	"    current_pos.y >  0.95 ||\n"
	"    current_pos.y < -0.95)\n"
	"  {\n"
	"    current_pos = -2.0 * velocity + current_pos + 0.05;\n"
	"  }\n"
	"  positions.pos[gl_GlobalInvocationID.x] = current_pos;\n"
	"}\n";


/****************************
 * Compute test.
 */
TEST_DESCRIBE(compute, t)
{
	bool success = 0;

	// Create a compute shader.
	GFXShader* shader = gfx_create_shader(GFX_STAGE_COMPUTE, t->device);
	if (shader == NULL)
		goto clean;

	// Compile GLSL into the shader.
	GFXStringReader str;
	if (!gfx_shader_compile(shader, GFX_GLSL, 1,
		gfx_string_reader(&str, glsl_compute), NULL, NULL, NULL))
	{
		goto clean;
	}

	// TODO: Uuuuh, do smth.

	// Render a single 'frame'.
	GFXFrame* frame = gfx_renderer_acquire(t->renderer);
	gfx_frame_start(frame, 1, (GFXInject[]){ gfx_dep_wait(t->dep), });
	gfx_frame_submit(frame);
	gfx_heap_purge(t->heap);

	success = 1;


	// Cleanup.
clean:
	gfx_destroy_shader(shader);

	if (!success) TEST_FAIL();
}


/****************************
 * Run the compute test.
 */
TEST_MAIN(compute);
