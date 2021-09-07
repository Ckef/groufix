/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <assert.h>


#define _GFX_GET_FORMAT(elem) \
	(*(GFXFormat*)(elem))

#define _GFX_GET_VK_FORMAT(elem) \
	(*(VkFormat*)((char*)(elem) + sizeof(GFXFormat)))

#define _GFX_GET_VK_FORMAT_PROPERTIES(elem) \
	(*(VkFormatProperties)((char*)(elem) + sizeof(GFXFormat) + sizeof(VkFormat)))


/****************************/
int _gfx_device_init_formats(_GFXDevice* device)
{
	assert(device != NULL);

	// Initialize the format 'dictionary'.
	// i.e. a vector storing { GFXFormat, VkFormat, VkFormatProperties }.
	// We cannot use a map because we want to perform fuzzy search.
	gfx_vec_init(&device->formats,
		sizeof(GFXFormat) +
		sizeof(VkFormat) +
		sizeof(VkFormatProperties));

	return 1;
}

/****************************/
GFXFormatFeatures gfx_format_support(GFXFormat fmt, GFXDevice* device)
{
	//_GFXDevice* dev;
	//_GFX_GET_DEVICE(dev, device);

	return 0;
}

/****************************/
GFXFormat gfx_format_fuzzy(GFXFormat fmt, GFXFuzzyFlags flags, GFXDevice* device)
{
	//_GFXDevice* dev;
	//_GFX_GET_DEVICE(dev, device);

	return GFX_FORMAT_EMPTY;
}
