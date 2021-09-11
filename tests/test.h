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
 *   Where `t` is the name of the exposed TestBase pointer.
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
 *
 * TEST_SKIP_CREATE_RENDER_GRAPH
 *   Do not build a render graph,
 *   i.e. no passes are added to the base renderer.
 */


#ifndef TEST_H
#define TEST_H

#include <groufix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined (TEST_ENABLE_THREADS)
	#include <pthread.h>
#endif


// Describes a test function that can be called.
#define TEST_DESCRIBE(name, base) \
	static TestState _test_state_##name = { .state = TEST_IDLE }; \
	void _test_func_##name(TestBase* base)

// Forces the test to fail.
#define TEST_FAIL() \
	_test_fail()

// Runs a test function from within another test function.
#define TEST_RUN(name) \
	do { \
		if (_test_state_##name.state == TEST_IDLE) { \
			_test_state_##name.state = TEST_RUNNING; \
			_test_func_##name(&_test_base) \
			_test_state_##name.state = TEST_IDLE; \
		} \
	} while (0)

// Runs a test in a new thread.
#define TEST_RUN_THREAD(name) \
	do { \
		if (_test_state_##name.state == TEST_IDLE) { \
			_test_state_##name.state = TEST_RUNNING_THRD; \
			_test_state_##name.f = _test_func_##name; \
			if (pthread_create(&_test_state_##name.thrd, NULL, _test_thrd, &_test_state_##name)) \
				TEST_FAIL(); \
		} \
	} while (0)

// Joins a threaded test function.
#define TEST_JOIN(name) \
	do { \
		if (_test_state_##name.state == TEST_RUNNING_THRD) { \
			void* _test_ret; \
			pthread_join(_test_state_##name.thrd, &_test_ret); \
			_test_state_##name.state = TEST_IDLE; \
		} \
	} while(0)

// Main entry point for a test program, runs the given test name.
#define TEST_MAIN(name) \
	int main(void) { \
		_test_init(); \
		_test_state_##name.state = TEST_RUNNING; \
		_test_func_##name(&_test_base); \
		_test_state_##name.state = TEST_IDLE; \
		_test_end(); \
	} \
	int _test_unused_for_semicolon


/**
 * Base testing state, modify at your leisure :)
 */
typedef struct
{
	GFXWindow*    window;
	GFXHeap*      heap;
	GFXRenderer*  renderer; // Window is attached at index 0.
	GFXPrimitive* primitive;
	GFXGroup*     group;

} TestBase;


/**
 * Thread handle.
 */
typedef struct
{
	enum
	{
		TEST_IDLE,
		TEST_RUNNING,
		TEST_RUNNING_THRD

	} state;

#if defined (TEST_ENABLE_THREADS)
	void (*f)(TestBase*);
	pthread_t thrd;
#endif

} TestState;


/**
 * Instance of the test base state.
 */
static TestBase _test_base = { NULL, NULL, NULL, NULL, NULL };


/****************************
 * All internal testing functions.
 ****************************/

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

/**
 * Clears the base test state.
 */
static void _test_clear(void)
{
	gfx_destroy_renderer(_test_base.renderer);
	gfx_destroy_heap(_test_base.heap);
	gfx_destroy_window(_test_base.window);
	gfx_terminate();

	// Don't bother resetting _test_base as we will exit() anyway.
}

/**
 * Forces the test to fail and exits the program.
 */
static void _test_fail(void)
{
	_test_clear();

	fputs("\n* TEST FAILED\n", stderr);
	exit(EXIT_FAILURE);
}

/**
 * End (i.e. exit) the test program.
 */
static void _test_end(void)
{
	_test_clear();

	fputs("\n* TEST SUCCESSFUL\n", stderr);
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
		TEST_FAIL();

	test->f(&_test_base);
	gfx_detach();

	return NULL;
}

#endif


/**
 * Initializes the test base program.
 */
static void _test_init(void)
{
	// Initialize.
	if (!gfx_init())
		TEST_FAIL();

	// Create a window.
	_test_base.window = gfx_create_window(
		GFX_WINDOW_RESIZABLE | GFX_WINDOW_DOUBLE_BUFFER,
		NULL, NULL, (GFXVideoMode){ .width = 600, .height = 400 }, "groufix");

	if (_test_base.window == NULL)
		TEST_FAIL();

#if !defined (TEST_SKIP_EVENT_HANDLERS)
	// Register the default key release event.
	_test_base.window->events.key.release = _test_key_release;
#endif

	// Create a heap.
	_test_base.heap = gfx_create_heap(NULL);
	if (_test_base.heap == NULL)
		TEST_FAIL();

	// Create a renderer and attach the window at index 0.
	_test_base.renderer = gfx_create_renderer(NULL, 2);
	if (_test_base.renderer == NULL)
		TEST_FAIL();

	if (!gfx_renderer_attach_window(_test_base.renderer, 0, _test_base.window))
		TEST_FAIL();

#if !defined (TEST_SKIP_CREATE_RENDER_GRAPH)
	// Allocate a primitive.
	uint16_t indexData[] = {
		0, 1, 3, 2
	};

	float vertexData[] = {
		-0.5f, -0.5f, 0.0f,   1.0f, 0.0f, 0.0f,
		 0.5f, -0.5f, 0.0f,   1.0f, 1.0f, 0.0f,
		 0.5f,  0.5f, 0.0f,   0.0f, 1.0f, 0.0f,
		-0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f
	};

	_test_base.primitive = gfx_alloc_primitive(_test_base.heap,
		GFX_MEMORY_HOST_VISIBLE, 0,
		GFX_REF_NULL, GFX_REF_NULL,
		4, sizeof(float) * 6,
		4, sizeof(uint16_t),
		2, (GFXAttribute[]){
			{
				.format = GFX_FORMAT_R32G32B32_SFLOAT,
				.offset = 0
			}, {
				.format = GFX_FORMAT_R32G32B32_SFLOAT,
				.offset = sizeof(float) * 3
			}
		},
		GFX_TOPO_TRIANGLE_STRIP);

	if (_test_base.primitive == NULL)
		TEST_FAIL();

	GFXBufferRef vert = gfx_ref_primitive_vertices(_test_base.primitive, 0);
	GFXBufferRef ind = gfx_ref_primitive_indices(_test_base.primitive, 0);
	void* ptrVert = gfx_map(vert);
	void* ptrInd = gfx_map(ind);

	if (ptrVert == NULL || ptrInd == NULL)
		TEST_FAIL();

	memcpy(ptrVert, vertexData, sizeof(vertexData));
	memcpy(ptrInd, indexData, sizeof(indexData));
	gfx_unmap(vert);
	gfx_unmap(ind);

	// Allocate a group with an mvp matrix.
	float uboData[] = {
		1.0f, 0.2f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	_test_base.group = gfx_alloc_group(_test_base.heap,
		GFX_MEMORY_HOST_VISIBLE,
		GFX_BUFFER_UNIFORM,
		1, (GFXBinding[]){
			{
				.type = GFX_BINDING_BUFFER,
				.count = 1,
				.elementSize = sizeof(float) * 16,
				.numElements = 1,
				.buffers = NULL
			}
		});

	if (_test_base.group == NULL)
		TEST_FAIL();

	GFXBufferRef ubo = gfx_ref_group_buffer(_test_base.group, 0, 0, 0);
	void* ptrUbo = gfx_map(ubo);

	if (ptrUbo == NULL)
		TEST_FAIL();

	memcpy(ptrUbo, uboData, sizeof(uboData));
	gfx_unmap(ubo);

	// Add a single render pass that writes to the window.
	GFXRenderPass* pass = gfx_renderer_add(_test_base.renderer, 0, NULL);
	if (pass == NULL)
		TEST_FAIL();

	if (!gfx_render_pass_write(pass, 0))
		TEST_FAIL();

	// Make it render the thing.
	gfx_render_pass_use(pass, _test_base.primitive, _test_base.group);
#endif
}


#endif
