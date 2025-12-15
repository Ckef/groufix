/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_GAMEPAD_H
#define GFX_CORE_GAMEPAD_H

#include "groufix/core/keys.h"
#include "groufix/def.h"


/**
 * Gamepad state definition.
 */
typedef struct GFXGamepadState
{
	// Indices defined by GFXGamepadButton.
	bool buttons[GFX_GAMEPAD_NUM_BUTTONS];

	// Indices defined by GFXGamepadAxis.
	float axes[GFX_GAMEPAD_NUM_AXES];

} GFXGamepadState;


/**
 * Gamepad definition.
 */
typedef struct GFXGamepad
{
	// User pointer, can be used for any purpose.
	// Defaults to NULL.
	void* ptr;

	// All read-only.
	const char* name;
	const char* guid;
	const char* sysName;

} GFXGamepad;


/**
 * Sets the configuration change event callback.
 * The callback takes the gamepad in question and a zero or non-zero value,
 * zero if the gamepad is disconnected, non-zero if it is connected.
 * @param event NULL to disable the event callback.
 */
GFX_API void gfx_gamepad_event_set(void (*event)(GFXGamepad*, bool));

/**
 * Retrieves the number of currently connected gamepads.
 * @return 0 if no gamepads were found.
 */
GFX_API size_t gfx_get_num_gamepads(void);

/**
 * Retrieves a currently connected gamepad.
 * @param index Must be < gfx_get_num_gamepads().
 */
GFX_API GFXGamepad* gfx_get_gamepad(size_t index);

/**
 * Retrieves the state of a gamepad.
 * @param gamepad Cannot be NULL.
 * @param state   Output gamepad state, cannot be NULL.
 */
GFX_API void gfx_gamepad_get_state(GFXGamepad* gamepad, GFXGamepadState* state);


#endif
