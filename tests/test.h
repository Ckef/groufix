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
 */


#ifndef TEST_H
#define TEST_H

#include <groufix.h>
#include <stdio.h>
#include <stdlib.h>

#if defined (TEST_ENABLE_THREADS)
	#include <pthread.h>
#endif


// Failure & success printing.
#define _TEST_PRINT_FAIL(name) \
	fprintf(stderr, "\n** %s test failed\n\n", name)

#define _TEST_PRINT_SUCCESS(name) \
	fprintf(stderr, "\n** %s test successful\n\n", name)


// Describes a test function that can be called.
#define TEST_DESCRIBE(tName, base) \
	static TestState _test_state_##tName = { .state = TEST_IDLE, .name = #tName }; \
	void _test_func_##tName(TestBase* base, TestState* _test_state)

// Forces the test to fail.
#define TEST_FAIL() \
	_test_fail(_test_state)

// Runs a test function from within another test function.
#define TEST_RUN(tName) \
	do { \
		if (_test_state_##tName.state == TEST_IDLE) { \
			_test_state_##tName.state = TEST_RUNNING; \
			_test_func_##tName(&_test_base, &_test_state_##name); \
			_TEST_PRINT_SUCCESS(#tName); \
			_test_state_##tName.state = TEST_IDLE; \
		} \
	} while (0)

// Runs a test in a new thread.
#define TEST_RUN_THREAD(tName) \
	do { \
		if (_test_state_##tName.state == TEST_IDLE) { \
			_test_state_##tName.state = TEST_RUNNING_THRD; \
			_test_state_##tName.f = _test_func_##tName; \
			if (pthread_create(&_test_state_##tName.thrd, NULL, _test_thrd, &_test_state_##tName)) \
				TEST_FAIL(); \
		} \
	} while (0)

// Joins a threaded test function.
#define TEST_JOIN(tName) \
	do { \
		if (_test_state_##tName.state == TEST_RUNNING_THRD) { \
			void* _test_ret; \
			pthread_join(_test_state_##tName.thrd, &_test_ret); \
			_test_state_##tName.state = TEST_IDLE; \
		} \
	} while(0)

// Main entry point for a test program, runs the given test name.
#define TEST_MAIN(tName) \
	int main(void) { \
		_test_init(&_test_state_##tName); \
		_test_state_##tName.state = TEST_RUNNING; \
		_test_func_##tName(&_test_base, &_test_state_##tName); \
		_test_state_##tName.state = TEST_IDLE; \
		_test_end(&_test_state_##tName); \
	} \
	int _test_unused_for_semicolon


/**
 * Global TestBase struct and
 * default event handlers & callbacks.
 */
#define TEST_BASE _test_base
#define TEST_EVT_KEY_RELEASE _test_key_release
#define TEST_CALLBACK_RENDER _test_default_render


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
static TestBase _test_base =
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
	.set = NULL,
	.renderable = { NULL, NULL, NULL }
};


/****************************
 * Some default shaders.
 ****************************/

#if !defined (TEST_SKIP_CREATE_SCENE)

static const char* _test_glsl_vertex =
	"#version 450\n"
	"layout(row_major, set = 0, binding = 0) uniform UBO {\n"
	"  mat4 mvp;\n"
	"};\n"
	"layout(location = 0) in vec3 position;\n"
	"layout(location = 1) in vec3 color;\n"
	"layout(location = 2) in vec2 texCoord;\n"
	"layout(location = 0) out vec3 fragColor;\n"
	"layout(location = 1) out vec2 fragTexCoord;\n"
	"out gl_PerVertex {\n"
	"  vec4 gl_Position;\n"
	"};\n"
	"void main() {\n"
	"  gl_Position = mvp * vec4(position, 1.0);\n"
	"  fragColor = color;\n"
	"  fragTexCoord = texCoord;\n"
	"}\n";


static const char* _test_glsl_fragment =
	"#version 450\n"
	"layout(set = 0, binding = 1) uniform sampler2D texSampler;\n"
	"layout(location = 0) in vec3 fragColor;\n"
	"layout(location = 1) in vec2 fragTexCoord;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() {\n"
	"  float tex = texture(texSampler, fragTexCoord).r;\n"
	"  outColor = vec4(fragColor, 1.0) * tex;\n"
	"}\n";

#endif


/****************************
 * All internal testing functions.
 ****************************/

/**
 * Clears the base test state.
 */
static void _test_clear(void)
{
	gfx_destroy_renderer(_test_base.renderer);
	gfx_destroy_shader(_test_base.vertex);
	gfx_destroy_shader(_test_base.fragment);
	gfx_destroy_heap(_test_base.heap);
	gfx_destroy_dep(_test_base.dep);
	gfx_destroy_window(_test_base.window);
	gfx_terminate();

	// Don't bother resetting _test_base as we will exit() anyway.
}

/**
 * Forces the test to fail and exits the program.
 */
static void _test_fail(TestState* test)
{
	_test_clear();

	_TEST_PRINT_FAIL(test->name);
	exit(EXIT_FAILURE);
}

/**
 * End (i.e. exit) the test program.
 */
static void _test_end(TestState* test)
{
	_test_clear();

	_TEST_PRINT_SUCCESS(test->name);
	exit(EXIT_SUCCESS);
}


#if defined (TEST_ENABLE_THREADS)

/**
 * Thread entry point for a test.
 */
static void* _test_thrd(void* arg)
{
	TestState* test = arg;

	if (!gfx_attach())
		_test_fail(test);

	test->f(&_test_base, test);
	gfx_detach();
	_TEST_PRINT_SUCCESS(test->name);

	return NULL;
}

#endif


/**
 * Default key release event handler.
 */
static void _test_key_release(GFXWindow* window,
                              GFXKey key, int scan, GFXModifier mod)
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
		break;

	// Close on escape.
	case GFX_KEY_ESCAPE:
		gfx_window_set_close(window, 1);
		break;

	default:
		break;
	}
}


#if !defined (TEST_SKIP_CREATE_SCENE)

/**
 * Default render callback.
 */
static void _test_default_render(GFXRecorder* recorder,
                                 unsigned int frame, void* ptr)
{
	// Record stuff.
	gfx_cmd_bind(recorder, _test_base.technique, 0, 1, 0, &_test_base.set, NULL);
	gfx_cmd_draw_indexed(recorder, &_test_base.renderable, 0, 0, 0, 0, 1);
}

#endif


/**
 * Initializes the test base program.
 */
static void _test_init(TestState* _test_state)
{
	// Initialize.
	if (!gfx_init())
		TEST_FAIL();

	// Create a window.
	_test_base.window = gfx_create_window(
		GFX_WINDOW_RESIZABLE | GFX_WINDOW_DOUBLE_BUFFER,
		_test_base.device, NULL,
		(GFXVideoMode){ 600, 400, 0 }, "groufix");

	if (_test_base.window == NULL)
		TEST_FAIL();

#if !defined (TEST_SKIP_EVENT_HANDLERS)
	// Register the default key events.
	_test_base.window->events.key.release = TEST_EVT_KEY_RELEASE;
#endif

	// Create a heap & dependency.
	_test_base.heap = gfx_create_heap(_test_base.device);
	if (_test_base.heap == NULL)
		TEST_FAIL();

	_test_base.dep = gfx_create_dep(_test_base.device, 2);
	if (_test_base.dep == NULL)
		TEST_FAIL();

	// Create a renderer and attach the window at index 0.
	_test_base.renderer = gfx_create_renderer(_test_base.device, 2);
	if (_test_base.renderer == NULL)
		TEST_FAIL();

	if (!gfx_renderer_attach_window(_test_base.renderer, 0, _test_base.window))
		TEST_FAIL();

	// Add a single recorder.
	_test_base.recorder = gfx_renderer_add_recorder(_test_base.renderer);
	if (_test_base.recorder == NULL)
		TEST_FAIL();

#if !defined (TEST_SKIP_CREATE_RENDER_GRAPH)
	// Add a single pass that writes to the window.
	_test_base.pass = gfx_renderer_add_pass(_test_base.renderer, 0, NULL);
	if (_test_base.pass == NULL)
		TEST_FAIL();

	if (!gfx_pass_consume(_test_base.pass, 0,
		GFX_ACCESS_ATTACHMENT_WRITE, GFX_STAGE_ANY))
	{
		TEST_FAIL();
	}

	gfx_pass_clear(_test_base.pass, 0,
		GFX_IMAGE_COLOR, (GFXClear){{ 0.0f, 0.0f, 0.0f, 0.0f }});

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

	_test_base.primitive = gfx_alloc_prim(_test_base.heap,
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

	if (_test_base.primitive == NULL)
		TEST_FAIL();

	GFXBufferRef vert = gfx_ref_prim_vertices(_test_base.primitive, 0);
	GFXBufferRef ind = gfx_ref_prim_indices(_test_base.primitive);

	if (!gfx_write(vertexData, vert, GFX_TRANSFER_ASYNC, 1, 1,
		(GFXRegion[]){{ .offset = 0, .size = sizeof(vertexData) }},
		(GFXRegion[]){{ .offset = 0, .size = 0 }},
		(GFXInject[]){
			gfx_dep_sig(_test_base.dep,
				GFX_ACCESS_VERTEX_READ, GFX_STAGE_ANY)
		}))
	{
		TEST_FAIL();
	}

	if (!gfx_write(indexData, ind, GFX_TRANSFER_ASYNC, 1, 1,
		(GFXRegion[]){{ .offset = 0, .size = sizeof(indexData) }},
		(GFXRegion[]){{ .offset = 0, .size = 0 }},
		(GFXInject[]){
			gfx_dep_sig(_test_base.dep,
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

	GFXImage* image = gfx_alloc_image(_test_base.heap,
		GFX_IMAGE_2D, GFX_MEMORY_WRITE,
		GFX_IMAGE_SAMPLED, GFX_FORMAT_R8_UNORM, 1, 1,
		4, 4, 1);

	if (image == NULL)
		TEST_FAIL();

	GFXGroup* group = gfx_alloc_group(_test_base.heap,
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
			gfx_dep_sig(_test_base.dep,
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
			gfx_dep_sig(_test_base.dep,
				GFX_ACCESS_SAMPLED_READ, GFX_STAGE_FRAGMENT)
		}))
	{
		TEST_FAIL();
	}

	// We've allocated & populated all GPU memory,
	// flush all the currently pending operations.
	if (!gfx_heap_flush(_test_base.heap))
		TEST_FAIL();

	// Create some shaders.
	_test_base.vertex =
		gfx_create_shader(GFX_STAGE_VERTEX, _test_base.device);
	if (_test_base.vertex == NULL)
		TEST_FAIL();

	_test_base.fragment =
		gfx_create_shader(GFX_STAGE_FRAGMENT, _test_base.device);
	if (_test_base.fragment == NULL)
		TEST_FAIL();

	// Compile GLSL into the shaders.
	GFXStringReader str;

	if (!gfx_shader_compile(_test_base.vertex, GFX_GLSL, 1,
		gfx_string_reader(&str, _test_glsl_vertex), NULL, NULL, NULL))
	{
		TEST_FAIL();
	}

	if (!gfx_shader_compile(_test_base.fragment, GFX_GLSL, 1,
		gfx_string_reader(&str, _test_glsl_fragment), NULL, NULL, NULL))
	{
		TEST_FAIL();
	}

	// Add a single technique & set immutable samplers.
	_test_base.technique = gfx_renderer_add_tech(_test_base.renderer, 2,
		(GFXShader*[]){ _test_base.vertex, _test_base.fragment });

	if (_test_base.technique == NULL)
		TEST_FAIL();

	gfx_tech_immutable(_test_base.technique, 0, 1); // Warns on fail.

	// Add a single set.
	_test_base.set = gfx_renderer_add_set(_test_base.renderer,
		_test_base.technique, 0,
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

	if (_test_base.set == NULL)
		TEST_FAIL();

	// Init the default renderable.
	gfx_renderable(&_test_base.renderable,
		_test_base.pass, _test_base.technique, _test_base.primitive, NULL);

#endif // TEST_SKIP_CREATE_SCENE
#endif // TEST_SKIP_CREATE_RENDER_GRAPH
}


#endif
