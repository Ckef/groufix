/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/log.h"
#include "groufix/core.h"
#include <stddef.h>


/****************************/
GFX_API int gfx_log_set_level(GFXLogLevel level)
{
	// Because logging is special, we actually check this here.
	if (!_groufix.initialized)
		return 0;

	// Get the thread local state and set its level.
	_GFXThreadState* state = _gfx_state_get_local();
	if (state == NULL)
		return 0;

	state->log.level = level;

	return 1;
}

/****************************/
GFX_API int gfx_log_set(const char* file, int std)
{
	// Again, logging is special.
	if (!_groufix.initialized)
		return 0;

	_GFXThreadState* state = _gfx_state_get_local();
	if (state == NULL)
		return 0;

	// Set the output for the logger.
	// If a previous file was present, close it.
	if (state->log.file != NULL)
		fclose(state->log.file);

	state->log.std = std;
	state->log.file = NULL;

	if (file != NULL)
	{
		// Now open the appropriate logging file, if any.
		// We are going to prepend the thread id to the filename...
		// First find the length of the thread id.
		size_t idLen = (size_t)snprintf(NULL, 0, "%.4u", state->id);

		// Now find the point at which to insert the thread id.
		// This is after the last '/' character.
		size_t i;
		size_t li = 0;
		for (i = 0; file[i] != '\0'; ++i)
			if (file[i] == '/') li = i + 1;

		// Create a string for the file name.
		char f[i + idLen + 1];
		sprintf(f, "%.*s%.4u%s", (int)li, file, state->id, file + li);

		// Now finally attempt to open the file.
		state->log.file = fopen(f, "w");

		if (state->log.file == NULL)
			return 0;
	}

	return 1;
}
