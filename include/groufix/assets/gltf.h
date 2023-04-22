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
 * glTF node feature flags.
 */
typedef enum GFXGltfNodeFlags
{
	GFX_GLTF_NODE_TRANSLATION = 0x0001,
	GFX_GLTF_NODE_ROTATION    = 0x0002,
	GFX_GLTF_NODE_SCALE       = 0x0004

} GFXGltfNodeFlags;

GFX_BIT_FIELD(GFXGltfNodeFlags)


/**
 * glTF material feature flags.
 */
typedef enum GFXGltfMaterialFlags
{
	GFX_GLTF_MATERIAL_PBR_METALLIC_ROUGHNESS  = 0x0001,
	GFX_GLTF_MATERIAL_PBR_SPECULAR_GLOSSINESS = 0x0002,
	GFX_GLTF_MATERIAL_IOR                     = 0x0004,
	GFX_GLTF_MATERIAL_EMISSIVE_STRENGTH       = 0x0008,
	GFX_GLTF_MATERIAL_CLEARCOAT               = 0x0010,
	GFX_GLTF_MATERIAL_IRIDESCENCE             = 0x0020,
	GFX_GLTF_MATERIAL_SHEEN                   = 0x0040,
	GFX_GLTF_MATERIAL_SPECULAR                = 0x0080,
	GFX_GLTF_MATERIAL_TRANSMISSION            = 0x0100,
	GFX_GLTF_MATERIAL_VOLUME                  = 0x0200,
	GFX_GLTF_MATERIAL_UNLIT                   = 0x0400,
	GFX_GLTF_MATERIAL_DOUBLE_SIDED            = 0x0800

} GFXGltfMaterialFlags;

GFX_BIT_FIELD(GFXGltfMaterialFlags)


/**
 * glTF material alpha mode.
 */
typedef enum GFXGltfAlphaMode
{
	GFX_GLTF_ALPHA_OPAQUE,
	GFX_GLTF_ALPHA_MASK,
	GFX_GLTF_ALPHA_BLEND

} GFXGltfAlphaMode;


/**
 * glTF buffer definition.
 */
typedef struct GFXGltfBuffer
{
	size_t     size;
	void*      bin;
	GFXBuffer* buffer;

} GFXGltfBuffer;


/**
 * glTF sampler definition.
 */
typedef struct GFXGltfSampler
{
	GFXFilter minFilter;
	GFXFilter magFilter;
	GFXFilter mipFilter;

	GFXWrapping wrapU;
	GFXWrapping wrapV;

} GFXGltfSampler;


/**
 * glTF texture definition.
 */
typedef struct GFXGltfTexture
{
	GFXImage*       image;
	GFXGltfSampler* sampler;

} GFXGltfTexture;


/**
 * glTF material definition.
 */
typedef struct GFXGltfMaterial
{
	// All used features.
	GFXGltfMaterialFlags flags;

	// Physically based rendering.
	struct
	{
		// Metallic roughness.
		GFXGltfTexture baseColor;
		GFXGltfTexture metallicRoughness;

		float baseColorFactors[4];
		float metallicFactor;
		float roughnessFactor;
		float ior;

		// Specular glossiness.
		GFXGltfTexture diffuse;
		GFXGltfTexture specularGlossiness;

		float diffuseFactors[4];
		float specularFactors[3];
		float glossinessFactor;

	} pbr;

	// Standard.
	GFXGltfTexture normal;
	GFXGltfTexture occlusion;
	GFXGltfTexture emissive;

	GFXGltfAlphaMode alphaMode;

	float normalScale;
	float occlusionStrength;
	float emissiveFactors[3];
	float emissiveStrength;
	float alphaCutoff;

	// Clearcoat.
	GFXGltfTexture clearcoat;
	GFXGltfTexture clearcoatRoughness;
	GFXGltfTexture clearcoatNormal;

	float clearcoatFactor;
	float clearcoatRoughnessFactor;

	// Iridescence
	GFXGltfTexture iridescence;
	GFXGltfTexture iridescenceThickness;

	float iridescenceFactor;
	float iridescenceIor;
	float iridescenceThicknessMin;
	float iridescenceThicknessMax;

	// Sheen.
	GFXGltfTexture sheenColor;
	GFXGltfTexture sheenRoughness;

	float sheenColorFactors[3];
	float sheenRoughnessFactor;

	// Specular.
	GFXGltfTexture specular;
	GFXGltfTexture specularColor;

	float specularFactor;
	float specularColorFactors[3];

	// Transmission.
	GFXGltfTexture transmission;

	float transmissionFactor;

	// Volume.
	GFXGltfTexture thickness;

	float thicknessFactor;
	float attenuationColors[3];
	float attenuationDistance;

} GFXGltfMaterial;


/**
 * glTF primitive definition.
 */
typedef struct GFXGltfPrimitive
{
	GFXPrimitive*    primitive;
	GFXGltfMaterial* material;

} GFXGltfPrimitive;


/**
 * glTF mesh definition.
 */
typedef struct GFXGltfMesh
{
	size_t            numPrimitives;
	GFXGltfPrimitive* primitives;

} GFXGltfMesh;


/**
 * glTF node definition.
 */
typedef struct GFXGltfNode
{
	// All used features.
	GFXGltfNodeFlags flags;

	struct GFXGltfNode*  parent;
	struct GFXGltfNode** children;
	size_t               numChildren;

	float matrix[16];
	float translation[3];
	float rotation[4];
	float scale[3];

	GFXGltfMesh* mesh;

} GFXGltfNode;


/**
 * glTF scene definition.
 */
typedef struct GFXGltfScene
{
	size_t        numNodes;
	GFXGltfNode** nodes;

} GFXGltfScene;


/**
 * glTF 2.0 parsing result definition.
 */
typedef struct GFXGltfResult
{
	// Default scene.
	GFXGltfScene* scene;

	size_t         numBuffers;
	GFXGltfBuffer* buffers;

	size_t     numImages;
	GFXImage** images;

	size_t          numSamplers;
	GFXGltfSampler* samplers;

	size_t           numMaterials;
	GFXGltfMaterial* materials;

	size_t            numPrimitives;
	GFXGltfPrimitive* primitives;

	size_t       numMeshes;
	GFXGltfMesh* meshes;

	size_t       numNodes;
	GFXGltfNode* nodes;

	size_t        numScenes;
	GFXGltfScene* scenes;

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
 * Does NOT destroy or free any of the heap-allocated groufix objects!
 * @param result Cannot be NULL.
 *
 * The content of result is invalidated after this call.
 */
GFX_API void gfx_release_gltf(GFXGltfResult* result);


#endif
