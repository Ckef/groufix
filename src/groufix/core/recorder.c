/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>


/****************************/
GFX_API GFXRecorder* gfx_renderer_add_recorder(GFXRenderer* renderer)
{
	assert(renderer != NULL);

	// Get the number of virtual frames.
	// This immediately makes it very thread-unsafe with respect to the
	// virtual frame deque, luckily we're allowed to!
	const size_t frames = _GFX_RENDERER_NUM_FRAMES(renderer);

	// TODO: Implement.

	return NULL;
}

/****************************/
GFX_API void gfx_erase_recorder(GFXRecorder* recorder)
{
	assert(recorder != NULL);

	// TODO: Implement.
}
