/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <stdlib.h>


/****************************
 * Logs info about all connected gamepads.
 * @param head Header message (incl. \n!) after which all gamepads are listed.
 */
static void gfx_log_gamepads_(const char* head)
{
	// Let's see the connected gamepads :)
	GFXBufWriter* logger = gfx_logger_info();
	if (logger != NULL)
	{
		gfx_io_writef(logger, head);

		for (size_t i = 0; i < groufix_.gamepads.size; ++i)
		{
			GFXGamepad_* gamepad =
				*(GFXGamepad_**)gfx_vec_at(&groufix_.gamepads, i);

			gfx_io_writef(logger,
				"    [ %s ] (%s - %s%s)\n",
				gamepad->base.name,
				gamepad->base.guid,
				gamepad->base.sysName,
				gamepad->base.available ? "" : " | not available");
		}

		gfx_logger_end(logger);
	}
}

/****************************
 * Allocates and initializes a new groufix gamepad from a GLFW joystick id.
 * Automatically appends the gamepad to groufix_.gamepads.
 * @param jid Must be >= 0 and <= GLFW_JOYSTICK_LAST.
 * @return NULL on failure.
 *
 * glfwJoystickPresent(jid) must return GLFW_TRUE.
 */
static GFXGamepad_* gfx_alloc_gamepad_(int jid)
{
	assert(jid >= 0);
	assert(jid <= GLFW_JOYSTICK_LAST);

	// Allocate a new gamepad.
	GFXGamepad_* gamepad = malloc(sizeof(GFXGamepad_));
	if (gamepad == NULL) return NULL;

	if (!gfx_vec_push(&groufix_.gamepads, 1, &gamepad))
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
	gamepad->base.available = glfwJoystickIsGamepad(jid) == GLFW_TRUE;
	gamepad->jid = jid;

	return gamepad;
}

/****************************
 * On joystick connect or disconnect.
 * @param event Zero if it is disconnected, non-zero if it is connected.
 */
static void gfx_glfw_joystick_(int jid, int event)
{
	const bool conn = (event == GLFW_CONNECTED);
	GFXGamepad_* gamepad;

	if (conn)
	{
		// On connect, allocate a new gamepad and
		// attempt to insert it into the configuration.
		gamepad = gfx_alloc_gamepad_(jid);
		if (gamepad == NULL)
		{
			gfx_log_fatal("Could not initialize a newly connected gamepad.");
			return;
		}

		// Wanna know about it?
		gfx_log_info(
			"Gamepad connected:\n"
			"    [ %s ] (%s - %s%s)\n",
			gamepad->base.name,
			gamepad->base.guid,
			gamepad->base.sysName,
			gamepad->base.available ? "" : " | not available");
	}
	else
	{
		// On disconnect, get associated groufix gamepad.
		gamepad = glfwGetJoystickUserPointer(jid);

		// Then shrink the configuration.
		gfx_vec_pop(&groufix_.gamepads, 1);

		// Wanna know about it?
		gfx_log_info(
			"Gamepad disconnected:\n"
			"    [ %s ] (%s - %s%s)\n",
			gamepad->base.name,
			gamepad->base.guid,
			gamepad->base.sysName,
			gamepad->base.available ? "" : " | not available");
	}

	// So we don't know if the order of the configuration array is preserved.
	// On connect, we just inserted at the end and on disconnect we popped it.
	// Just like with monitors, we rebuild the array from GLFW user pointers,
	// fixing all problems.
	size_t i = 0;

	for (int ijid = 0; ijid <= GLFW_JOYSTICK_LAST; ++ijid)
	{
		GFXGamepad_* g = glfwGetJoystickUserPointer(ijid);

		// If disconnecting, make sure we're not re-inserting the same jid!
		if (g != NULL && (conn || ijid != jid))
			*(GFXGamepad_**)gfx_vec_at(&groufix_.gamepads, i++) = g;
	}

	// Finally, call the event if given, and free the gamepad on disconnect.
	if (groufix_.gamepadEvent != NULL)
		groufix_.gamepadEvent(&gamepad->base, conn);

	if (!conn)
		free(gamepad);
}

/****************************/
bool gfx_gamepads_init_(void)
{
	assert(groufix_.gamepads.size == 0);

	// Create all gamepads.
	for (int jid = 0; jid <= GLFW_JOYSTICK_LAST; ++jid)
		if (glfwJoystickPresent(jid) && gfx_alloc_gamepad_(jid) == NULL)
			goto terminate;

	// Only log if there are actual gamepads.
	if (groufix_.gamepads.size > 0)
		gfx_log_gamepads_("Detected gamepads:\n");

	// Make sure we get configuration change events.
	glfwSetJoystickCallback(gfx_glfw_joystick_);

	return 1;


	// Cleanup on failure.
terminate:
	gfx_log_error("Could not initialize all connected gamepads.");
	gfx_gamepads_terminate_();

	return 0;
}

/****************************/
void gfx_gamepads_terminate_(void)
{
	// In case it did not initialize, make it a no-op.
	if (groufix_.gamepads.size == 0)
		return;

	// First just deallocate all gamepads.
	for (size_t i = 0; i < groufix_.gamepads.size; ++i)
	{
		GFXGamepad_** gamepad = gfx_vec_at(&groufix_.gamepads, i);

		glfwSetJoystickUserPointer((*gamepad)->jid, NULL);
		free(*gamepad);
	}

	// Clear data.
	glfwSetJoystickCallback(NULL);
	gfx_vec_clear(&groufix_.gamepads);
}

/****************************/
GFX_API void gfx_gamepad_event_set(void (*event)(GFXGamepad*, bool))
{
	assert(atomic_load(&groufix_.initialized));

	// Yeah just set the event callback.
	groufix_.gamepadEvent = event;
}

/****************************/
GFX_API size_t gfx_get_num_gamepads(void)
{
	assert(atomic_load(&groufix_.initialized));

	return groufix_.gamepads.size;
}

/****************************/
GFX_API GFXGamepad* gfx_get_gamepad(size_t index)
{
	assert(atomic_load(&groufix_.initialized));
	assert(groufix_.gamepads.size > 0);
	assert(index < groufix_.gamepads.size);

	return *(GFXGamepad**)gfx_vec_at(&groufix_.gamepads, index);
}

/****************************/
GFX_API bool gfx_gamepad_get_state(GFXGamepad* gamepad, GFXGamepadState* state)
{
	assert(gamepad != NULL);
	assert(state != NULL);

	GLFWgamepadstate glfwState;
	if (!glfwGetGamepadState(((GFXGamepad_*)gamepad)->jid, &glfwState))
		return 0;

	for (GFXGamepadButton b = 0; b < GFX_GAMEPAD_NUM_BUTTONS; ++b)
		state->buttons[b] = glfwState.buttons[b] == GLFW_PRESS;

	for (GFXGamepadAxis a = 0; a < GFX_GAMEPAD_NUM_AXES; ++a)
		state->axes[a] = glfwState.axes[a];

	return 1;
}

/****************************/
GFX_API bool gfx_gamepad_mappings_update(const GFXReader* src)
{
	assert(atomic_load(&groufix_.initialized));
	assert(src != NULL);

	// Read the source, unfortunately we need a NULL-terminated string.
	// We don't know if gfx_io_get will return that, allocate it manually.
	long long len = gfx_io_len(src);
	if (len <= 0) goto error;

	char* source = malloc((size_t)len + 1);
	if (source == NULL) goto error;

	// Read source.
	len = gfx_io_read(src, source, (size_t)len);
	if (len <= 0)
	{
		free(source);
		goto error;
	}

	// Add NULL-terminator and simply feed it to GLFW.
	source[len] = '\0';
	glfwUpdateGamepadMappings(source);

	free(source); // Immediately free.

	// Lastly, fix all existing gamepads.
	// These may now have new availability & names.
	for (size_t i = 0; i < groufix_.gamepads.size; ++i)
	{
		GFXGamepad_* gamepad =
			*(GFXGamepad_**)gfx_vec_at(&groufix_.gamepads, i);

		gamepad->base.name = glfwGetGamepadName(gamepad->jid);
		gamepad->base.available = glfwJoystickIsGamepad(gamepad->jid) == GLFW_TRUE;
	}

	// Always log something.
	if (groufix_.gamepads.size > 0)
		gfx_log_gamepads_("Gamepad mappings updated, detected gamepads:\n");
	else
		gfx_log_info("Gamepad mappings updated.");

	return 1;


	// Error on failure.
error:
	gfx_log_error(
		"Could not read SDL game controller db source from stream.");

	return 0;
}
