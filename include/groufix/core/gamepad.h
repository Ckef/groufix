/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_GAMEPAD_H
#define GFX_CORE_GAMEPAD_H

#include "groufix/containers/io.h"
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
	const char* name; // NULL if not available.
	const char* guid;
	const char* sysName;

	bool available; // Zero if there is no gamepad mapping found.

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
 * @return Zero if the gamepad is not available, nothing is written to state.
 */
GFX_API bool gfx_gamepad_get_state(GFXGamepad* gamepad, GFXGamepadState* state);

/**
 * Updates the internal list of gamepad mappings from ASCII source.
 * The format is defined by the SDL and SDL_GameControllerDB projects.
 * See GLFW for the currently supported features.
 * @param src Source stream, cannot be NULL.
 * @return Non-zero on success.
 *
 * All gamepads returned by gfx_get_gamepad remain valid,
 * but their `name` and `available` fields may be updated.
 */
GFX_API bool gfx_gamepad_mappings_update(const GFXReader* src);


#endif
