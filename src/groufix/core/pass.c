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
#include <string.h>


/****************************/
GFXRenderPass* _gfx_create_render_pass(GFXRenderer* renderer,
                                       size_t numDeps, GFXRenderPass** deps)
{
	assert(renderer != NULL);
	assert(numDeps == 0 || deps != NULL);

	// Check if all dependencies use this renderer.
	for (size_t d = 0; d < numDeps; ++d)
		if (deps[d]->renderer != renderer)
		{
			gfx_log_warn(
				"Render pass cannot depend on a pass associated "
				"with a different renderer.");

			return NULL;
		}

	// Allocate a new render pass.
	GFXRenderPass* pass = malloc(
		sizeof(GFXRenderPass) +
		sizeof(GFXRenderPass*) * numDeps);

	if (pass == NULL)
		return NULL;

	// Initialize things.
	pass->renderer = renderer;
	pass->level = 0;
	pass->refs = 0;
	pass->vk.pass = VK_NULL_HANDLE;

	gfx_vec_init(&pass->reads, sizeof(size_t));
	gfx_vec_init(&pass->writes, sizeof(size_t));

	pass->numDeps = numDeps;

	if (numDeps) memcpy(
		pass->deps, deps, sizeof(GFXRenderPass*) * numDeps);

	for (size_t d = 0; d < numDeps; ++d)
	{
		// The level is the highest level of all dependencies + 1.
		if (deps[d]->level >= pass->level)
			pass->level = deps[d]->level + 1;

		// Increase the reference count of each dependency.
		// TODO: Maybe we want to filter out duplicates?
		++deps[d]->refs;
	}

	return pass;
}

/****************************/
void _gfx_destroy_render_pass(GFXRenderPass* pass)
{
	assert(pass != NULL);

	gfx_vec_clear(&pass->reads);
	gfx_vec_clear(&pass->writes);

	// Decrease the reference count of each dependency.
	// TODO: Maybe we want to filter out duplicates?
	for (size_t d = 0; d < pass->numDeps; ++d)
		--pass->deps[d]->refs;

	free(pass);
}

/****************************/
int _gfx_render_pass_rebuild(GFXRenderPass* pass)
{
	assert(pass != NULL);

	return 1;
}

/****************************/
GFX_API int gfx_render_pass_read(GFXRenderPass* pass, size_t index)
{
	assert(pass != NULL);

	// Try to find it first.
	// Just a linear search, nothing is sorted, whatever.
	for (size_t i = 0; i < pass->reads.size; ++i)
		if (*(size_t*)gfx_vec_at(&pass->reads, i) == index)
			return 1;

	return gfx_vec_push(&pass->reads, 1, &index);
}

/****************************/
GFX_API int gfx_render_pass_write(GFXRenderPass* pass, size_t index)
{
	assert(pass != NULL);

	// Try to find it first.
	for (size_t i = 0; i < pass->writes.size; ++i)
		if (*(size_t*)gfx_vec_at(&pass->writes, i) == index)
			return 1;

	return gfx_vec_push(&pass->writes, 1, &index);
}

/****************************/
GFX_API size_t gfx_render_pass_get_num_deps(GFXRenderPass* pass)
{
	assert(pass != NULL);

	return pass->numDeps;
}

/****************************/
GFX_API GFXRenderPass* gfx_render_pass_get_dep(GFXRenderPass* pass, size_t dep)
{
	assert(pass != NULL);
	assert(dep < pass->numDeps);

	return pass->deps[dep];
}
