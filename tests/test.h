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
 *
 * TEST_RUN_THREAD(name)
 *   Same as TEST_RUN(name), except the test will run in a new thread.
 *   This will attach and detach the thread to and from groufix appropriately.
 *
 * TEST_JOIN(name)
 *   Joins a threaded test by name.
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

#if defined (TEST_ENABLE_THREADS)
	#include <pthread.h>
#endif


// Describes a test function that can be called.
#define TEST_DESCRIBE(name, base) \
	void _test_func_##name(TestBase* base)

// Forces the test to fail.
#define TEST_FAIL() \
	_test_fail()

// Runs a test function from within another test function.
#define TEST_RUN(name) \
	_test_func_##name(&_test_base)

// Runs a test in a new thread.
#define TEST_RUN_THREAD(name) \
	TestThread _test_thrd_##name = { .f = &_test_func_##name }; \
	if (pthread_create(&_test_thrd_##name.thrd, NULL, &_test_thrd, &_test_thrd_##name) != 0) \
		TEST_FAIL(); \

// Joins a threaded test function.
#define TEST_JOIN(name) \
	do { \
		void* _test_ret; \
		pthread_join(_test_thrd_##name.thrd, &_test_ret); \
	} while(0)

// Main entry point for a test program, runs the given test name.
#define TEST_MAIN(name) \
	int main(void) { \
		_test_init(); \
		_test_func_##name(&_test_base); \
		_test_end(); \
	} \
	int _test_unused_for_semicolon


/**
 * Base testing state, modify at your leisure :)
 */
typedef struct
{
	GFXWindow*   window;
	GFXRenderer* renderer; // Window is attached at index 0.

} TestBase;


#if defined (TEST_ENABLE_THREADS)

/**
 * Thread handle.
 */
typedef struct
{
	void (*f)(TestBase*);
	pthread_t thrd;

} TestThread;

#endif


/**
 * Instance of the test base state.
 */
static TestBase _test_base = { NULL, NULL };


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
	TestThread* thrd = arg;

	if (!gfx_attach())
		TEST_FAIL();

	thrd->f(&_test_base);
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

	// Create a renderer and attach the window at index 0.
	_test_base.renderer = gfx_create_renderer(NULL);
	if (_test_base.renderer == NULL)
		TEST_FAIL();

	if (!gfx_renderer_attach_window(_test_base.renderer, 0, _test_base.window))
		TEST_FAIL();

#if !defined (TEST_SKIP_CREATE_RENDER_GRAPH)
	// Add a single render pass that writes to the window.
	GFXRenderPass* pass = gfx_renderer_add(_test_base.renderer, 0, NULL);
	if (pass == NULL)
		TEST_FAIL();

	if (!gfx_render_pass_write(pass, 0))
		TEST_FAIL();
#endif
}


#endif
