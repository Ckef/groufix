/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <assert.h>
#include <stdlib.h>


/****************************
 * Allocates and initializes a new groufix gamepad from a GLFW joystick id.
 * Automatically appends the gamepad to _groufix.gamepads.
 * @param jid Must be >= 0 and <= GLFW_JOYSTICK_LAST.
 * @return NULL on failure.
 *
 * glfwJoystickIsGamepad(jid) must return GLFW_TRUE.
 */
static _GFXGamepad* _gfx_alloc_gamepad(int jid)
{
	assert(jid >= 0);
	assert(jid <= GLFW_JOYSTICK_LAST);

	// Allocate a new gamepad.
	_GFXGamepad* gamepad = malloc(sizeof(_GFXGamepad));
	if (gamepad == NULL) return NULL;

	if (!gfx_vec_push(&_groufix.gamepads, 1, &gamepad))
	{
		free(gamepad);
		return NULL;
	}

	// Associate with GLFW using the user pointer and
	// initialize the gamepad itself.
	glfwSetJoystickUserPointer(jid, gamepad);

	gamepad->base.ptr = NULL;
	gamepad->base.name = glfwGetGamepadName(jid);
	gamepad->base.guid = glfwGetJoystickGUID(jid);
	gamepad->base.sysName = glfwGetJoystickName(jid);
	gamepad->jid = jid;

	return gamepad;
}

/****************************
 * On joystick connect or disconnect.
 * @param event Zero if it is disconnected, non-zero if it is connected.
 */
static void _gfx_glfw_joystick(int jid, int event)
{
	const bool conn = (event == GLFW_CONNECTED);
	_GFXGamepad* gamepad;

	if (conn)
	{
		// If not a gamepad, nothing to do.
		if (!glfwJoystickIsGamepad(jid)) return;

		// On connect, allocate a new gamepad and
		// attempt to insert it into the configuration.
		gamepad = _gfx_alloc_gamepad(jid);
		if (gamepad == NULL)
		{
			gfx_log_fatal("Could not initialize a newly connected gamepad.");
			return;
		}

		// Wanna know about it?
		gfx_log_info(
			"Gamepad connected:\n"
			"    [ %s ] (%s - %s)\n",
			gamepad->base.name,
			gamepad->base.guid,
			gamepad->base.sysName);
	}
	else
	{
		// On disconnect, get associated groufix gamepad.
		// If no groufix gamepad, it was no gamepad, do nothing.
		gamepad = glfwGetJoystickUserPointer(jid);
		if (gamepad == NULL) return;

		// On disconnect, shrink the configuration.
		gfx_vec_pop(&_groufix.gamepads, 1);

		// Wanna know about it?
		gfx_log_info(
			"Gamepad disconnected:\n"
			"    [ %s ] (%s - %s)\n",
			gamepad->base.name,
			gamepad->base.guid,
			gamepad->base.sysName);
	}

	// So we don't know if the order of the configuration array is preserved.
	// On connect, we just inserted at the end and on disconnect we popped it.
	// Just like with monitors, we rebuild the array from GLFW user pointers,
	// fixing all problems.
	size_t i = 0;

	for (int ijid = 0; ijid <= GLFW_JOYSTICK_LAST; ++ijid)
	{
		_GFXGamepad* g = glfwGetJoystickUserPointer(ijid);

		// If disconnecting, make sure we're not re-inserting the same jid!
		if (g != NULL && (conn || ijid != jid))
			*(_GFXGamepad**)gfx_vec_at(&_groufix.gamepads, i++) = g;
	}

	// Finally, call the event if given, and free the gamepad on disconnect.
	if (_groufix.gamepadEvent != NULL)
		_groufix.gamepadEvent(&gamepad->base, conn);

	if (!conn)
		free(gamepad);
}

/****************************/
bool _gfx_gamepads_init(void)
{
	assert(_groufix.gamepads.size == 0);

	// Reserve some data and create all gamepads.
	const size_t count = (size_t)(GLFW_JOYSTICK_LAST + 1);

	if (!gfx_vec_reserve(&_groufix.gamepads, count))
		goto terminate;

	for (int jid = 0; jid <= GLFW_JOYSTICK_LAST; ++jid)
		if (glfwJoystickIsGamepad(jid) && _gfx_alloc_gamepad(jid) == NULL)
			goto terminate;

	// Let's see the connected gamepads :)
	GFXBufWriter* logger = gfx_logger_info();
	if (logger != NULL)
	{
		gfx_io_writef(logger, "Detected gamepads:\n");

		for (size_t i = 0; i < _groufix.gamepads.size; ++i)
		{
			_GFXGamepad* gamepad =
				*(_GFXGamepad**)gfx_vec_at(&_groufix.gamepads, i);

			gfx_io_writef(logger,
				"    [ %s ] (%s - %s)\n",
				gamepad->base.name,
				gamepad->base.guid,
				gamepad->base.sysName);
		}

		gfx_logger_end(logger);
	}

	// Make sure we get configuration change events.
	glfwSetJoystickCallback(_gfx_glfw_joystick);

	return 1;


	// Cleanup on failure.
terminate:
	gfx_log_error("Could not initialize all connected gamepads.");
	_gfx_gamepads_terminate();

	return 0;
}

/****************************/
void _gfx_gamepads_terminate(void)
{
	// In case it did not initialize, make it a no-op.
	if (_groufix.gamepads.size == 0)
		return;

	// First just deallocate all gamepads.
	for (size_t i = 0; i < _groufix.gamepads.size; ++i)
	{
		_GFXGamepad** gamepad = gfx_vec_at(&_groufix.gamepads, i);

		glfwSetJoystickUserPointer((*gamepad)->jid, NULL);
		free(*gamepad);
	}

	// Clear data.
	glfwSetJoystickCallback(NULL);
	gfx_vec_clear(&_groufix.gamepads);
}

/****************************/
GFX_API void gfx_gamepad_event_set(void (*event)(GFXGamepad*, bool))
{
	assert(atomic_load(&_groufix.initialized));

	// Yeah just set the event callback.
	_groufix.gamepadEvent = event;
}

/****************************/
GFX_API size_t gfx_get_num_gamepads(void)
{
	assert(atomic_load(&_groufix.initialized));

	return _groufix.gamepads.size;
}

/****************************/
GFX_API GFXGamepad* gfx_get_gamepad(size_t index)
{
	assert(atomic_load(&_groufix.initialized));
	assert(_groufix.gamepads.size > 0);
	assert(index < _groufix.gamepads.size);

	return *(GFXGamepad**)gfx_vec_at(&_groufix.gamepads, index);
}

/****************************/
GFX_API void gfx_gamepad_get_state(GFXGamepad* gamepad, GFXGamepadState* state)
{
	assert(gamepad != NULL);
	assert(state != NULL);

	// Get GLFW state, guaranteed to not error in our case.
	GLFWgamepadstate glfwState;
	glfwGetGamepadState(((_GFXGamepad*)gamepad)->jid, &glfwState);

	for (GFXGamepadButton b = 0; b < GFX_GAMEPAD_NUM_BUTTONS; ++b)
		state->buttons[b] = glfwState.buttons[b] == GLFW_PRESS;

	for (GFXGamepadAxis a = 0; a < GFX_GAMEPAD_NUM_AXES; ++a)
		state->axes[a] = glfwState.axes[a];
}
