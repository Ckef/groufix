/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_DEVICE_H
#define GFX_CORE_DEVICE_H

#include "groufix/def.h"


/**
 * Physical device type (from most preferred to least preferred).
 */
typedef enum GFXDeviceType
{
	GFX_DEVICE_DISCRETE_GPU,
	GFX_DEVICE_VIRTUAL_GPU,
	GFX_DEVICE_INTEGRATED_GPU,
	GFX_DEVICE_CPU,
	GFX_DEVICE_UNKNOWN

} GFXDeviceType;


/**
 * Physical device definition (e.g. a GPU).
 */
typedef struct GFXDevice
{
	// All read-only.
	GFXDeviceType type;
	const char*   name;

	const char* driverName;
	const char* driverInfo;

	bool available; // Zero if it does not support the required Vulkan version.


	// Device features.
	struct
	{
		bool indexUint8;
		bool indexUint32;
		bool indirectMultiDraw;
		bool indirectFirstInstance;
		bool cubeArray;
		bool multisampledStorageImage;
		bool geometryShader;
		bool tessellationShader;
		bool inlineCompute;
		bool rasterNonSolid;
		bool wideLines;
		bool largePoints;
		bool independentBlend;
		bool logicOp;
		bool depthBounds;
		bool compressionBC;
		bool compressionETC2; // Includes EAC compression.
		bool compressionASTC;
		bool samplerAnisotropy;
		bool samplerClampToEdgeMirror;
		bool samplerMinmax;

		bool shaderClipDistance;
		bool shaderCullDistance;
		bool shaderInt8;
		bool shaderInt16;
		bool shaderInt64;
		bool shaderFloat16;
		bool shaderFloat64;
		bool shaderPushConstant8;
		bool shaderPushConstant16;
		bool shaderInputOutput16;

		bool dynamicIndexUniformBuffer;
		bool dynamicIndexStorageBuffer;
		bool dynamicIndexUniformTexelBuffer;
		bool dynamicIndexStorageTexelBuffer;
		bool dynamicIndexSampledImage;
		bool dynamicIndexStorageImage;
		bool dynamicIndexAttachmentInput;

		bool nonUniformIndexUniformBuffer;
		bool nonUniformIndexStorageBuffer;
		bool nonUniformIndexUniformTexelBuffer;
		bool nonUniformIndexStorageTexelBuffer;
		bool nonUniformIndexSampledImage;
		bool nonUniformIndexStorageImage;
		bool nonUniformIndexAttachmentInput;

	} features;


	// Device limits.
	struct
	{
		uint32_t maxIndexValue;
		uint32_t maxImageSize1D;   // For { width }.
		uint32_t maxImageSize2D;   // For { width, height }.
		uint32_t maxImageSize3D;   // For { width, height, depth }.
		uint32_t maxImageSizeCube; // For { width, height }.
		uint32_t maxImageLayers;
		uint32_t maxBufferTexels;
		uint32_t maxUniformBufferRange;
		uint32_t maxStorageBufferRange;
		uint32_t maxPushConstantSize;
		uint32_t maxBoundSets;
		uint32_t maxComputeMemorySize;
		uint32_t maxAttributes;
		uint32_t maxAttributeOffset;
		uint32_t maxAttributeStride;
		uint32_t maxPrimitiveBuffers;
		uint32_t maxAttachmentWidth;
		uint32_t maxAttachmentHeight;
		uint32_t maxAttachmentLayers;
		uint32_t maxAttachmentOutputs; // Non-depth/stencil r/w attachments.

		uint32_t maxStageUniformBuffers;
		uint32_t maxStageStorageBuffers;
		uint32_t maxStageSampledImages;
		uint32_t maxStageStorageImages;
		uint32_t maxStageSamplers;
		uint32_t maxStageAttachmentInputs;

		uint32_t maxSetUniformBuffers; // Includes dynamic.
		uint32_t maxSetStorageBuffers; // Includes dynamic.
		uint32_t maxSetUniformBuffersDynamic; // Only dynamic.
		uint32_t maxSetStorageBuffersDynamic; // Only dynamic.
		uint32_t maxSetSampledImages;
		uint32_t maxSetStorageImages;
		uint32_t maxSetSamplers;
		uint32_t maxSetAttachmentInputs;

		uint64_t maxBufferSize;
		uint64_t minTexelBufferAlign;
		uint64_t minUniformBufferAlign;
		uint64_t minStorageBufferAlign;

		float minPointSize;
		float maxPointSize;
		float pointSizeGranularity;

		float minLineWidth;
		float maxLineWidth;
		float lineWidthGranularity;

		float maxMipLodBias;
		float maxAnisotropy;

		// Compute workgroup limits.
		struct { uint32_t x, y, z; } computeWorkGroupCount;
		struct { uint32_t x, y, z, total; } computeWorkGroupSize;

		// Async-transfer image granularity (0,0,0 = only whole mip levels).
		struct { uint32_t x, y, z; } imageTransferGranularity;


		// Supported samples-per-texel count bit-masks.
		//  Masks: 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40 (1 through 64).

		// Supported sample bit-masks for rendered-to attachments:
		//  - Output floating-point format.
		//  - Output integer format.
		//  - Depth format.
		//  - Stencil format.
		//  - No attachments.
		struct { uint8_t f, i, depth, stencil, empty; } renderSampleCounts;

		// Supported sample bit-masks for sampled/storage attachments:
		//  - Sampled floating-point format.
		//  - Sampled integer format.
		//  - Sampled depth format.
		//  - Sampled stencil format.
		//  - Any storage format.
		struct { uint8_t f, i, depth, stencil, storage; } imageSampleCounts;

	} limits;

} GFXDevice;


/**
 * Retrieves the number of initialized devices.
 * @return 0 if no devices were found.
 *
 * Can be called from any thread.
 */
GFX_API size_t gfx_get_num_devices(void);

/**
 * Retrieves an initialized device.
 * The primary device is always stored at index 0, all subsequent devices are
 * sorted from most to least preferred, unavailable devices are always last.
 * @param index Must be < gfx_get_num_devices().
 *
 * Can be called from any thread.
 */
GFX_API GFXDevice* gfx_get_device(size_t index);

/**
 * Retrieves the primary device.
 * This is equivalent to gfx_get_device(0).
 *
 * Can be called from any thread.
 */
GFX_API GFXDevice* gfx_get_primary_device(void);


#endif
