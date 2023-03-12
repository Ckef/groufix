/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_ASSETS_GLTF_H
#define GFX_ASSETS_GLTF_H

#include "groufix/assets/image.h"
#include "groufix/containers/io.h"
#include "groufix/core/deps.h"
#include "groufix/core/heap.h"
#include "groufix/core/renderer.h"
#include "groufix/def.h"


/**
 * glTF texture definition.
 */
typedef struct GFXGltfTexture
{
	size_t image;
	size_t sampler;

} GFXGltfTexture;


/**
 * glTF material definition.
 */
typedef struct GFXGltfMaterial
{
	GFXGltfTexture color;
	GFXGltfTexture metalicRoughness;
	GFXGltfTexture normal;
	GFXGltfTexture occlusion;
	GFXGltfTexture emissive;

	float colorFactors[4];
	float metalicFactor;
	float roughnessFactor;
	float normalScale;
	float occlusionStrength;
	float emissiveFactors[4];

} GFXGltfMaterial;


/**
 * glTF primitive definition.
 */
typedef struct GFXGltfPrimitive
{
	GFXPrimitive* primitive;
	size_t        material; // SIZE_MAX for none.

} GFXGltfPrimitive;


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

	size_t     numImages;
	GFXImage** images;

	size_t      numSamplers;
	GFXSampler* samplers;

	size_t           numMaterials;
	GFXGltfMaterial* materials;

	size_t            numPrimitives;
	GFXGltfPrimitive* primitives;

	size_t       numMeshes;
	GFXGltfMesh* meshes;

} GFXGltfResult;


/**
 * glTF 2.0 parsing options.
 */
typedef struct GFXGltfOptions
{
	size_t       orderSize;      // Size of `attributeOrder`.
	const char** attributeOrder; // Name index -> attribute location.
	size_t       maxAttributes;  // Per primitive, 0 for no limit.

} GFXGltfOptions;


/**
 * Parses a glTF 2.0 stream into groufix objects.
 * @param heap    Heap to allocate resources from, cannot be NULL.
 * @param dep     Dependency to inject signal commands in, cannot be NULL.
 * @param options Optional parsing options.
 * @param flags   Flags to influence the format for any allocated image.
 * @param usage   Image usage to use for any images.
 * @param src     Source stream, cannot be NULL.
 * @param inc     Optional stream includer.
 * @param result  Cannot be NULL, output parsing results.
 * @return Non-zero on success.
 */
GFX_API bool gfx_load_gltf(GFXHeap* heap, GFXDependency* dep,
                           const GFXGltfOptions* options,
                           GFXImageFlags flags, GFXImageUsage usage,
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
