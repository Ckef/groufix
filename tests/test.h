/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef TEST_H
#define TEST_H

#include <groufix.h>


/**
 * Base testing state, modify at your leisure :)
 */
typedef struct GFXTestBase
{
	int initialized;

	GFXWindow*   window;
	GFXRenderer* renderer; // Window is attached at index 0.

} GFXTestBase;


// Describes a test function that can be called.
#define TEST_DESCRIBE(name, base) \
	void _test_func_##name(GFXTestBase* _test_base, GFXTestBase* base)

// Forces the test to fail.
#define TEST_FAIL() \
	_test_fail()

// Runs a test function from within another test function.
#define TEST_RUN(name) \
	_test_func_##name(_test_base, _test_base)

// Main entry point for a test program, runs the given test name.
#define TEST_MAIN(name) \
	int main(void) { \
		GFXTestBase* _t = _test_init(); \
		_test_func_##name(_t, _t); \
		_test_end(); \
	} \
	int _test_unused_for_semicolon


/**
 * Initializes the test base program (which initializes groufix).
 * On failure it will exit the program and write status to stderr.
 * @return The test base program.
 *
 * On multiple calls, will return the same object.
 */
GFXTestBase* _test_init(void);

/**
 * Forces the test to fail and exits the program
 * Writes failure status to stderr.
 */
void _test_fail(void);

/**
 * End the test (which terminates groufix).
 * Writes status to stderr, can still fail.
 */
void _test_end(void);


#endif
