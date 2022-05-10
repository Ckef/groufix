/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_PARSERS_GLTF_H
#define GFX_PARSERS_GLTF_H

#include "groufix/containers/io.h"
#include "groufix/core/heap.h"
#include "groufix/def.h"


/**
 * glTF 2.0 parsing result definition.
 */
typedef struct GFXGltfResult
{
	size_t        numPrimitives;
	GFXPrimitive* primitives;

} GFXGltfResult;


/**
 * Parses a glTF 2.0 stream into groufix objects.
 * @param heap   Heap to allocate resources from, cannot be NULL.
 * @param src    Source stream, cannot be NULL.
 * @param result Cannot be NULL, output parsing results.
 * @return Non-zero on success.
 */
GFX_API bool gfx_load_gltf(GFXHeap* heap, const GFXReader* src,
                           GFXGltfResult* result);

/**
 * Clears the result structure created by from gfx_load_gltf().
 * Does NOT destroy or free any of the stored groufix objects!
 * @param result Cannot be NULL.
 */
GFX_API void gfx_release_gltf(GFXGltfResult* result);


#endif
