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


/****************************/
GFX_API bool gfx_load_gltf(const GFXReader* src)
{
	assert(src != NULL);

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

	cgltf_result result = cgltf_parse(&options, source, (size_t)len, &data);
	free(source); // Immediately free source buffer.

	// Some useful logging.
	switch (result)
	{
	case cgltf_result_unknown_format:
		gfx_log_error("Failed to load glTF; unknown format.");
		return 0;

	case cgltf_result_invalid_json:
		gfx_log_error("Failed to load glTF; invalid JSON.");
		return 0;

	case cgltf_result_data_too_short:
	case cgltf_result_invalid_gltf:
		gfx_log_error("Failed to load glTF; contains invalid source.");
		return 0;

	case cgltf_result_legacy_gltf:
		gfx_log_error("Failed to load glTF; contains legacy source.");
		return 0;

	default:
		if (result != cgltf_result_success)
		{
			gfx_log_error("Failed to load glTF; unknown reason.");
			return 0;
		}
		break;
	}

	// TODO: Continue implement.

	cgltf_free(data);
	return 1;
}
