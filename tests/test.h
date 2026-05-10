/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 *
 * This file contains the header-only testing utility.
 * Use the following macros to describe and run a test:
 *
 * TEST_DESCRIBE(name, base)
 *   Describe a new test, the syntax is similar to a function:
 *   TEST_DESCRIBE(basic_test, t) { gfx_window_set_title(t->window, "test"); }
 *   Where `t` is equivalent to (&TEST_BASE),
 *   i.e. a pointer to the global TestBase struct.
 *
 * TEST_FAIL()
 *   Forces the test to fail and exits the program.
 *
 * TEST_RUN(name)
 *   Call from within a test, run another test by name.
 *   Becomes a no-op if an instance of name is already running.
 *
 * TEST_RUN_THREAD(name)
 *   Same as TEST_RUN(name), except the test will run in a new thread.
 *   This will attach and detach the thread to and from groufix appropriately.
 *
 * TEST_JOIN(name)
 *   Joins a threaded test by name.
 *   Becomes a no-op if no threaded instance of name is running.
 *
 * TEST_MAIN(name)
 *   Main entry point of the program by test name, use as follows:
 *   TEST_MAIN(basic_test);
 *
 * To enable threading, TEST_ENABLE_THREADS must be defined. Threading is
 * implementeded using pthreads. The compiler should support this, luckily
 * Mingw-w64 does on all platforms so no issues on Windows.
 *
 * The testing utility initializes groufix and opens a window backed by a
 * default renderer setup. To override default behaviour you can disable some
 * building steps, define one of the following before includng this file:
 *
 * TEST_SKIP_CREATE_WINDOW
 *   Do not open a base window to render to.
 *   Also skips creating a render graph and scene.
 *
 * TEST_SKIP_EVENT_HANDLERS
 *   Do not register the default event handlers for the base window.
 *   To set event handlers yourself, default event handlers are defined:
 *    TEST_EVT_KEY_RELEASE
 *
 * TEST_SKIP_CREATE_RENDER_GRAPH
 *   Do not build a render graph,
 *   i.e. no passes are added to the base renderer.
 *   Also skips creating a scene.
 *
 * TEST_SKIP_CREATE_SCENE
 *   Do not build a scene,
 *   i.e. no renderables (or associated resources) are created.
 *   To record with the created scene, default render callbacks are defined:
 *    TEST_CALLBACK_RENDER
 *
 * Lastly, the created renderer will have 2 virtual render frames by default.
 * To override this behaviour, TEST_NUM_FRAMES can be defined.
 */


#ifndef TEST_H
#define TEST_H

#include <groufix.h>
#include <stdio.h>
#include <stdlib.h>

#if defined (TEST_ENABLE_THREADS)
	#include <pthread.h>
#endif


// Make disable defines cascade.
#if defined (TEST_SKIP_CREATE_WINDOW)
	#define TEST_SKIP_CREATE_RENDER_GRAPH
#endif

#if defined (TEST_SKIP_CREATE_RENDER_GRAPH)
	#define TEST_SKIP_CREATE_SCENE
#endif


// Failure & success printing.
#define TEST_PRINT_FAIL_(name) \
	fprintf(stderr, "\n** %s test failed\n\n", name)

#define TEST_PRINT_SUCCESS_(name) \
	fprintf(stderr, "\n** %s test successful\n\n", name)


// Describes a test function that can be called.
#define TEST_DESCRIBE(tName, base) \
	static TestState test_state_##tName##_ = { .state = TEST_IDLE, .name = #tName }; \
	void test_func_##tName##_(TestBase* base, TestState* test_state_)


// Forces the test to fail.
#define TEST_FAIL() \
	test_fail_(test_state_)


// Runs a test function from within another test function.
#define TEST_RUN(tName) \
	do { \
		if (test_state_##tName##_.state == TEST_IDLE) { \
			test_state_##tName##_.state = TEST_RUNNING; \
			test_func_##tName##_(&test_base_, &test_state_##tName##_); \
			TEST_PRINT_SUCCESS_(#tName); \
			test_state_##tName##_.state = TEST_IDLE; \
		} \
	} while (0)


// Runs a test in a new thread.
#define TEST_RUN_THREAD(tName) \
	do { \
		if (test_state_##tName##_.state == TEST_IDLE) { \
			test_state_##tName##_.state = TEST_RUNNING_THRD; \
			test_state_##tName##_.f = test_func_##tName##_; \
			if (pthread_create(&test_state_##tName##_.thrd, NULL, test_thrd_, &test_state_##tName##_)) \
				TEST_FAIL(); \
		} \
	} while (0)


// Joins a threaded test function.
#define TEST_JOIN(tName) \
	do { \
		if (test_state_##tName##_.state == TEST_RUNNING_THRD) { \
			void* test_ret_; \
			pthread_join(test_state_##tName##_.thrd, &test_ret_); \
			test_state_##tName##_.state = TEST_IDLE; \
		} \
	} while(0)


// Main entry point for a test program, runs the given test name.
#define TEST_MAIN(tName) \
	int main(void) { \
		test_init_(&test_state_##tName##_); \
		test_state_##tName##_.state = TEST_RUNNING; \
		test_func_##tName##_(&test_base_, &test_state_##tName##_); \
		test_state_##tName##_.state = TEST_IDLE; \
		test_end_(&test_state_##tName##_); \
	} \
	int test_unused_for_semicolon_


/**
 * Global TestBase struct and
 * default event handlers & callbacks and
 * number of frames to create.
 */
#define TEST_BASE test_base_
#define TEST_EVT_KEY_RELEASE test_key_release_
#define TEST_CALLBACK_RENDER test_default_render_

#ifndef TEST_NUM_FRAMES
	#define TEST_NUM_FRAMES 2
#endif


/**
 * Base testing state, read/write at your leisure :)
 */
typedef struct TestBase
{
	// Base stuff.
	GFXDevice*     device;
	GFXWindow*     window;
	GFXHeap*       heap;
	GFXDependency* dep;
	GFXRenderer*   renderer; // Window is attached at index 0.
	GFXRecorder*   recorder;

	// Render graph stuff.
	GFXPass* pass;

	// Scene stuff.
	GFXPrimitive* primitive;
	GFXShader*    vertex;
	GFXShader*    fragment;
	GFXTechnique* technique;
	GFXSet*       set;
	GFXRenderable renderable;

} TestBase;


/**
 * Thread handle.
 */
typedef struct TestState
{
	enum {
		TEST_IDLE,
		TEST_RUNNING,
		TEST_RUNNING_THRD

	} state;

#if defined (TEST_ENABLE_THREADS)
	void (*f)(struct TestBase*, struct TestState*);
	pthread_t thrd;
#endif

	const char* name;

} TestState;


/**
 * 'Global' instance of the test base state.
 */
static TestBase test_base_ =
{
	.device = NULL,
	.window = NULL,
	.heap = NULL,
	.dep = NULL,
	.renderer = NULL,
	.recorder = NULL,
	.pass = NULL,
	.primitive = NULL,
	.vertex = NULL,
	.fragment = NULL,
	.technique = NULL,
	.set = NULL
};


/****************************
 * Some default shaders.
 ****************************/

#if !defined (TEST_SKIP_CREATE_SCENE)

static const char* test_glsl_vertex_ =
	"#version 450\n"
	"layout(row_major, set = 0, binding = 0) uniform UBO {\n"
	"  mat4 mvp;\n"
	"};\n"
	"layout(location = 0) in vec3 vPosition;\n"
	"layout(location = 1) in vec3 vColor;\n"
	"layout(location = 2) in vec2 vTexCoord;\n"
	"layout(location = 0) out vec3 fColor;\n"
	"layout(location = 1) out vec2 fTexCoord;\n"
	"out gl_PerVertex {\n"
	"  vec4 gl_Position;\n"
	"};\n"
	"void main() {\n"
	"  gl_Position = mvp * vec4(vPosition, 1.0);\n"
	"  fColor = vColor;\n"
	"  fTexCoord = vTexCoord;\n"
	"}\n";


static const char* test_glsl_fragment_ =
	"#version 450\n"
	"layout(set = 0, binding = 1) uniform sampler2D tex;\n"
	"layout(location = 0) in vec3 fColor;\n"
	"layout(location = 1) in vec2 fTexCoord;\n"
	"layout(location = 0) out vec4 oColor;\n"
	"void main() {\n"
	"  float tex = texture(tex, fTexCoord).r;\n"
	"  oColor = vec4(fColor, 1.0) * tex;\n"
	"}\n";

#endif


/****************************
 * All internal testing functions.
 ****************************/

/**
 * Clears the base test state.
 */
static void test_clear_(void)
{
	gfx_destroy_renderer(test_base_.renderer);
	gfx_destroy_shader(test_base_.vertex);
	gfx_destroy_shader(test_base_.fragment);
	gfx_destroy_heap(test_base_.heap);
	gfx_destroy_dep(test_base_.dep);
	gfx_destroy_window(test_base_.window);
	gfx_terminate();

	// Don't bother resetting test_base_ as we will exit() anyway.
}

/**
 * Forces the test to fail and exits the program.
 */
static void test_fail_(TestState* test)
{
	test_clear_();

	TEST_PRINT_FAIL_(test->name);
	exit(EXIT_FAILURE);
}

/**
 * End (i.e. exit) the test program.
 */
static void test_end_(TestState* test)
{
	test_clear_();

	TEST_PRINT_SUCCESS_(test->name);
	exit(EXIT_SUCCESS);
}


#if defined (TEST_ENABLE_THREADS)

/**
 * Thread entry point for a test.
 */
static void* test_thrd_(void* arg)
{
	TestState* test = arg;

	if (!gfx_attach())
		test_fail_(test);

	test->f(&test_base_, test);
	gfx_detach();
	TEST_PRINT_SUCCESS_(test->name);

	return NULL;
}

#endif


#if !defined (TEST_SKIP_CREATE_WINDOW)

/**
 * Default key release event handler.
 */
static bool test_key_release_(GFXWindow* window,
                              GFXKey key, int scan, GFXModifier mod, void* data)
{
	switch (key)
	{
	// Toggle fullscreen on F11.
	case GFX_KEY_F11:
		if (gfx_window_get_monitor(window) != NULL)
		{
			gfx_window_set_monitor(
				window, NULL,
				(GFXVideoMode){ 600, 400, 0 });
		}
		else
		{
			GFXMonitor* monitor = gfx_get_primary_monitor();
			gfx_window_set_monitor(
				window, monitor,
				gfx_monitor_get_current_mode(monitor));
		}
		return 0;

	// Close on escape.
	case GFX_KEY_ESCAPE:
		gfx_window_set_close(window, 1);
		return 0;

	default:
		return 0;
	}
}

#endif


#if !defined (TEST_SKIP_CREATE_SCENE)

/**
 * Default render callback.
 */
static void test_default_render_(GFXRecorder* recorder, void* ptr)
{
	// Record stuff.
	gfx_cmd_bind(recorder, test_base_.technique, 0, 1, 0, &test_base_.set, NULL);
	gfx_cmd_draw_prim(recorder, &test_base_.renderable, 1, 0);
}

#endif


/**
 * Initializes the test base program.
 */
static void test_init_(TestState* test_state_)
{
	// Initialize.
	if (!gfx_init())
		TEST_FAIL();

	// Create a heap & dependency.
	test_base_.heap = gfx_create_heap(test_base_.device);
	if (test_base_.heap == NULL)
		TEST_FAIL();

	test_base_.dep = gfx_create_dep(test_base_.device, TEST_NUM_FRAMES);
	if (test_base_.dep == NULL)
		TEST_FAIL();

	// Create a renderer.
	test_base_.renderer = gfx_create_renderer(test_base_.heap, TEST_NUM_FRAMES);
	if (test_base_.renderer == NULL)
		TEST_FAIL();

	// Add a single recorder.
	test_base_.recorder = gfx_renderer_add_recorder(test_base_.renderer);
	if (test_base_.recorder == NULL)
		TEST_FAIL();

#if !defined (TEST_SKIP_CREATE_WINDOW)
	// Create a window.
	test_base_.window = gfx_create_window(
		GFX_WINDOW_RESIZABLE | GFX_WINDOW_DOUBLE_BUFFER,
		test_base_.device, NULL,
		(GFXVideoMode){ 600, 400, 0 }, "groufix");

	if (test_base_.window == NULL)
		TEST_FAIL();

#if !defined (TEST_SKIP_EVENT_HANDLERS)
	// Register the default key events.
	test_base_.window->events.key.release = TEST_EVT_KEY_RELEASE;
#endif

	// Attach the window at index 0.
	if (!gfx_renderer_attach_window(test_base_.renderer, 0, test_base_.window))
		TEST_FAIL();

#if !defined (TEST_SKIP_CREATE_RENDER_GRAPH)
	// Add a single pass that writes to the window.
	test_base_.pass = gfx_renderer_add_pass(
		test_base_.renderer, GFX_PASS_RENDER, 0, 0, NULL);

	if (test_base_.pass == NULL)
		TEST_FAIL();

	if (!gfx_pass_consume(test_base_.pass, 0,
		GFX_ACCESS_ATTACHMENT_WRITE, GFX_STAGE_ANY))
	{
		TEST_FAIL();
	}

	gfx_pass_clear(test_base_.pass, 0,
		GFX_IMAGE_COLOR, (GFXClear){{ 0.0f, 0.0f, 0.0f, 0.0f }});

	// Preemptively inject a general wait dependency.
	gfx_pass_inject(test_base_.pass,
		1, (GFXInject[]){ gfx_dep_wait(test_base_.dep) });

#if !defined (TEST_SKIP_CREATE_SCENE)
	// Allocate a primitive.
	uint16_t indexData[] = {
		0, 1, 3, 2
	};

	float vertexData[] = {
		-0.5f, -0.5f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,
		 0.5f, -0.5f, 0.0f,   1.0f, 1.0f, 0.0f,   1.0f, 0.0f,
		 0.5f,  0.5f, 0.0f,   0.0f, 1.0f, 0.0f,   1.0f, 1.0f,
		-0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, 1.0f
	};

	test_base_.primitive = gfx_alloc_prim(test_base_.heap,
		GFX_MEMORY_WRITE,
		GFX_BUFFER_NONE,
		GFX_TOPO_TRIANGLE_STRIP,
		4, sizeof(uint16_t), 4,
		GFX_REF_NULL,
		3, (GFXAttribute[]){
			{
				.format = GFX_FORMAT_R32G32B32_SFLOAT,
				.offset = 0,
				.stride = sizeof(float) * 8,
				.buffer = GFX_REF_NULL
			}, {
				.format = GFX_FORMAT_R32G32B32_SFLOAT,
				.offset = sizeof(float) * 3,
				.stride = sizeof(float) * 8,
				.buffer = GFX_REF_NULL
			}, {
				.format = GFX_FORMAT_R32G32_SFLOAT,
				.offset = sizeof(float) * 6,
				.stride = sizeof(float) * 8,
				.buffer = GFX_REF_NULL
			}
		});

	if (test_base_.primitive == NULL)
		TEST_FAIL();

	GFXBufferRef vert = gfx_ref_prim_vertices(test_base_.primitive, 0);
	GFXBufferRef ind = gfx_ref_prim_indices(test_base_.primitive);

	if (!gfx_write(vertexData, vert, GFX_TRANSFER_ASYNC, 1, 1,
		(GFXRegion[]){{ .offset = 0, .size = sizeof(vertexData) }},
		(GFXRegion[]){{ .offset = 0, .size = 0 }},
		(GFXInject[]){
			gfx_dep_sig(test_base_.dep,
				GFX_ACCESS_VERTEX_READ, GFX_STAGE_ANY)
		}))
	{
		TEST_FAIL();
	}

	if (!gfx_write(indexData, ind, GFX_TRANSFER_ASYNC, 1, 1,
		(GFXRegion[]){{ .offset = 0, .size = sizeof(indexData) }},
		(GFXRegion[]){{ .offset = 0, .size = 0 }},
		(GFXInject[]){
			gfx_dep_sig(test_base_.dep,
				GFX_ACCESS_INDEX_READ, GFX_STAGE_ANY)
		}))
	{
		TEST_FAIL();
	}

	// Allocate a group with an mvp matrix and a texture.
	float uboData[] = {
		1.0f, 0.2f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	uint8_t imgData[] = {
		255, 0, 255, 0,
		0, 255, 0, 255,
		255, 0, 255, 0,
		0, 255, 0, 255
	};

	GFXImage* image = gfx_alloc_image(test_base_.heap,
		GFX_IMAGE_2D, GFX_MEMORY_WRITE,
		GFX_IMAGE_SAMPLED, GFX_FORMAT_R8_UNORM, 1, 1,
		4, 4, 1);

	if (image == NULL)
		TEST_FAIL();

	GFXGroup* group = gfx_alloc_group(test_base_.heap,
		GFX_MEMORY_WRITE,
		GFX_BUFFER_UNIFORM,
		2, (GFXBinding[]){
			{
				.type = GFX_BINDING_BUFFER,
				.count = 1,
				.numElements = 1,
				.elementSize = sizeof(float) * 16,
				.buffers = NULL
			}, {
				.type = GFX_BINDING_IMAGE,
				.count = 1,
				.images = (GFXImageRef[]){ gfx_ref_image(image) }
			}
		});

	if (group == NULL)
		TEST_FAIL();

	GFXBufferRef ubo = gfx_ref_group_buffer(group, 0, 0);
	GFXImageRef img = gfx_ref_group_image(group, 1, 0);

	if (!gfx_write(uboData, ubo, GFX_TRANSFER_ASYNC, 1, 1,
		(GFXRegion[]){{ .offset = 0, .size = sizeof(uboData) }},
		(GFXRegion[]){{ .offset = 0, .size = 0 }},
		(GFXInject[]){
			gfx_dep_sig(test_base_.dep,
				GFX_ACCESS_UNIFORM_READ, GFX_STAGE_VERTEX)
		}))
	{
		TEST_FAIL();
	}

	if (!gfx_write(imgData, img, GFX_TRANSFER_ASYNC, 1, 1,
		(GFXRegion[]){{
			.offset = 0,
			.rowSize = 0,
			.numRows = 0
		}},
		(GFXRegion[]){{
			.aspect = GFX_IMAGE_COLOR,
			.mipmap = 0, .layer = 0,  .numLayers = 1,
			.x = 0,      .y = 0,      .z = 0,
			.width = 4,  .height = 4, .depth = 1
		}},
		(GFXInject[]){
			gfx_dep_sig(test_base_.dep,
				GFX_ACCESS_SAMPLED_READ, GFX_STAGE_FRAGMENT)
		}))
	{
		TEST_FAIL();
	}

	// We've allocated & populated all GPU memory,
	// flush all the currently pending operations.
	if (!gfx_heap_flush(test_base_.heap))
		TEST_FAIL();

	// Create some shaders.
	test_base_.vertex =
		gfx_create_shader(GFX_STAGE_VERTEX, test_base_.device);
	if (test_base_.vertex == NULL)
		TEST_FAIL();

	test_base_.fragment =
		gfx_create_shader(GFX_STAGE_FRAGMENT, test_base_.device);
	if (test_base_.fragment == NULL)
		TEST_FAIL();

	// Compile GLSL into the shaders.
	GFXStringReader str;

	if (!gfx_shader_compile(test_base_.vertex, GFX_GLSL, 1,
		gfx_string_reader(&str, test_glsl_vertex_), NULL, NULL, NULL))
	{
		TEST_FAIL();
	}

	if (!gfx_shader_compile(test_base_.fragment, GFX_GLSL, 1,
		gfx_string_reader(&str, test_glsl_fragment_), NULL, NULL, NULL))
	{
		TEST_FAIL();
	}

	// Add a single technique & set immutable samplers.
	test_base_.technique = gfx_renderer_add_tech(test_base_.renderer, 2,
		(GFXShader*[]){ test_base_.vertex, test_base_.fragment });

	if (test_base_.technique == NULL)
		TEST_FAIL();

	gfx_tech_immutable(test_base_.technique, 0, 1); // Warns on fail.

	// Add a single set.
	test_base_.set = gfx_renderer_add_set(test_base_.renderer,
		test_base_.technique, 0,
		0, 1, 0, 0,
		NULL,
		(GFXSetGroup[]){{
			.binding = 0,
			.offset = 0,
			.numBindings = 0,
			.group = group
		}},
		NULL,
		NULL);

	if (test_base_.set == NULL)
		TEST_FAIL();

	// Init the default renderable.
	gfx_renderable(&test_base_.renderable,
		test_base_.pass, test_base_.technique, test_base_.primitive, NULL);

#endif // TEST_SKIP_CREATE_SCENE
#endif // TEST_SKIP_CREATE_RENDER_GRAPH
#endif // TEST_SKIP_CREATE_WINDOW
}


#endif
