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


// Checks if the format has features to support the requested usage.
#define _GFX_STB_FMT_SUPPORTED(usage, feats) \
	((feats & GFX_FORMAT_IMAGE_WRITE) && \
	((feats & GFX_FORMAT_SAMPLED_IMAGE) || !(usage & GFX_IMAGE_SAMPLED)) && \
	((feats & GFX_FORMAT_SAMPLED_IMAGE_LINEAR) || !(usage & GFX_IMAGE_SAMPLED_LINEAR)) && \
	((feats & GFX_FORMAT_SAMPLED_IMAGE_MINMAX) || !(usage & GFX_IMAGE_SAMPLED_MINMAX)) && \
	((feats & GFX_FORMAT_STORAGE_IMAGE) || !(usage & GFX_IMAGE_STORAGE)) && \
	((feats & GFX_FORMAT_ATTACHMENT_BLEND) || !(usage & GFX_IMAGE_BLEND)) && \
	((feats & GFX_FORMAT_ATTACHMENT) || \
		!((GFX_IMAGE_INPUT | GFX_IMAGE_OUTPUT | GFX_IMAGE_TRANSIENT) & usage)))


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
                                 GFXImageFlags flags, GFXImageUsage usage,
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
			"Could not read image source from stream.");

		free(source);
		return NULL;
	}

	// Get image properties.
	int x, y, sComps;
	const bool sIshdr = stbi_is_hdr_from_memory(source, (int)len);
	const bool sIs16 = stbi_is_16_bit_from_memory(source, (int)len);

	if (!stbi_info_from_memory(source, (int)len, &x, &y, &sComps))
	{
		gfx_log_error(
			"Failed to retrieve image dimensions & components from stream: %s.",
			stbi_failure_reason());

		free(source);
		return NULL;
	}

	if (x <= 0 || y <= 0 || sComps <= 0 || sComps > 4)
	{
		gfx_log_error(
			"Cannot load image from stream, invalid dimensions: %dx%d:%d",
			x, y, sComps);

		free(source);
		return NULL;
	}

	// Get appropriate format from properties.
	// We check if it is supported, if not we;
	// firstly try out bigger orders and secondly try out smaller types.
	// This will eventually result in an 8-bit format with 4 components,
	// which is required to be supported by Vulkan!
	GFXDevice* device = gfx_heap_get_device(heap);
	GFXFormat fmt = _gfx_stb_image_fmt(sIshdr, sIs16, sComps);
	GFXFormatFeatures feats = gfx_format_support(fmt, device);

	int comps = sComps;
	bool ishdr = sIshdr;
	bool is16 = sIs16;

	while (!_GFX_STB_FMT_SUPPORTED(usage, feats))
	{
		// Try smaller type first, then bigger order.
		if (flags & GFX_IMAGE_TYPE_BEFORE_ORDER)
		{
			if ((ishdr || is16) && !(flags & GFX_IMAGE_KEEP_TYPE))
			{
				if (ishdr) ishdr = 0, is16 = 1;
				else is16 = 0;
			}
			else if (!(flags & GFX_IMAGE_KEEP_ORDER))
			{
				ishdr = sIshdr;
				is16 = sIs16;
				if (comps < 4) ++comps;
				else break; // None found.
			}
			else break; // None found.
		}

		// Try bigger order first, then smaller type (default!).
		else
		{
			if (comps < 4 && !(flags & GFX_IMAGE_KEEP_ORDER))
			{
				++comps;
			}
			else if (!(flags & GFX_IMAGE_KEEP_TYPE))
			{
				comps = sComps;
				if (ishdr) ishdr = 0, is16 = 1;
				else if (is16) is16 = 0;
				else break; // None found.
			}
			else break; // None found.
		}

		fmt = _gfx_stb_image_fmt(ishdr, is16, comps);
		feats = gfx_format_support(fmt, device);
	}

	// Uh oh.
	if (!_GFX_STB_FMT_SUPPORTED(usage, feats))
	{
		gfx_log_error(
			"No suitable supported format to load image from stream.");

		free(source);
		return NULL;
	}

	// Load/parse the image from memory.
	void* img;

	if (ishdr)
		img = stbi_loadf_from_memory(source, (int)len, &x, &y, &comps, comps);
	else if (is16)
		img = stbi_load_16_from_memory(source, (int)len, &x, &y, &comps, comps);
	else
		img = stbi_load_from_memory(source, (int)len, &x, &y, &comps, comps);

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

	const GFXAccessMask mask =
		((image->usage & GFX_IMAGE_SAMPLED) ||
		(image->usage & GFX_IMAGE_SAMPLED_LINEAR) ||
		(image->usage & GFX_IMAGE_SAMPLED_MINMAX) ?
			GFX_ACCESS_SAMPLED_READ : 0) |
		((image->usage & GFX_IMAGE_STORAGE) ?
			GFX_ACCESS_STORAGE_READ_WRITE : 0);

	const GFXInject inject =
		gfx_dep_sig(dep, mask, GFX_STAGE_ANY);

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
