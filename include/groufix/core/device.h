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
 * Physical device type.
 * From most preferred to least preferred.
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

	int available; // Zero if it does not support the required Vulkan version.


	// Device features.
	struct
	{
		char indexUint32;
		char cubeArray;
		char geometryShader;
		char tessellationShader;
		char compressionBC;
		char compressionETC2; // Includes EAC compression.
		char compressionASTC;
		char shaderClipDistance;
		char shaderCullDistance;
		char shaderInt8;
		char shaderInt16;
		char shaderInt64;
		char shaderFloat16;
		char shaderFloat64;
		char samplerAnisotropy;
		char samplerClampToEdgeMirror;
		char samplerMinmax;

	} features;

	// Device limits.
	struct
	{
		uint32_t maxIndexUint32;
		uint32_t maxImageSize1D;   // For { width }.
		uint32_t maxImageSize2D;   // For { width, height }.
		uint32_t maxImageSize3D;   // For { width, height, depth }.
		uint32_t maxImageSizeCube; // For { width, height }.
		uint32_t maxImageLayers;
		uint32_t maxBufferTexels;
		uint32_t maxUniformBufferRange;
		uint32_t maxStorageBufferRange;
		uint32_t maxAttributes;
		uint32_t maxAttributeOffset;
		uint32_t maxAttributeStride;
		uint32_t maxPrimitiveBuffers;

		float maxMipLodBias;
		float maxAnisotropy;

		// Async-transfer image granularity (0,0,0 = only whole mip levels).
		struct { uint32_t x, y, z; } imageTransferGranularity;

	} limits;

} GFXDevice;


/**
 * Retrieves the number of initialized devices.
 * @return 0 if no devices were found.
 */
GFX_API size_t gfx_get_num_devices(void);

/**
 * Retrieves an initialized device.
 * The primary device is always stored at index 0 and stays constant.
 * @param index Must be < gfx_get_num_devices().
 */
GFX_API GFXDevice* gfx_get_device(size_t index);

/**
 * Retrieves the primary device.
 * This is equivalent to gfx_get_device(0).
 */
GFX_API GFXDevice* gfx_get_primary_device(void);


#endif
