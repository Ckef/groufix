/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/assets/gltf.h"
#include "groufix/core/log.h"
#include <assert.h>
#include <stdlib.h>

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

/****************************/
GFX_API bool gfx_load_gltf(GFXHeap* heap, const GFXReader* src,
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

	// Fail on error.
	if (res != cgltf_result_success)
	{
		gfx_log_error("Failed to load glTF: %s", _gfx_gltf_error_string(res));
		return 0;
	}

	// TODO: Continue implementing.

	cgltf_free(data);
	return 1;
}

/****************************/
GFX_API void gfx_release_gltf(GFXGltfResult* result)
{
	assert(result != NULL);

	// TODO: Implement.
}
