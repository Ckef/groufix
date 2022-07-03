/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/assets/image.h"
#include <assert.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"


/****************************/
GFX_API GFXImage* gfx_load_image(GFXHeap* heap, GFXDependency* dep,
                                 const GFXReader* src)
{
	assert(heap != NULL);
	assert(dep != NULL);
	assert(src != NULL);

	// TODO: Implement.

	return NULL;
}
