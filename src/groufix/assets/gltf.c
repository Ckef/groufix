/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/assets/gltf.h"
#include "groufix/containers/vec.h"
#include "groufix/core/log.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"


/****************************
 * Retrieves a cgltf_result as a readable string.
 */
static const char* _gfx_gltf_error_string(cgltf_result result)
{
	switch (result)
	{
	case cgltf_result_success:
		return "Success.";

	case cgltf_result_data_too_short:
		return "Data too short.";

	case cgltf_result_unknown_format:
		return "Unknown format.";

	case cgltf_result_invalid_json:
		return "Invalid JSON.";

	case cgltf_result_invalid_gltf:
		return "Invalid glTF.";

	case cgltf_result_out_of_memory:
		return "Out of memory.";

	case cgltf_result_legacy_gltf:
		return "Legacy glTF.";

	default:
		return "Unknown error.";
	}
}

/****************************
 * Decodes a base64 string into a newly allocated binary buffer.
 * @param size Size of the output buffer (_NOT_ of src) in bytes.
 * @return Must call free() on success!
 */
static void* _gfx_gltf_decode_base64(size_t size, const char* src)
{
	unsigned char* bin = malloc(size);
	if (bin == NULL) return NULL;

	unsigned int buff = 0;
	unsigned int bits = 0;

	// Loop over all output bytes.
	for (size_t i = 0; i < size; ++i)
	{
		// Loop over base64 digits until we have at least 8 bits.
		while (bits < 8)
		{
			char digit = *(src++);
			unsigned char index = (unsigned char)(
				(unsigned)(digit - 'A') < 26 ? (digit - 'A') :
				(unsigned)(digit - 'a') < 26 ? (digit - 'a') + 26 :
				(unsigned)(digit - '0') < 10 ? (digit - '0') + 52 :
				(digit == '+') ? 62 :
				(digit == '/') ? 63 :
				UCHAR_MAX);

			if (index > 63)
			{
				free(bin);
				return NULL;
			}

			buff = (buff << 6) | index;
			bits += 6;
		}

		// Output the oldest 8 bits.
		bits -= 8;
		bin[i] = (unsigned char)(buff >> bits);
	}

	return bin;
}

/****************************
 * TODO: Figure out buffer usage & access mask dynamically?
 * Allocates a new buffer and fills it with biven data.
 * @return NULL on failure.
 */
static GFXBuffer* _gfx_gltf_alloc_buffer(GFXHeap* heap, GFXDependency* dep,
                                         size_t size, const void* bin)
{
	// Allocate.
	GFXBuffer* buffer = gfx_alloc_buffer(heap,
		GFX_MEMORY_WRITE,
		GFX_BUFFER_VERTEX | GFX_BUFFER_INDEX,
		size);

	if (buffer == NULL) return NULL;

	// Write data.
	bool written = gfx_write(bin, gfx_ref_buffer(buffer),
		GFX_TRANSFER_ASYNC,
		1, dep != NULL ? 1 : 0,
		(GFXRegion[]){{ .offset = 0, .size = size }},
		(GFXRegion[]){{ .offset = 0, .size = size }},
		(GFXInject[]){
			gfx_dep_sig(dep,
				GFX_ACCESS_VERTEX_READ | GFX_ACCESS_INDEX_READ, 0)
		});

	if (!written)
	{
		gfx_free_buffer(buffer);
		return NULL;
	}

	return buffer;
}

/****************************/
GFX_API bool gfx_load_gltf(GFXHeap* heap, GFXDependency* dep,
                           const GFXReader* src,
                           GFXGltfResult* result)
{
	assert(heap != NULL);
	assert(src != NULL);
	assert(result != NULL);

	// Allocate source buffer.
	long long len = gfx_io_len(src);
	if (len <= 0)
	{
		gfx_log_error(
			"Zero or unknown stream length, cannot load glTF source.");

		return 0;
	}

	void* source = malloc((size_t)len);
	if (source == NULL)
	{
		gfx_log_error(
			"Could not allocate source buffer to load glTF source.");

		return 0;
	}

	// Read source.
	len = gfx_io_read(src, source, (size_t)len);
	if (len <= 0)
	{
		gfx_log_error(
			"Could not read glTF source from stream.");

		free(source);
		return 0;
	}

	// Parse the glTF source.
	cgltf_options options = {0};
	cgltf_data* data = NULL;

	cgltf_result res = cgltf_parse(&options, source, (size_t)len, &data);
	free(source); // Immediately free source buffer.

	// Some extra validation.
	if (res == cgltf_result_success) res = cgltf_validate(data);
	else data = NULL; // So we do not free garbage.

	// Fail on error.
	if (res != cgltf_result_success)
	{
		gfx_log_error("Failed to load glTF: %s", _gfx_gltf_error_string(res));
		cgltf_free(data);
		return 0;
	}

	// Setup some output vectors.
	// We are going to fill them with groufix equivalents of the glTF.
	// From this point onwards we need to clean on failure.
	GFXVec buffers;
	GFXVec primitives;
	gfx_vec_init(&buffers, sizeof(GFXBuffer*));
	gfx_vec_init(&primitives, sizeof(GFXPrimitive*));
	gfx_vec_reserve(&buffers, data->buffers_count);

	// Create all buffers.
	for (size_t b = 0; b < data->buffers_count; ++b)
	{
		GFXBuffer* buffer = NULL;
		const char* uri = data->buffers[b].uri;

		// Check if data URI.
		if (uri != NULL && strncmp(uri, "data:", 5) == 0)
		{
			const char* comma = strchr(uri, ',');

			// Check if base64.
			if (comma == NULL || comma - uri < 7 ||
				strncmp(comma - 7, ";base64", 7) != 0)
			{
				gfx_log_error("Data URI can only be base64.");
				goto clean;
			}

			// Decode.
			void* bin = _gfx_gltf_decode_base64(
				data->buffers[b].size, comma + 1);

			if (bin == NULL)
			{
				gfx_log_error("Failed to decode base64 data URI.");
				goto clean;
			}

			// Alloc buffer.
			buffer = _gfx_gltf_alloc_buffer(
				heap, dep, data->buffers[b].size, bin);

			free(bin);
			if (buffer == NULL) goto clean;
		}

		// Check if actual URI.
		else if (uri != NULL)
		{
			// TODO: Handle file URIs.
		}

		// Insert the buffer.
		if (!gfx_vec_push(&buffers, 1, &buffer))
		{
			gfx_free_buffer(buffer);
			goto clean;
		}
	}

	// TODO: Continue implementing.

	// We are done building groufix objects, free gltf things.
	cgltf_free(data);

	// Claim all data and return.
	result->numBuffers = buffers.size;
	result->buffers = gfx_vec_claim(&buffers);

	result->numPrimitives = primitives.size;
	result->primitives = gfx_vec_claim(&primitives);

	return 1;


	// Cleanup on failure.
clean:
	for (size_t b = 0; b < buffers.size; ++b)
		gfx_free_buffer(*(GFXBuffer**)gfx_vec_at(&buffers, b));

	for (size_t p = 0; p < primitives.size; ++p)
		gfx_free_prim(*(GFXPrimitive**)gfx_vec_at(&primitives, p));

	gfx_vec_clear(&buffers);
	gfx_vec_clear(&primitives);
	cgltf_free(data);

	gfx_log_error("Failed to load glTF from stream.");

	return 0;
}

/****************************/
GFX_API void gfx_release_gltf(GFXGltfResult* result)
{
	assert(result != NULL);

	free(result->buffers);
	free(result->primitives);

	// Leave all values, result is invalidated.
}
