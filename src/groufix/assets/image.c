/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/assets/image.h"
#include "groufix/core/log.h"
#include <assert.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#define STBI_NO_STDIO
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"


/****************************/
GFX_API GFXImage* gfx_load_image(GFXHeap* heap, GFXDependency* dep,
                                 GFXImageUsage usage,
                                 const GFXReader* src)
{
	assert(heap != NULL);
	assert(dep != NULL);
	assert(src != NULL);

	// Allocate source buffer.
	long long len = gfx_io_len(src);
	if (len <= 0)
	{
		gfx_log_error(
			"Zero or unknown stream length, cannot load image source.");

		return NULL;
	}

	void* source = malloc((size_t)len);
	if (source == NULL)
	{
		gfx_log_error(
			"Could not allocate source buffer to load image source.");

		return NULL;
	}

	// Read source.
	len = gfx_io_read(src, source, (size_t)len);
	if (len <= 0)
	{
		gfx_log_error(
			"Could not read glTF source from stream.");

		free(source);
		return NULL;
	}

	// Load/parse the image from memory.
	const bool ishdr = stbi_is_hdr_from_memory(source, (int)len);
	const bool is16 = stbi_is_16_bit_from_memory(source, (int)len);

	int x, y, comps;
	void* img;

	if (ishdr)
		img = stbi_loadf_from_memory(source, (int)len, &x, &y, &comps, 0);
	else if (is16)
		img = stbi_load_16_from_memory(source, (int)len, &x, &y, &comps, 0);
	else
		img = stbi_load_from_memory(source, (int)len, &x, &y, &comps, 0);

	free(source); // Immediately free source buffer.

	if (img == NULL)
	{
		gfx_log_error(
			"Failed to load image from stream: %s.",
			stbi_failure_reason());

		return NULL;
	}

	// Allocate image.
	const unsigned char depth =
		ishdr ? 32 : is16 ? 16 : 8;

	const GFXFormatType type =
		ishdr ? GFX_SFLOAT : GFX_UNORM;

	const GFXOrder order =
		(comps == 1) ? GFX_ORDER_R :
		(comps == 2) ? GFX_ORDER_RG :
		(comps == 3) ? GFX_ORDER_RGB :
		(comps == 4) ? GFX_ORDER_RGBA : 0;

	const GFXFormat fmt = {
		{
			comps > 0 ? depth : 0,
			comps > 1 ? depth : 0,
			comps > 2 ? depth : 0,
			comps > 3 ? depth : 0
		},
		type,
		order
	};

	GFXImage* image = gfx_alloc_image(heap,
		GFX_IMAGE_2D, GFX_MEMORY_WRITE,
		usage, fmt, 1, 1, (uint32_t)x, (uint32_t)y, 1);

	if (image == NULL) goto clean;

	// Write data.
	const GFXRegion srcRegion = {
		.offset = 0,
		.rowSize = 0,
		.numRows = 0
	};

	const GFXRegion dstRegion = {
		.aspect = GFX_IMAGE_COLOR,
		.mipmap = 0,
		.layer = 0,
		.numLayers = 1,
		.x = 0,
		.y = 0,
		.z = 0,
		.width = (uint32_t)x,
		.height = (uint32_t)y,
		.depth = 1
	};

	const GFXInject inject =
		gfx_dep_sig(dep, GFX_ACCESS_SAMPLED_READ, 0);

	if (!gfx_write(img, gfx_ref_image(image),
		GFX_TRANSFER_ASYNC,
		1, 1, &srcRegion, &dstRegion, &inject))
	{
		gfx_free_image(image);
		goto clean;
	}

	// Free the parsed data and return.
	stbi_image_free(img);

	return image;


	// Cleanup on failure.
clean:
	stbi_image_free(img);
	gfx_log_error("Failed to load image from stream.");

	return NULL;
}
