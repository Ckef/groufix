/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_ASSETS_GLTF_H
#define GFX_ASSETS_GLTF_H

#include "groufix/containers/io.h"
#include "groufix/core/deps.h"
#include "groufix/core/heap.h"
#include "groufix/def.h"


/**
 * glTF mesh definition.
 */
typedef struct GFXGltfMesh
{
	size_t firstPrimitive;
	size_t numPrimitives;

} GFXGltfMesh;


/**
 * glTF 2.0 parsing result definition.
 */
typedef struct GFXGltfResult
{
	size_t      numBuffers;
	GFXBuffer** buffers;

	size_t         numPrimitives;
	GFXPrimitive** primitives;

	size_t       numMeshes;
	GFXGltfMesh* meshes;

} GFXGltfResult;


/**
 * Parses a glTF 2.0 stream into groufix objects.
 * @param heap   Heap to allocate resources from, cannot be NULL.
 * @param dep    Dependency to inject signal commands in, cannot be NULL.
 * @param src    Source stream, cannot be NULL.
 * @param inc    Stream includer, may be NULL.
 * @param result Cannot be NULL, output parsing results.
 * @return Non-zero on success.
 */
GFX_API bool gfx_load_gltf(GFXHeap* heap, GFXDependency* dep,
                           const GFXReader* src,
                           const GFXIncluder* inc,
                           GFXGltfResult* result);

/**
 * Clears the result structure created by gfx_load_gltf().
 * Does NOT destroy or free any of the stored groufix objects!
 * @param result Cannot be NULL.
 *
 * The content of result is invalidated after this call.
 */
GFX_API void gfx_release_gltf(GFXGltfResult* result);


#endif
