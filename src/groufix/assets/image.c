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


/****************************
 * Constructs an image format based on:
 *  - If it is HDR (float).
 *  - If it is unsigned 16 bits integers.
 *  - How many components per pixel it has.
 */
static GFXFormat _gfx_stb_image_fmt(bool ishdr, bool is16, int comps)
{
	const unsigned char depth =
		ishdr ? 32 : is16 ? 16 : 8;

	const GFXFormatType type =
		ishdr ? GFX_SFLOAT : GFX_UNORM;

	const GFXOrder order =
		(comps == 1) ? GFX_ORDER_R :
		(comps == 2) ? GFX_ORDER_RG :
		(comps == 3) ? GFX_ORDER_RGB :
		(comps == 4) ? GFX_ORDER_RGBA : 0;

	return (GFXFormat){
		{
			comps > 0 ? depth : 0,
			comps > 1 ? depth : 0,
			comps > 2 ? depth : 0,
			comps > 3 ? depth : 0
		},
		type,
		order
	};
}

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

	// Get image properties.
	int x, y, comps;
	bool ishdr = stbi_is_hdr_from_memory(source, (int)len);
	bool is16 = stbi_is_16_bit_from_memory(source, (int)len);

	if (!stbi_info_from_memory(source, (int)len, &x, &y, &comps))
	{
		gfx_log_error(
			"Failed to retrieve image dimensions & components from stream: %s.",
			stbi_failure_reason());

		free(source);
		return NULL;
	}

	// Get appropriate format from properties.
	// TODO: Currently forced into a required supported format, make dynamic.
	// TODO: Prolly want to use gfx_format_support to get the format to use.
	int des = 4;
	ishdr = 0;
	is16 = 0;
	GFXFormat fmt = _gfx_stb_image_fmt(ishdr, is16, des);

	// Load/parse the image from memory.
	void* img;

	if (ishdr)
		img = stbi_loadf_from_memory(source, (int)len, &x, &y, &comps, des);
	else if (is16)
		img = stbi_load_16_from_memory(source, (int)len, &x, &y, &comps, des);
	else
		img = stbi_load_from_memory(source, (int)len, &x, &y, &comps, des);

	free(source); // Immediately free source buffer.

	if (img == NULL)
	{
		gfx_log_error(
			"Failed to load image from stream: %s.",
			stbi_failure_reason());

		return NULL;
	}

	// Allocate image.
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
		// TODO: Take into account the usage?
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
