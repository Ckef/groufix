/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/objects.h"
#include <assert.h>
#include <stdlib.h>


/****************************/
GFX_API GFXRenderPass* gfx_create_render_pass(GFXDevice* device)
{
	assert(device != NULL);

	// Allocate a new render pass.
	GFXRenderPass* pass = malloc(sizeof(GFXRenderPass));
	if (pass == NULL)
		goto clean;

	pass->window = NULL;

	// Get the physical device and make sure it's initialized.
	pass->device =
		(_GFXDevice*)((device != NULL) ? device : gfx_get_primary_device());

	if (!_gfx_device_init_context(pass->device))
		goto clean;

	return pass;

	// Clean on failure.
clean:
	gfx_log_error("Could not create a new render pass.");
	free(pass);

	return NULL;
}

/****************************/
GFX_API void gfx_destroy_render_pass(GFXRenderPass* pass)
{
	if (pass == NULL)
		return;

	free(pass);
}

/****************************/
GFX_API void gfx_render_pass_attach_window(GFXRenderPass* pass,
                                           GFXWindow* window)
{
	assert(pass != NULL);
	assert(window != NULL);

	pass->window = (_GFXWindow*)window;
}

/****************************/
GFX_API void gfx_render_pass_submit(GFXRenderPass* pass)
{
	assert(pass != NULL);
}
