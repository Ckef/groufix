/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>


#define _GFX_GET_DEVICE_PROC_ADDR(pName) \
	do { \
		context->vk.pName = (PFN_vk##pName)_groufix.vk.GetDeviceProcAddr( \
			context->vk.device, "vk"#pName); \
		if (context->vk.pName == NULL) { \
			gfx_log_error("Could not load vk"#pName"."); \
			goto clean; \
		} \
	} while (0)

#define _GFX_GET_DEVICE_TYPE(vType) \
	((vType) == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? \
		GFX_DEVICE_DISCRETE_GPU : \
	(vType) == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? \
		GFX_DEVICE_VIRTUAL_GPU : \
	(vType) == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? \
		GFX_DEVICE_INTEGRATED_GPU : \
	(vType) == VK_PHYSICAL_DEVICE_TYPE_CPU ? \
		GFX_DEVICE_CPU : \
		GFX_DEVICE_UNKNOWN)


// Gets the complete set of queue flags (adding optional left out bits).
#define _GFX_QUEUE_FLAGS_ALL(vFlags) \
	((vFlags) & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT) ? \
		(vFlags) | VK_QUEUE_TRANSFER_BIT : (vFlags))

// Counts the number of (relevant) set bits in a set of queue flags.
#define _GFX_QUEUE_FLAGS_COUNT(vFlags) \
	(((vFlags) & VK_QUEUE_GRAPHICS_BIT ? 1 : 0) + \
	((vFlags) & VK_QUEUE_COMPUTE_BIT ? 1 : 0) + \
	((vFlags) & VK_QUEUE_TRANSFER_BIT ? 1 : 0))


// Gets device version support & features support per version (all lvalues).
#define _GFX_GET_DEVICE_FEATURES(device, \
		vk11, vk12, vk13, vk14, \
		pdf, pdv11f, pdv12f, pdv13f, pdv14f) \
	do { \
		vk11 = (device)->api >= VK_MAKE_API_VERSION(0,1,1,0); \
		vk12 = (device)->api >= VK_MAKE_API_VERSION(0,1,2,0); \
		vk13 = (device)->api >= VK_MAKE_API_VERSION(0,1,3,0); \
		vk14 = (device)->api >= VK_MAKE_API_VERSION(0,1,4,0); \
		_gfx_get_device_features(device, \
			&pdf, \
			(vk11 ? &pdv11f : NULL), \
			(vk12 ? &pdv12f : NULL), \
			(vk13 ? &pdv13f : NULL), \
			(vk14 ? &pdv14f : NULL)); \
	} while (0)


/****************************
 * Stringified device types for logging.
 */
static const char* _gfx_log_device_types[] = {
	"GPU", "Virtual GPU", "Integrated GPU", "CPU", "Unknown device type"
};


/****************************
 * Array of Vulkan queue priority values in [0,1].
 * The only separate queues that may be allocated within the same family are
 *  { (graphics|present), compute, transfer }
 *
 * where the graphics queue (the first) always gets priority over others.
 * This is _ALWAYS_ the order of queues adhered to in the entire engine.
 * If a queue is not present in a set, the next in order takes its place.
 */
static const float _gfx_vk_queue_priorities[] = { 1.0f, 0.5f, 0.5f };


/****************************
 * Finds whether or not substr occurs in str.
 * All inputs must be NULL-terminated or NULL.
 * Comparison is case insensitive.
 */
static bool _gfx_contains_substr(const char* str, const char* substr)
{
	if (str == NULL || substr == NULL) return 0;

	const char* haystack = str;
	const char* needle = substr;
	const char* found = NULL;

	while (*haystack != '\0' && *needle != '\0')
	{
		if (tolower(*haystack) == tolower(*needle))
		{
			if (found == NULL) found = haystack;
			++needle;
		}
		else
		{
			if (found != NULL) haystack = found;
			needle = substr;
			found = NULL;
		}

		++haystack;
	}

	return *needle == '\0';
}


#if defined (GFX_USE_VK_SUBSET_DEVICES)

/****************************
 * Checks whether a given physical Vulkan device exposes the
 * VK_KHR_portability_subset extension.
 */
static bool _gfx_device_is_subset(VkPhysicalDevice device)
{
	bool subset = 0;

	uint32_t extCount;
	_GFX_VK_CHECK(_groufix.vk.EnumerateDeviceExtensionProperties(
		device, NULL, &extCount, NULL), extCount = 0);

	if (extCount > 0)
	{
		// May be many extensions, allocate on heap!
		VkExtensionProperties* extProps =
			calloc(extCount, sizeof(VkExtensionProperties));

		if (extProps != NULL)
		{
			_GFX_VK_CHECK(_groufix.vk.EnumerateDeviceExtensionProperties(
				device, NULL, &extCount, extProps), extCount = 0);

			for (uint32_t e = 0; e < extCount; ++e)
				if (strcmp(
					extProps[e].extensionName,
					"VK_KHR_portability_subset") == 0)
				{
					subset = 1;
					break;
				}

			free(extProps);
		}
	}

	return subset;
}

#endif


/****************************
 * Fills a VkPhysicalDeviceFeatures struct with features to enable,
 * in other words; it disables feature we don't want.
 * @param device Cannot be NULL, only device->{ api, vk.device } need to be set.
 * @param pdf    Output Vulkan 1.0 data, cannot be NULL.
 * @param pdv11f Output Vulkan 1.1 data, may be NULL.
 * @param pdv12f Output Vulkan 1.2 data, may be NULL.
 * @param pdv13f Output Vulkan 1.3 data, may be NULL.
 * @param pdv14f Output Vulkan 1.4 data, may be NULL.
 *
 * All output structure for Vulkan >= 1.1 will link to one another in order
 * of increasing Vulkan version.
 */
static void _gfx_get_device_features(_GFXDevice* device,
                                     VkPhysicalDeviceFeatures* pdf,
                                     VkPhysicalDeviceVulkan11Features* pdv11f,
                                     VkPhysicalDeviceVulkan12Features* pdv12f,
                                     VkPhysicalDeviceVulkan13Features* pdv13f,
                                     VkPhysicalDeviceVulkan14Features* pdv14f)
{
	assert(device != NULL);
	assert(device->vk.device != NULL);
	assert(!pdv11f || device->api >= VK_MAKE_API_VERSION(0,1,1,0));
	assert(!pdv12f || device->api >= VK_MAKE_API_VERSION(0,1,2,0));
	assert(!pdv13f || device->api >= VK_MAKE_API_VERSION(0,1,3,0));
	assert(!pdv14f || device->api >= VK_MAKE_API_VERSION(0,1,4,0));
	assert(pdf != NULL);

	VkPhysicalDeviceFeatures2 pdf2 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = pdv11f ? (void*)pdv11f : (void*)pdv12f
	};

	if (pdv11f) *pdv11f = (VkPhysicalDeviceVulkan11Features){
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
		.pNext = pdv12f
	};

	if (pdv12f) *pdv12f = (VkPhysicalDeviceVulkan12Features){
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = pdv13f
	};

	if (pdv13f) *pdv13f = (VkPhysicalDeviceVulkan13Features){
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.pNext = pdv14f
	};

	if (pdv14f) *pdv14f = (VkPhysicalDeviceVulkan14Features){
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
		.pNext = NULL
	};

	if (!pdv11f && !pdv12f && !pdv13f && !pdv14f)
		_groufix.vk.GetPhysicalDeviceFeatures(device->vk.device, pdf);
	else
	{
		_groufix.vk.GetPhysicalDeviceFeatures2(device->vk.device, &pdf2);
		*pdf = pdf2.features;
	}

#if defined (NDEBUG)
	pdf->robustBufferAccess                     = VK_FALSE;
#endif
	pdf->sampleRateShading                      = VK_FALSE;
	pdf->dualSrcBlend                           = VK_FALSE;
	pdf->depthClamp                             = VK_FALSE;
	pdf->depthBiasClamp                         = VK_FALSE;
	pdf->alphaToOne                             = VK_FALSE;
	pdf->multiViewport                          = VK_FALSE;
	pdf->occlusionQueryPrecise                  = VK_FALSE;
	pdf->pipelineStatisticsQuery                = VK_FALSE;
	pdf->vertexPipelineStoresAndAtomics         = VK_FALSE;
	pdf->fragmentStoresAndAtomics               = VK_FALSE;
	pdf->shaderTessellationAndGeometryPointSize = VK_FALSE;
	pdf->shaderImageGatherExtended              = VK_FALSE;
	pdf->shaderStorageImageExtendedFormats      = VK_FALSE;
	pdf->shaderStorageImageReadWithoutFormat    = VK_FALSE;
	pdf->shaderStorageImageWriteWithoutFormat   = VK_FALSE;
	pdf->shaderResourceResidency                = VK_FALSE;
	pdf->shaderResourceMinLod                   = VK_FALSE;
	pdf->sparseBinding                          = VK_FALSE;
	pdf->sparseResidencyBuffer                  = VK_FALSE;
	pdf->sparseResidencyImage2D                 = VK_FALSE;
	pdf->sparseResidencyImage3D                 = VK_FALSE;
	pdf->sparseResidency2Samples                = VK_FALSE;
	pdf->sparseResidency4Samples                = VK_FALSE;
	pdf->sparseResidency8Samples                = VK_FALSE;
	pdf->sparseResidency16Samples               = VK_FALSE;
	pdf->sparseResidencyAliased                 = VK_FALSE;
	pdf->variableMultisampleRate                = VK_FALSE;
	pdf->inheritedQueries                       = VK_FALSE;

	if (pdv11f)
	{
		pdv11f->storageBuffer16BitAccess           = VK_FALSE;
		pdv11f->uniformAndStorageBuffer16BitAccess = VK_FALSE;
		pdv11f->multiview                          = VK_FALSE;
		pdv11f->multiviewGeometryShader            = VK_FALSE;
		pdv11f->multiviewTessellationShader        = VK_FALSE;
		pdv11f->variablePointersStorageBuffer      = VK_FALSE;
		pdv11f->variablePointers                   = VK_FALSE;
		pdv11f->protectedMemory                    = VK_FALSE;
		pdv11f->samplerYcbcrConversion             = VK_FALSE;
		pdv11f->shaderDrawParameters               = VK_FALSE;
	}

	if (pdv12f)
	{
		pdv12f->drawIndirectCount                                  = VK_FALSE;
		pdv12f->storageBuffer8BitAccess                            = VK_FALSE;
		pdv12f->uniformAndStorageBuffer8BitAccess                  = VK_FALSE;
		pdv12f->shaderBufferInt64Atomics                           = VK_FALSE;
		pdv12f->shaderSharedInt64Atomics                           = VK_FALSE;
		pdv12f->descriptorIndexing                                 = VK_FALSE;
		pdv12f->descriptorBindingUniformBufferUpdateAfterBind      = VK_FALSE;
		pdv12f->descriptorBindingSampledImageUpdateAfterBind       = VK_FALSE;
		pdv12f->descriptorBindingStorageImageUpdateAfterBind       = VK_FALSE;
		pdv12f->descriptorBindingStorageBufferUpdateAfterBind      = VK_FALSE;
		pdv12f->descriptorBindingUniformTexelBufferUpdateAfterBind = VK_FALSE;
		pdv12f->descriptorBindingStorageTexelBufferUpdateAfterBind = VK_FALSE;
		pdv12f->descriptorBindingUpdateUnusedWhilePending          = VK_FALSE;
		pdv12f->descriptorBindingPartiallyBound                    = VK_FALSE;
		pdv12f->descriptorBindingVariableDescriptorCount           = VK_FALSE;
		pdv12f->runtimeDescriptorArray                             = VK_FALSE;
		pdv12f->scalarBlockLayout                                  = VK_FALSE;
		pdv12f->imagelessFramebuffer                               = VK_FALSE;
		pdv12f->uniformBufferStandardLayout                        = VK_FALSE;
		pdv12f->shaderSubgroupExtendedTypes                        = VK_FALSE;
		pdv12f->separateDepthStencilLayouts                        = VK_FALSE;
		pdv12f->hostQueryReset                                     = VK_FALSE;
		pdv12f->timelineSemaphore                                  = VK_FALSE;
		pdv12f->bufferDeviceAddress                                = VK_FALSE;
		pdv12f->bufferDeviceAddressCaptureReplay                   = VK_FALSE;
		pdv12f->bufferDeviceAddressMultiDevice                     = VK_FALSE;
		pdv12f->vulkanMemoryModel                                  = VK_FALSE;
		pdv12f->vulkanMemoryModelDeviceScope                       = VK_FALSE;
		pdv12f->vulkanMemoryModelAvailabilityVisibilityChains      = VK_FALSE;
		pdv12f->shaderOutputViewportIndex                          = VK_FALSE;
		pdv12f->shaderOutputLayer                                  = VK_FALSE;
		pdv12f->subgroupBroadcastDynamicId                         = VK_FALSE;
	}

	if (pdv13f)
	{
#if defined (NDEBUG)
		pdv13f->robustImageAccess                                  = VK_FALSE;
#endif
		pdv13f->inlineUniformBlock                                 = VK_FALSE;
		pdv13f->descriptorBindingInlineUniformBlockUpdateAfterBind = VK_FALSE;
		pdv13f->pipelineCreationCacheControl                       = VK_FALSE;
		pdv13f->privateData                                        = VK_FALSE;
		pdv13f->shaderDemoteToHelperInvocation                     = VK_FALSE;
		pdv13f->shaderTerminateInvocation                          = VK_FALSE;
		pdv13f->subgroupSizeControl                                = VK_FALSE;
		pdv13f->computeFullSubgroups                               = VK_FALSE;
		pdv13f->synchronization2                                   = VK_FALSE;
		pdv13f->textureCompressionASTC_HDR                         = VK_FALSE;
		pdv13f->shaderZeroInitializeWorkgroupMemory                = VK_FALSE;
		pdv13f->dynamicRendering                                   = VK_FALSE;
	}

	if (pdv14f)
	{
		pdv14f->globalPriorityQuery                    = VK_FALSE;
		pdv14f->shaderSubgroupRotate                   = VK_FALSE;
		pdv14f->shaderSubgroupRotateClustered          = VK_FALSE;
		pdv14f->shaderFloatControls2                   = VK_FALSE;
		pdv14f->shaderExpectAssume                     = VK_FALSE;
		pdv14f->rectangularLines                       = VK_FALSE;
		pdv14f->bresenhamLines                         = VK_FALSE;
		pdv14f->smoothLines                            = VK_FALSE;
		pdv14f->stippledRectangularLines               = VK_FALSE;
		pdv14f->stippledBresenhamLines                 = VK_FALSE;
		pdv14f->stippledSmoothLines                    = VK_FALSE;
		pdv14f->vertexAttributeInstanceRateDivisor     = VK_FALSE;
		pdv14f->vertexAttributeInstanceRateZeroDivisor = VK_FALSE;
		pdv14f->dynamicRenderingLocalRead              = VK_FALSE;
		pdv14f->maintenance5                           = VK_FALSE;
		pdv14f->maintenance6                           = VK_FALSE;
		pdv14f->pipelineProtectedAccess                = VK_FALSE;
		pdv14f->pipelineRobustness                     = VK_FALSE;
		pdv14f->hostImageCopy                          = VK_FALSE;
		pdv14f->pushDescriptor                         = VK_FALSE;
	}
}

/****************************
 * Retrieves the device group a device is part of.
 * @param context Populates its numDevices and devices members.
 * @param device  Cannot be NULL.
 * @param index   Output device index into the group, cannot be NULL.
 * @return Zero on failure.
 */
static bool _gfx_get_device_group(_GFXContext* context, _GFXDevice* device,
                                  size_t* index)
{
	assert(context != NULL);
	assert(device != NULL);
	assert(index != NULL);

	// Enumerate all device groups.
	uint32_t cnt;
	_GFX_VK_CHECK(_groufix.vk.EnumeratePhysicalDeviceGroups(
		_groufix.vk.instance, &cnt, NULL), return 0);

	if (cnt == 0) return 0;

	VkPhysicalDeviceGroupProperties groups[cnt];

	for (uint32_t g = 0; g < cnt; ++g)
		groups[g].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES,
		groups[g].pNext = NULL;

	_GFX_VK_CHECK(_groufix.vk.EnumeratePhysicalDeviceGroups(
		_groufix.vk.instance, &cnt, groups), return 0);

	// Loop over all groups and see if one contains the device.
	// We take the first device group we find it in, this assumes a device is
	// never seen in multiple groups, which should be reasonable...
	size_t g, i;

	for (g = 0; g < cnt; ++g)
	{
		for (i = 0; i < groups[g].physicalDeviceCount; ++i)
			if (groups[g].physicalDevices[i] == device->vk.device)
				break;

		if (i < groups[g].physicalDeviceCount)
			break;
	}

	if (g >= cnt)
	{
		// Probably want to know when a device is somehow invalid..
		gfx_log_error(
			"[ %s ] could not be found in any device group.",
			device->name);

		return 0;
	}

	*index = i;
	context->numDevices = groups[g].physicalDeviceCount;

	memcpy(
		context->devices,
		groups[g].physicalDevices,
		sizeof(VkPhysicalDevice) * groups[g].physicalDeviceCount);

	return 1;
}

/****************************
 * Finds the optimal (least flags) queue family from count properties
 * that includes the required flags and presentation support.
 * @param props Cannot be NULL if count > 0.
 * @return Index into props, UINT32_MAX if none found.
 */
static uint32_t _gfx_find_queue_family(_GFXDevice* device, uint32_t count,
                                       const VkQueueFamilyProperties* props,
                                       VkQueueFlags flags, bool present)
{
	assert(device != NULL);
	assert(device->vk.device != NULL); // Only field to access!
	assert(count == 0 || props != NULL);
	assert(flags != 0 || present);

	// Since we don't know anything about the order of the queues,
	// we loop over all the things and keep track of the best fit.
	uint32_t found = UINT32_MAX;
	VkQueueFlags foundFlags;

	for (uint32_t i = 0; i < count; ++i)
	{
		VkQueueFlags iFlags =
			_GFX_QUEUE_FLAGS_ALL(props[i].queueFlags);

		// If it does not include all required flags OR
		// it needs presentation support but it doesn't have it,
		// skip it.
		// Note we do not check for presentation to a specific surface yet.
		// (make sure to short circuit the presentation call tho..)
		if (
			(iFlags & flags) != flags ||
			(present && !glfwGetPhysicalDevicePresentationSupport(
				_groufix.vk.instance, device->vk.device, i)))
		{
			continue;
		}

		// Evaluate if it's better, i.e. check which has less flags.
		if (
			found == UINT32_MAX ||
			_GFX_QUEUE_FLAGS_COUNT(iFlags) < _GFX_QUEUE_FLAGS_COUNT(foundFlags))
		{
			found = i;
			foundFlags = iFlags;
		}
	}

	return found;
}

/****************************
 * Finds all required queue families to use.
 * @param props Cannot be NULL if count > 0.
 * @param families Output array storing { graphics, present, compute, transfer }.
 *
 * families is overwritten with the Vulkan family indices to use.
 * Members may have the same output, and any may be UINT32_MAX if not found.
 */
static void _gfx_find_queue_families(_GFXDevice* device, uint32_t count,
                                     const VkQueueFamilyProperties* props,
                                     uint32_t* families)
{
	assert(device != NULL);
	assert(device->vk.device != NULL); // Only field to access!
	assert(count == 0 || props != NULL);
	assert(families != NULL);

	// We need/want a few different queues (families) for different operations.
	// 1) A general graphics family:
	//  We use the most optimal family with VK_QUEUE_GRAPHICS_BIT set.
	//  If we can, it may have VK_QUEUE_COMPUTE_BIT set.
	// 2) A family that supports presentation to surface:
	//  Preferably the graphics queue, otherwise another one.
	//  This has precedence over compute capabilities.
	// 3) A compute-only family for use when others are stalling.
	//  We use the most optimal family with VK_QUEUE_COMPUTE_BIT set.
	// 4) A transfer family:
	//  We use the most optimal family with VK_QUEUE_TRANSFER_BIT set.

	uint32_t* graphics = families + 0;
	uint32_t* present = families + 1;
	uint32_t* compute = families + 2;
	uint32_t* transfer = families + 3;

	// Start with finding a graphics family,
	// hopefully with presentation + compute.
	// And find async (hopefully better) compute & transfer families.
	*graphics = _gfx_find_queue_family(
		device, count, props, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 1);
	*compute = _gfx_find_queue_family(
		device, count, props, VK_QUEUE_COMPUTE_BIT, 0);
	*transfer = _gfx_find_queue_family(
		device, count, props, VK_QUEUE_TRANSFER_BIT, 0);

	// Fallback to a graphics family with only presentation.
	if (*graphics == UINT32_MAX)
		*graphics = _gfx_find_queue_family(
			device, count, props, VK_QUEUE_GRAPHICS_BIT, 1);

	if (*graphics != UINT32_MAX)
		*present = *graphics;
	else
	{
		// If no graphics family with presentation, find separate families.
		// Again, hopefully there's a graphics family with only compute.
		*graphics = _gfx_find_queue_family(
			device, count, props, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 0);
		*present = _gfx_find_queue_family(
			device, count, props, 0, 1);

		// Fallback to a graphics-only family.
		if (*graphics == UINT32_MAX)
			*graphics = _gfx_find_queue_family(
				device, count, props, VK_QUEUE_GRAPHICS_BIT, 0);
	}
}

/****************************
 * Allocates a new queue set.
 * @param context    Appends created set to its sets member.
 * @param count      Number of queues/mutexes to create, must be > 0.
 * @param createInfo Queue create info output, cannot be NULL.
 * @param flags      Flags this queue is specifically chosen for.
 * @return Non-zero on success.
 */
static bool _gfx_alloc_queue_set(_GFXContext* context,
                                 VkDeviceQueueCreateInfo* createInfo,
                                 uint32_t family, size_t count,
                                 bool present,
                                 VkQueueFlags allFlags, VkQueueFlags flags)
{
	assert(context != NULL);
	assert(createInfo != NULL);
	assert(count > 0);
	assert(count <= sizeof(_gfx_vk_queue_priorities)/sizeof(_gfx_vk_queue_priorities[0]));

	// Allocate a new queue set.
	_GFXQueueSet* set = malloc(sizeof(_GFXQueueSet) + sizeof(_GFXMutex) * count);
	if (set == NULL) return 0;

	set->flags    = flags;
	set->allFlags = allFlags;
	set->present  = present;
	set->family   = family;

	// Keep inserting a mutex for each queue and stop as soon as we fail.
	for (set->count = 0; set->count < count; ++set->count)
		if (!_gfx_mutex_init(&set->locks[set->count]))
		{
			while (set->count > 0) _gfx_mutex_clear(&set->locks[--set->count]);
			free(set);

			return 0;
		}

	// Insert into set list of the context.
	gfx_list_insert_after(&context->sets, &set->list, NULL);

	// Fill create info.
	*createInfo = (VkDeviceQueueCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,

		.pNext            = NULL,
		.flags            = 0,
		.queueFamilyIndex = family,
		.queueCount       = (uint32_t)count,
		.pQueuePriorities = _gfx_vk_queue_priorities
	};

	return 1;
}

/****************************
 * Creates an array of VkDeviceQueueCreateInfo structures and fills the
 * _GFXQueueSet list of context, on failure, no list elements are freed!
 * @param createInfos Output create infos, must call free() on success.
 * @param families    Output array storing { graphics, present, compute, transfer }.
 * @return Number of created queue sets.
 *
 * See _gfx_find_queue_families for the families parameter,
 * except this function guarantees none are UINT32_MAX if successful.
 */
static uint32_t _gfx_create_queue_sets(_GFXContext* context, _GFXDevice* device,
                                       VkDeviceQueueCreateInfo** createInfos,
                                       uint32_t* families)
{
	assert(context != NULL);
	assert(device != NULL);
	assert(createInfos != NULL);
	assert(*createInfos == NULL);
	assert(families != NULL);

	// So get all queue families.
	uint32_t fCount;
	_groufix.vk.GetPhysicalDeviceQueueFamilyProperties(
		device->vk.device, &fCount, NULL);

	VkQueueFamilyProperties props[fCount];
	_groufix.vk.GetPhysicalDeviceQueueFamilyProperties(
		device->vk.device, &fCount, props);

	// Find all queue families as follows:
	//  { graphics, present, compute, transfer }.
	_gfx_find_queue_families(device, fCount, props, families);

	const uint32_t* graphics = families + 0;
	const uint32_t* present = families + 1;
	const uint32_t* compute = families + 2;
	const uint32_t* transfer = families + 3;

	// Now check if we found a family for all queues (and log for all).
	if (*graphics == UINT32_MAX) gfx_log_error(
		"[ %s ] lacks a queue family with VK_QUEUE_GRAPHICS_BIT set.",
		device->name);

	if (*present == UINT32_MAX) gfx_log_error(
		"[ %s ] lacks a queue family with presentation support.",
		device->name);

	if (*compute == UINT32_MAX) gfx_log_error(
		"[ %s ] lacks a queue family with VK_QUEUE_COMPUTE_BIT set.",
		device->name);

	if (*transfer == UINT32_MAX) gfx_log_error(
		"[ %s ] lacks a queue family with VK_QUEUE_TRANSFER_BIT set.",
		device->name);

	if (
		*graphics == UINT32_MAX || *present == UINT32_MAX ||
		*compute == UINT32_MAX || *transfer == UINT32_MAX)
	{
		return 0;
	}

	// Ok so we found all queues, we should now allocate the queue sets and
	// info structures for Vulkan.
	// Allocate the maximum number of infostructures.
	*createInfos = malloc(sizeof(VkDeviceQueueCreateInfo) * 4); // 4!
	if (*createInfos == NULL)
		return 0;

	// Allocate queue sets and count how many.
	uint32_t sets = 0;
	bool success = 1;

	const size_t presentIsGraphics = *present == *graphics ? 1 : 0;
	const size_t computeIsGraphics = *compute == *graphics ? 1 : 0;
	const size_t transferIsGraphics = *transfer == *graphics ? 1 : 0;
	const size_t computeIsPresent = *compute == *present ? 1 : 0;
	const size_t transferIsPresent = *transfer == *present ? 1 : 0;
	const size_t transferIsCompute = *transfer == *compute ? 1 : 0;

	// Allocate main (graphics) queue set.
	{
		size_t count = GFX_MIN(props[*graphics].queueCount,
			computeIsGraphics + transferIsGraphics + 1);

		success = success && _gfx_alloc_queue_set(context,
			(*createInfos) + (sets++), *graphics, count, presentIsGraphics,
			_GFX_QUEUE_FLAGS_ALL(props[*graphics].queueFlags),
			VK_QUEUE_GRAPHICS_BIT |
				(computeIsGraphics ? VK_QUEUE_COMPUTE_BIT : (VkQueueFlags)0) |
				(transferIsGraphics ? VK_QUEUE_TRANSFER_BIT : (VkQueueFlags)0));
	}

	// Allocate novel present queue set if necessary.
	if (!presentIsGraphics)
	{
		size_t count = GFX_MIN(props[*present].queueCount,
			computeIsPresent + transferIsPresent + 1);

		success = success && _gfx_alloc_queue_set(context,
			(*createInfos) + (sets++), *present, count, 1,
			_GFX_QUEUE_FLAGS_ALL(props[*present].queueFlags),
				(computeIsPresent ? VK_QUEUE_COMPUTE_BIT : (VkQueueFlags)0) |
				(transferIsPresent ? VK_QUEUE_TRANSFER_BIT : (VkQueueFlags)0));
	}

	// Allocate a novel compute queue set if necessary.
	if (!computeIsGraphics && !computeIsPresent)
	{
		size_t count = GFX_MIN(props[*compute].queueCount,
			transferIsCompute + 1);

		success = success && _gfx_alloc_queue_set(context,
			(*createInfos) + (sets++), *compute, count, 0,
			_GFX_QUEUE_FLAGS_ALL(props[*compute].queueFlags),
			VK_QUEUE_COMPUTE_BIT |
				(transferIsCompute ? VK_QUEUE_TRANSFER_BIT : (VkQueueFlags)0));
	}

	// Allocate a novel transfer queue set if necessary.
	if (!transferIsGraphics && !transferIsPresent && !transferIsCompute)
	{
		success = success && _gfx_alloc_queue_set(context,
			(*createInfos) + (sets++), *transfer, 1, 0,
			_GFX_QUEUE_FLAGS_ALL(props[*transfer].queueFlags),
			VK_QUEUE_TRANSFER_BIT);
	}

	// Check if we succeeded..
	if (!success)
	{
		free(*createInfos);
		*createInfos = NULL;
		return 0;
	}

	return sets;
}

/****************************
 * Destroys a context and all its resources.
 * @param context Cannot be NULL.
 */
static void _gfx_destroy_context(_GFXContext* context)
{
	assert(context != NULL);

	// Erase itself from the context list.
	gfx_list_erase(&_groufix.contexts, &context->list);

	// Loop over all its queue sets and free their resources.
	while (context->sets.head != NULL)
	{
		_GFXQueueSet* set = (_GFXQueueSet*)context->sets.head;
		gfx_list_erase(&context->sets, context->sets.head);

		for (size_t q = 0; q < set->count; ++q)
			_gfx_mutex_clear(&set->locks[q]);

		free(set);
	}

	// We wait for all queues of the device to complete, then we can destroy.
	// We check if the functions were loaded properly,
	// they may not be if something failed during context creation.
	if (context->vk.DeviceWaitIdle != NULL)
		context->vk.DeviceWaitIdle(context->vk.device);
	if (context->vk.DestroyDevice != NULL)
		context->vk.DestroyDevice(context->vk.device, NULL);

	_gfx_mutex_clear(&context->limits.samplerLock);
	_gfx_mutex_clear(&context->limits.allocLock);
	gfx_list_clear(&context->sets);

	free(context);
}

/****************************
 * Creates an appropriate context (Vulkan device + fp's) suited for a device.
 * device->context must be NULL, no prior context can be assigned.
 * @param device Cannot be NULL.
 *
 * Not thread-safe for the same device, it modifies.
 * device->context will remain NULL on failure, on success it will be set to
 * the newly created context (context->index will be set also).
 */
static void _gfx_create_context(_GFXDevice* device)
{
	assert(_groufix.vk.instance != NULL);
	assert(device != NULL);
	assert(device->context == NULL);

	VkDeviceQueueCreateInfo* createInfos = NULL; // Will be explicitly freed.

	// First of all, check availability & Vulkan version.
	if (!device->base.available)
	{
		gfx_log_error("[ %s ] is not available.", device->name);

		goto error;
	}

	if (device->api < _GFX_VK_API_VERSION)
	{
		gfx_log_error("[ %s ] does not support Vulkan version %u.%u.%u.",
			(unsigned int)VK_API_VERSION_MAJOR(_GFX_VK_API_VERSION),
			(unsigned int)VK_API_VERSION_MINOR(_GFX_VK_API_VERSION),
			(unsigned int)VK_API_VERSION_PATCH(_GFX_VK_API_VERSION),
			device->name);

		goto error;
	}

	// Allocate a new context, we are allocating an array of physical devices
	// at the end, just allocate the maximum number, who cares..
	// These are used to check if a future device can use this context.
	_GFXContext* context = malloc(
		sizeof(_GFXContext) +
		sizeof(VkPhysicalDevice) * VK_MAX_DEVICE_GROUP_SIZE);

	if (context == NULL)
		goto error;

	// Get supported feature flags.
	context->features =
		(device->base.features.geometryShader ?
			_GFX_SUPPORT_GEOMETRY_SHADER : 0) |
		(device->base.features.tessellationShader ?
			_GFX_SUPPORT_TESSELLATION_SHADER : 0);

	{
		// Get allocation limits in a scope so pdp gets freed :)
		VkPhysicalDeviceProperties pdp;
		_groufix.vk.GetPhysicalDeviceProperties(device->vk.device, &pdp);

		// Memory allocation limit.
		context->limits.maxAllocs = pdp.limits.maxMemoryAllocationCount;
		atomic_store(&context->limits.allocs, 0);

		if (!_gfx_mutex_init(&context->limits.allocLock))
		{
			free(context);
			goto error;
		}

		// Sampler allocation limit.
		context->limits.maxSamplers = pdp.limits.maxSamplerAllocationCount;
		atomic_store(&context->limits.samplers, 0);

		if (!_gfx_mutex_init(&context->limits.samplerLock))
		{
			_gfx_mutex_clear(&context->limits.allocLock);
			free(context);
			goto error;
		}

		// Shader allocations.
		atomic_store(&context->limits.shaders, 0);
	}

	// Insert itself in the context list.
	gfx_list_insert_after(&_groufix.contexts, &context->list, NULL);
	gfx_list_init(&context->sets);

	// From this point on we call _gfx_destroy_context on cleanup.
	// Set these to NULL so we don't accidentally call garbage on cleanup.
	context->vk.DestroyDevice = NULL;
	context->vk.DeviceWaitIdle = NULL;

	// Now find the device group which this device is part of.
	// This fills numDevices and devices of context!
	size_t index;
	if (!_gfx_get_device_group(context, device, &index))
		goto clean;

	// Call the thing that allocates the desired queues (i.e. fills sets of context!)
	// and gets us the creation info to pass to Vulkan.
	// When a a future device also uses this context, it is assumed it has
	// equivalent queue family properties.
	// If there are any device groups such that this is the case, you
	// probably have equivalent GPUs in an SLI/CrossFire setup anyway...
	uint32_t sets;
	uint32_t families[4];
	if (!(sets = _gfx_create_queue_sets(context, device, &createInfos, families)))
		goto clean;

	// Get desired device feature structs for the next chain.
	// Similarly to the families, we assume that any device that uses the same
	// context has equivalent features.
	bool vk11, vk12, vk13, vk14;
	VkPhysicalDeviceFeatures pdf;
	VkPhysicalDeviceVulkan11Features pdv11f;
	VkPhysicalDeviceVulkan12Features pdv12f;
	VkPhysicalDeviceVulkan13Features pdv13f;
	VkPhysicalDeviceVulkan14Features pdv14f;

	_GFX_GET_DEVICE_FEATURES(device,
		vk11, vk12, vk13, vk14,
		pdf, pdv11f, pdv12f, pdv13f, pdv14f);

	// Enable VK_KHR_swapchain so we can interact with surfaces from GLFW.
	const char* extensions[] = {
		"VK_KHR_swapchain",

		// If a portability subset device, add VK_KHR_portability_subset.
#if defined (GFX_USE_VK_SUBSET_DEVICES)
		"VK_KHR_portability_subset"
#endif
	};

	const uint32_t extensionCount =
#if defined (GFX_USE_VK_SUBSET_DEVICES)
		sizeof(extensions)/sizeof(char*) - (device->subset ? 0 : 1);
#else
		sizeof(extensions)/sizeof(char*);
#endif

	// Enable VK_LAYER_KHRONOS_validation if debug,
	// this is deprecated by now, but for older Vulkan versions.
#if !defined (NDEBUG)
	const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
#endif

	// Finally go create the logical Vulkan device.
	VkDeviceGroupDeviceCreateInfo dgdci = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO,

		.pNext               = (vk11 ? &pdv11f : NULL),
		.physicalDeviceCount = (uint32_t)context->numDevices,
		.pPhysicalDevices    = context->devices
	};

	VkDeviceCreateInfo dci = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,

		.pNext                   = &dgdci,
		.flags                   = 0,
		.queueCreateInfoCount    = sets,
		.pQueueCreateInfos       = createInfos,
#if defined (NDEBUG)
		.enabledLayerCount       = 0,
		.ppEnabledLayerNames     = NULL,
#else
		.enabledLayerCount       = sizeof(layers)/sizeof(char*),
		.ppEnabledLayerNames     = layers,
#endif
		.enabledExtensionCount   = extensionCount,
		.ppEnabledExtensionNames = extensions,
		.pEnabledFeatures        = &pdf
	};

	_GFX_VK_CHECK(_groufix.vk.CreateDevice(
		device->vk.device, &dci, NULL, &context->vk.device), goto clean);

#if !defined (NDEBUG)
	// This is like a moment to celebrate, right?
	GFXBufWriter* logger = gfx_logger_debug();
	if (logger != NULL)
	{
		// We count the number of actual queues here.
		size_t queueCount = 0;

		// And get the queue indices of each chosen family.
		// Don't forget:
		//  { graphics, present, compute, transfer }.
		uint32_t indices[4];

		for (GFXListNode* s = context->sets.head; s != NULL; s = s->next)
		{
			_GFXQueueSet* set = (_GFXQueueSet*)s;
			queueCount += set->count;

			// Bit ugly, but we don't want extra computation when not debug!
			if (set->family == families[0])
				indices[0] = _gfx_queue_index(set, VK_QUEUE_GRAPHICS_BIT, 0);
			if (set->family == families[1])
				indices[1] = _gfx_queue_index(set, 0, 1);
			if (set->family == families[2])
				indices[2] = _gfx_queue_index(set, VK_QUEUE_COMPUTE_BIT, 0);
			if (set->family == families[3])
				indices[3] = _gfx_queue_index(set, VK_QUEUE_TRANSFER_BIT, 0);
		}

		gfx_io_writef(logger,
			"Logical Vulkan device created:\n"
			"    API version: v%u.%u.%u\n"
			"    Contains at least: [ %s ].\n"
			"    #physical devices: %"GFX_PRIs".\n"
			"    #queue sets: %"PRIu32".\n"
			"    #queues (total): %"GFX_PRIs".\n"
			"    Picked queues (family,index):\n"
			"        graphics: (%"PRIu32",%"PRIu32")\n"
			"        presentation: (%"PRIu32",%"PRIu32")\n"
			"        compute: (%"PRIu32",%"PRIu32")\n"
			"        transfer: (%"PRIu32",%"PRIu32")\n"
			"    Enabled extensions: %s\n",
			(unsigned int)VK_API_VERSION_MAJOR(device->api),
			(unsigned int)VK_API_VERSION_MINOR(device->api),
			(unsigned int)VK_API_VERSION_PATCH(device->api),
			device->name, context->numDevices, sets, queueCount,
			families[0], indices[0],
			families[1], indices[1],
			families[2], indices[2],
			families[3], indices[3],
			extensionCount > 0 ? "" : "None.");

		for (uint32_t e = 0; e < extensionCount; ++e)
			gfx_io_writef(logger, "        %s\n", extensions[e]);

		gfx_logger_end(logger);
	}
#endif


	// Now load all device level Vulkan functions.
	// Load vkDestroyDevice and vkDeviceWaitIdle first so we can clean properly.
	_GFX_GET_DEVICE_PROC_ADDR(DestroyDevice);
	_GFX_GET_DEVICE_PROC_ADDR(DeviceWaitIdle);

	_GFX_GET_DEVICE_PROC_ADDR(AcquireNextImageKHR);
	_GFX_GET_DEVICE_PROC_ADDR(AllocateCommandBuffers);
	_GFX_GET_DEVICE_PROC_ADDR(AllocateDescriptorSets);
	_GFX_GET_DEVICE_PROC_ADDR(AllocateMemory);
	_GFX_GET_DEVICE_PROC_ADDR(BindBufferMemory);
	_GFX_GET_DEVICE_PROC_ADDR(BindImageMemory);
	_GFX_GET_DEVICE_PROC_ADDR(BeginCommandBuffer);
	_GFX_GET_DEVICE_PROC_ADDR(CmdBeginRenderPass);
	_GFX_GET_DEVICE_PROC_ADDR(CmdBindDescriptorSets);
	_GFX_GET_DEVICE_PROC_ADDR(CmdBindIndexBuffer);
	_GFX_GET_DEVICE_PROC_ADDR(CmdBindPipeline);
	_GFX_GET_DEVICE_PROC_ADDR(CmdBindVertexBuffers);
	_GFX_GET_DEVICE_PROC_ADDR(CmdBlitImage);
	_GFX_GET_DEVICE_PROC_ADDR(CmdCopyBuffer);
	_GFX_GET_DEVICE_PROC_ADDR(CmdCopyImage);
	_GFX_GET_DEVICE_PROC_ADDR(CmdCopyBufferToImage);
	_GFX_GET_DEVICE_PROC_ADDR(CmdCopyImageToBuffer);
	_GFX_GET_DEVICE_PROC_ADDR(CmdDispatch);
	_GFX_GET_DEVICE_PROC_ADDR(CmdDispatchBase);
	_GFX_GET_DEVICE_PROC_ADDR(CmdDispatchIndirect);
	_GFX_GET_DEVICE_PROC_ADDR(CmdDraw);
	_GFX_GET_DEVICE_PROC_ADDR(CmdDrawIndexed);
	_GFX_GET_DEVICE_PROC_ADDR(CmdDrawIndexedIndirect);
	_GFX_GET_DEVICE_PROC_ADDR(CmdDrawIndirect);
	_GFX_GET_DEVICE_PROC_ADDR(CmdEndRenderPass);
	_GFX_GET_DEVICE_PROC_ADDR(CmdExecuteCommands);
	_GFX_GET_DEVICE_PROC_ADDR(CmdNextSubpass);
	_GFX_GET_DEVICE_PROC_ADDR(CmdPipelineBarrier);
	_GFX_GET_DEVICE_PROC_ADDR(CmdPushConstants);
	_GFX_GET_DEVICE_PROC_ADDR(CmdResolveImage);
	_GFX_GET_DEVICE_PROC_ADDR(CmdSetLineWidth);
	_GFX_GET_DEVICE_PROC_ADDR(CmdSetScissor);
	_GFX_GET_DEVICE_PROC_ADDR(CmdSetViewport);
	_GFX_GET_DEVICE_PROC_ADDR(CreateBuffer);
	_GFX_GET_DEVICE_PROC_ADDR(CreateBufferView);
	_GFX_GET_DEVICE_PROC_ADDR(CreateCommandPool);
	_GFX_GET_DEVICE_PROC_ADDR(CreateComputePipelines);
	_GFX_GET_DEVICE_PROC_ADDR(CreateDescriptorPool);
	_GFX_GET_DEVICE_PROC_ADDR(CreateDescriptorSetLayout);
	_GFX_GET_DEVICE_PROC_ADDR(CreateDescriptorUpdateTemplate);
	_GFX_GET_DEVICE_PROC_ADDR(CreateFence);
	_GFX_GET_DEVICE_PROC_ADDR(CreateFramebuffer);
	_GFX_GET_DEVICE_PROC_ADDR(CreateGraphicsPipelines);
	_GFX_GET_DEVICE_PROC_ADDR(CreateImage);
	_GFX_GET_DEVICE_PROC_ADDR(CreateImageView);
	_GFX_GET_DEVICE_PROC_ADDR(CreatePipelineCache);
	_GFX_GET_DEVICE_PROC_ADDR(CreatePipelineLayout);
	_GFX_GET_DEVICE_PROC_ADDR(CreateRenderPass);
	_GFX_GET_DEVICE_PROC_ADDR(CreateSampler);
	_GFX_GET_DEVICE_PROC_ADDR(CreateSemaphore);
	_GFX_GET_DEVICE_PROC_ADDR(CreateShaderModule);
	_GFX_GET_DEVICE_PROC_ADDR(CreateSwapchainKHR);
	_GFX_GET_DEVICE_PROC_ADDR(DestroyBuffer);
	_GFX_GET_DEVICE_PROC_ADDR(DestroyBufferView);
	_GFX_GET_DEVICE_PROC_ADDR(DestroyCommandPool);
	_GFX_GET_DEVICE_PROC_ADDR(DestroyDescriptorPool);
	_GFX_GET_DEVICE_PROC_ADDR(DestroyDescriptorSetLayout);
	_GFX_GET_DEVICE_PROC_ADDR(DestroyDescriptorUpdateTemplate);
	_GFX_GET_DEVICE_PROC_ADDR(DestroyFence);
	_GFX_GET_DEVICE_PROC_ADDR(DestroyFramebuffer);
	_GFX_GET_DEVICE_PROC_ADDR(DestroyImage);
	_GFX_GET_DEVICE_PROC_ADDR(DestroyImageView);
	_GFX_GET_DEVICE_PROC_ADDR(DestroyPipeline);
	_GFX_GET_DEVICE_PROC_ADDR(DestroyPipelineCache);
	_GFX_GET_DEVICE_PROC_ADDR(DestroyPipelineLayout);
	_GFX_GET_DEVICE_PROC_ADDR(DestroyRenderPass);
	_GFX_GET_DEVICE_PROC_ADDR(DestroySampler);
	_GFX_GET_DEVICE_PROC_ADDR(DestroySemaphore);
	_GFX_GET_DEVICE_PROC_ADDR(DestroyShaderModule);
	_GFX_GET_DEVICE_PROC_ADDR(DestroySwapchainKHR);
	_GFX_GET_DEVICE_PROC_ADDR(EndCommandBuffer);
	_GFX_GET_DEVICE_PROC_ADDR(FreeCommandBuffers);
	_GFX_GET_DEVICE_PROC_ADDR(FreeMemory);
	_GFX_GET_DEVICE_PROC_ADDR(GetBufferMemoryRequirements);
	_GFX_GET_DEVICE_PROC_ADDR(GetBufferMemoryRequirements2);
	_GFX_GET_DEVICE_PROC_ADDR(GetDeviceQueue);
	_GFX_GET_DEVICE_PROC_ADDR(GetFenceStatus);
	_GFX_GET_DEVICE_PROC_ADDR(GetImageMemoryRequirements);
	_GFX_GET_DEVICE_PROC_ADDR(GetImageMemoryRequirements2);
	_GFX_GET_DEVICE_PROC_ADDR(GetPipelineCacheData);
	_GFX_GET_DEVICE_PROC_ADDR(GetSwapchainImagesKHR);
	_GFX_GET_DEVICE_PROC_ADDR(MapMemory);
	_GFX_GET_DEVICE_PROC_ADDR(MergePipelineCaches);
	_GFX_GET_DEVICE_PROC_ADDR(QueuePresentKHR);
	_GFX_GET_DEVICE_PROC_ADDR(QueueSubmit);
	_GFX_GET_DEVICE_PROC_ADDR(ResetCommandPool);
	_GFX_GET_DEVICE_PROC_ADDR(ResetDescriptorPool);
	_GFX_GET_DEVICE_PROC_ADDR(ResetFences);
	_GFX_GET_DEVICE_PROC_ADDR(UnmapMemory);
	_GFX_GET_DEVICE_PROC_ADDR(UpdateDescriptorSetWithTemplate);
	_GFX_GET_DEVICE_PROC_ADDR(WaitForFences);


	// Set device's reference to this context.
	device->context = context;

	free(createInfos);

	return;


	// Cleanup on failure.
clean:
	_gfx_destroy_context(context);
	free(createInfos);
error:
	gfx_log_error(
		"Could not create or initialize a logical Vulkan device for physical "
		"device group containing at least: [ %s ].",
		device->name);
}

/****************************
 * Initializes a single _GFXDevice struct given a Vulkan device.
 * @param dev Cannot be NULL.
 *
 * Only the following fields are left unset:
 *  dev->{ base.{ name, driverName, driverInfo }, lock, formats }
 */
static void _gfx_device_init(_GFXDevice* dev, VkPhysicalDevice device)
{
	assert(_groufix.vk.instance != NULL);
	assert(device != NULL);

	// Get some Vulkan properties.
	VkPhysicalDeviceProperties2 pdp2 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		.pNext = NULL
	};

	VkPhysicalDeviceProperties* pdp = &pdp2.properties;
	_groufix.vk.GetPhysicalDeviceProperties(device, pdp);

	// Initial setup.
	dev->api       = pdp->apiVersion;
	dev->context   = NULL;
	dev->vk.device = device;

	memcpy(
		dev->name, pdp->deviceName,
		VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);

	dev->driverName[0] = '\0';
	dev->driverInfo[0] = '\0';

#if defined (GFX_USE_VK_SUBSET_DEVICES)
	// If we're including portability subset devices, we need to check if
	// the device exposes VK_KHR_portability_subset.
	// If it does, we need to enable the extension in the device.
	dev->subset = _gfx_device_is_subset(device);
#endif

	// Get all Vulkan device features as well.
	bool vk11, vk12, vk13, vk14;
	VkPhysicalDeviceFeatures pdf;
	VkPhysicalDeviceVulkan11Features pdv11f;
	VkPhysicalDeviceVulkan12Features pdv12f;
	VkPhysicalDeviceVulkan13Features pdv13f;
	VkPhysicalDeviceVulkan14Features pdv14f;

	_GFX_GET_DEVICE_FEATURES(dev,
		vk11, vk12, vk13, vk14,
		pdf, pdv11f, pdv12f, pdv13f, pdv14f);

	// And get us later properties, now that we have the version...
	VkPhysicalDeviceVulkan14Properties pdv14p = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES,
		.pNext = NULL
	};

	VkPhysicalDeviceVulkan13Properties pdv13p = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES,
		.pNext = (vk14 ? (void*)&pdv14p : NULL)
	};

	VkPhysicalDeviceVulkan12Properties pdv12p = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,
		.pNext = (vk13 ? (void*)&pdv13p : NULL)
	};

	VkPhysicalDeviceVulkan11Properties pdv11p = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES,
		.pNext = (vk12 ? (void*)&pdv12p : NULL)
	};

	VkPhysicalDeviceDriverProperties pddp = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
		.pNext = (void*)&pdv11p
	};

	pdp2.pNext = (vk12 ? (void*)&pddp : vk11 ? (void*)&pdv11p : NULL);
	_groufix.vk.GetPhysicalDeviceProperties2(device, &pdp2);

	// Extra setup.
	if (vk12)
	{
		memcpy(
			dev->driverName, pddp.driverName,
			VK_MAX_DRIVER_NAME_SIZE);
		memcpy(
			dev->driverInfo, pddp.driverInfo,
			VK_MAX_DRIVER_INFO_SIZE);
	}

	// Sadly we need to get all queue family properties so we can
	// determine some of the features.
	// Plus availability is based on the presence of all families.
	uint32_t fCount;
	_groufix.vk.GetPhysicalDeviceQueueFamilyProperties(device, &fCount, NULL);

	VkQueueFamilyProperties props[fCount];
	_groufix.vk.GetPhysicalDeviceQueueFamilyProperties(device, &fCount, props);

	// Find all queue families as follows:
	//  { graphics, present, compute, transfer }.
	uint32_t families[4];
	_gfx_find_queue_families(dev, fCount, props, families);

	const bool available =
		dev->api >= _GFX_VK_API_VERSION &&
		families[0] != UINT32_MAX &&
		families[1] != UINT32_MAX &&
		families[2] != UINT32_MAX &&
		families[3] != UINT32_MAX;

	// Then define the features and limits part of the new device :)
	dev->base = (GFXDevice){
		.type = _GFX_GET_DEVICE_TYPE(pdp->deviceType),
		.name = NULL,
		.driverName = NULL,
		.driverInfo = NULL,
		.available = available,

		.features = {
			.indexUint8               = (vk14 ? pdv14f.indexTypeUint8 : 0),
			.indexUint32              = pdf.fullDrawIndexUint32,
			.indirectMultiDraw        = pdf.multiDrawIndirect,
			.indirectFirstInstance    = pdf.drawIndirectFirstInstance,
			.cubeArray                = pdf.imageCubeArray,
			.multisampledStorageImage = pdf.shaderStorageImageMultisample,
			.geometryShader           = pdf.geometryShader,
			.tessellationShader       = pdf.tessellationShader,
			.rasterNonSolid           = pdf.fillModeNonSolid,
			.wideLines                = pdf.wideLines,
			.largePoints              = pdf.largePoints,
			.independentBlend         = pdf.independentBlend,
			.logicOp                  = pdf.logicOp,
			.depthBounds              = pdf.depthBounds,
			.compressionBC            = pdf.textureCompressionBC,
			.compressionETC2          = pdf.textureCompressionETC2,
			.compressionASTC          = pdf.textureCompressionASTC_LDR,
			.samplerAnisotropy        = pdf.samplerAnisotropy,
			.samplerClampToEdgeMirror = (vk12 ? pdv12f.samplerMirrorClampToEdge : 0),
			.samplerMinmax            = (vk12 ? pdv12f.samplerFilterMinmax : 0),

			.shaderClipDistance      = pdf.shaderClipDistance,
			.shaderCullDistance      = pdf.shaderCullDistance,
			.shaderInt8              = (vk12 ? pdv12f.shaderInt8 : 0),
			.shaderInt16             = pdf.shaderInt16,
			.shaderInt64             = pdf.shaderInt64,
			.shaderFloat16           = (vk12 ? pdv12f.shaderFloat16 : 0),
			.shaderFloat64           = pdf.shaderFloat64,
			.shaderPushConstant8     = (vk12 ? pdv12f.storagePushConstant8 : 0),
			.shaderPushConstant16    = (vk11 ? pdv11f.storagePushConstant16 : 0),
			.shaderInputOutput16     = (vk11 ? pdv11f.storageInputOutput16 : 0),
			.shaderIntegerDotProduct = (vk13 ? pdv13f.shaderIntegerDotProduct : 0),

			.dynamicIndexUniformBuffer      = pdf.shaderUniformBufferArrayDynamicIndexing,
			.dynamicIndexStorageBuffer      = pdf.shaderStorageBufferArrayDynamicIndexing,
			.dynamicIndexUniformTexelBuffer = (vk12 ? pdv12f.shaderUniformTexelBufferArrayDynamicIndexing : 0),
			.dynamicIndexStorageTexelBuffer = (vk12 ? pdv12f.shaderStorageTexelBufferArrayDynamicIndexing : 0),
			.dynamicIndexSampledImage       = pdf.shaderSampledImageArrayDynamicIndexing,
			.dynamicIndexStorageImage       = pdf.shaderStorageImageArrayDynamicIndexing,
			.dynamicIndexAttachmentInput    = (vk12 ? pdv12f.shaderInputAttachmentArrayDynamicIndexing : 0),

			.nonUniformIndexUniformBuffer      = (vk12 ? pdv12f.shaderUniformBufferArrayNonUniformIndexing : 0),
			.nonUniformIndexStorageBuffer      = (vk12 ? pdv12f.shaderStorageBufferArrayNonUniformIndexing : 0),
			.nonUniformIndexUniformTexelBuffer = (vk12 ? pdv12f.shaderUniformTexelBufferArrayNonUniformIndexing : 0),
			.nonUniformIndexStorageTexelBuffer = (vk12 ? pdv12f.shaderStorageTexelBufferArrayNonUniformIndexing : 0),
			.nonUniformIndexSampledImage       = (vk12 ? pdv12f.shaderSampledImageArrayNonUniformIndexing : 0),
			.nonUniformIndexStorageImage       = (vk12 ? pdv12f.shaderStorageImageArrayNonUniformIndexing : 0),
			.nonUniformIndexAttachmentInput    = (vk12 ? pdv12f.shaderInputAttachmentArrayNonUniformIndexing : 0),

			.inlineCompute = available && (props[families[0]].queueFlags & VK_QUEUE_COMPUTE_BIT)
		},

		.limits = {
			.maxIndexValue         = pdp->limits.maxDrawIndexedIndexValue,
			.maxImageSize1D        = pdp->limits.maxImageDimension1D,
			.maxImageSize2D        = pdp->limits.maxImageDimension2D,
			.maxImageSize3D        = pdp->limits.maxImageDimension3D,
			.maxImageSizeCube      = pdp->limits.maxImageDimensionCube,
			.maxImageLayers        = pdp->limits.maxImageArrayLayers,
			.maxBufferTexels       = pdp->limits.maxTexelBufferElements,
			.maxUniformBufferRange = pdp->limits.maxUniformBufferRange,
			.maxStorageBufferRange = pdp->limits.maxStorageBufferRange,
			.maxPushConstantSize   = pdp->limits.maxPushConstantsSize,
			.maxBoundSets          = pdp->limits.maxBoundDescriptorSets,
			.maxComputeMemorySize  = pdp->limits.maxComputeSharedMemorySize,
			.maxAttributes         = pdp->limits.maxVertexInputAttributes,
			.maxAttributeOffset    = pdp->limits.maxVertexInputAttributeOffset,
			.maxAttributeStride    = pdp->limits.maxVertexInputBindingStride,
			.maxPrimitiveBuffers   = pdp->limits.maxVertexInputBindings,
			.maxAttachmentWidth    = pdp->limits.maxFramebufferWidth,
			.maxAttachmentHeight   = pdp->limits.maxFramebufferHeight,
			.maxAttachmentLayers   = pdp->limits.maxFramebufferLayers,
			.maxAttachmentOutputs  = pdp->limits.maxColorAttachments,

			.maxStageUniformBuffers   = pdp->limits.maxPerStageDescriptorUniformBuffers,
			.maxStageStorageBuffers   = pdp->limits.maxPerStageDescriptorStorageBuffers,
			.maxStageSampledImages    = pdp->limits.maxPerStageDescriptorSampledImages,
			.maxStageStorageImages    = pdp->limits.maxPerStageDescriptorStorageImages,
			.maxStageSamplers         = pdp->limits.maxPerStageDescriptorSamplers,
			.maxStageAttachmentInputs = pdp->limits.maxPerStageDescriptorInputAttachments,

			.maxSetUniformBuffers        = pdp->limits.maxDescriptorSetUniformBuffers,
			.maxSetStorageBuffers        = pdp->limits.maxDescriptorSetStorageBuffers,
			.maxSetUniformBuffersDynamic = pdp->limits.maxDescriptorSetUniformBuffersDynamic,
			.maxSetStorageBuffersDynamic = pdp->limits.maxDescriptorSetStorageBuffersDynamic,
			.maxSetSampledImages         = pdp->limits.maxDescriptorSetSampledImages,
			.maxSetStorageImages         = pdp->limits.maxDescriptorSetStorageImages,
			.maxSetSamplers              = pdp->limits.maxDescriptorSetSamplers,
			.maxSetAttachmentInputs      = pdp->limits.maxDescriptorSetInputAttachments,

			.maxBufferSize         = (vk13 ? pdv13p.maxBufferSize : (uint64_t)1073741824), // 2^30
			.minTexelBufferAlign   = pdp->limits.minTexelBufferOffsetAlignment,
			.minUniformBufferAlign = pdp->limits.minUniformBufferOffsetAlignment,
			.minStorageBufferAlign = pdp->limits.minStorageBufferOffsetAlignment,

			.minPointSize = pdp->limits.pointSizeRange[0],
			.maxPointSize = pdp->limits.pointSizeRange[1],
			.pointSizeGranularity = pdp->limits.pointSizeGranularity,

			.minLineWidth = pdp->limits.lineWidthRange[0],
			.maxLineWidth = pdp->limits.lineWidthRange[1],
			.lineWidthGranularity = pdp->limits.lineWidthGranularity,

			.maxMipLodBias = pdp->limits.maxSamplerLodBias,
			.maxAnisotropy = pdp->limits.maxSamplerAnisotropy,

			.computeWorkGroupCount = {
				.x = pdp->limits.maxComputeWorkGroupCount[0],
				.y = pdp->limits.maxComputeWorkGroupCount[1],
				.z = pdp->limits.maxComputeWorkGroupCount[2]
			},

			.computeWorkGroupSize = {
				.x = pdp->limits.maxComputeWorkGroupSize[0],
				.y = pdp->limits.maxComputeWorkGroupSize[1],
				.z = pdp->limits.maxComputeWorkGroupSize[2],
				.total = pdp->limits.maxComputeWorkGroupInvocations
			},

			.imageTransferGranularity = {
				.x = available ? props[families[3]].minImageTransferGranularity.width : 0,
				.y = available ? props[families[3]].minImageTransferGranularity.height : 0,
				.z = available ? props[families[3]].minImageTransferGranularity.depth : 0
			},

			.renderSampleCounts = {
				.f       = (uint8_t)pdp->limits.framebufferColorSampleCounts,
				.i       = (uint8_t)(vk12 ? pdv12p.framebufferIntegerColorSampleCounts : 0),
				.depth   = (uint8_t)pdp->limits.framebufferDepthSampleCounts,
				.stencil = (uint8_t)pdp->limits.framebufferStencilSampleCounts,
				.empty   = (uint8_t)pdp->limits.framebufferNoAttachmentsSampleCounts
			},

			.imageSampleCounts = {
				.f       = (uint8_t)pdp->limits.sampledImageColorSampleCounts,
				.i       = (uint8_t)pdp->limits.sampledImageIntegerSampleCounts,
				.depth   = (uint8_t)pdp->limits.sampledImageDepthSampleCounts,
				.stencil = (uint8_t)pdp->limits.sampledImageStencilSampleCounts,
				.storage = (uint8_t)pdp->limits.storageImageSampleCounts
			}
		}
	};
}

/****************************/
bool _gfx_devices_init(void)
{
	assert(_groufix.vk.instance != NULL);
	assert(_groufix.devices.size == 0);

	// Enumerate all devices.
	uint32_t count;
	_GFX_VK_CHECK(_groufix.vk.EnumeratePhysicalDevices(
		_groufix.vk.instance, &count, NULL), count = 0);

	VkPhysicalDevice devices[GFX_MAX(1, count)];
	if (count == 0) goto terminate;

	_GFX_VK_CHECK(_groufix.vk.EnumeratePhysicalDevices(
		_groufix.vk.instance, &count, devices), goto terminate);

	// Reserve and create groufix devices.
	// The number or order of devices never changes after initialization,
	// nor is there a user pointer for callbacks, as there are no callbacks.
	// This means we do not have to dynamically allocate the devices.
	if (!gfx_vec_reserve(&_groufix.devices, (size_t)count))
		goto terminate;

	// Once before looping, get the env var for the primary device.
	const char* envPrimDevice = getenv(GFX_ENV_PRIMARY_VK_DEVICE);

	// Because we insert in random places, we keep moving devices around,
	// so we leave their mutexes and name pointers blank until after this loop.
	for (uint32_t i = 0; i < count; ++i)
	{
		// Define a new device & init.
		_GFXDevice dev;
		_gfx_device_init(&dev, devices[i]);

		// Find position to insert at, insert them in order of primary-ness.
		// This makes it so the 'primary' device is at index 0.
		// We also check if the env variable for the primary device is set.
		const bool matchEnv = _gfx_contains_substr(dev.name, envPrimDevice);
		size_t j;

		for (j = 0; j < _groufix.devices.size; ++j)
		{
			_GFXDevice* d = gfx_vec_at(&_groufix.devices, j);
			const bool dEnv = _gfx_contains_substr(d->name, envPrimDevice);

			// Compare the device against the current in the vector
			// and check if we found the position or need to move on.

			// All matches against the primary env variable go to the left.
			// Even if not available, so we get a proper error/warning
			// instead of silently not picking the picked device!
			if (matchEnv && !dEnv) break;
			if (!matchEnv && dEnv) continue;

			// All available devices to the left.
			if (dev.base.available && !d->base.available) break;
			if (!dev.base.available && d->base.available) continue;

			// Compare against superior device type.
			if (dev.base.type < d->base.type) break;
			if (dev.base.type > d->base.type) continue;

			// Compare vulkan versions.
			if (dev.api > d->api) break;
			if (dev.api < d->api) continue;

#if defined (GFX_USE_VK_SUBSET_DEVICES)
			// If all else fails, pick the non-subset device,
			// i.e. the Vulkan implementation that is fully conformant!
			if (!dev.subset && d->subset) break;
#endif
		}

		// Actual insert.
		gfx_vec_insert(&_groufix.devices, 1, &dev, j);
	}

	// Now loop over 'm again to init
	//  dev->{ base.{ name, driverName, driverInfo }, lock, formats }.
	// Because the number of devices never changes, the vector never
	// gets reallocated, thus we store & init these values and mutexes here.
	for (uint32_t i = 0; i < count; ++i)
	{
		_GFXDevice* dev = gfx_vec_at(&_groufix.devices, i);
		dev->base.name = dev->name;
		dev->base.driverName = dev->driverName;
		dev->base.driverInfo = dev->driverInfo;

		// Sneaky goto-based init/clear structure :o
		if (!_gfx_mutex_init(&dev->lock))
			goto clear_prev_devices;
		if (_gfx_device_init_formats(dev))
			continue; // Success!

		_gfx_mutex_clear(&dev->lock);

	clear_prev_devices:
		// If it could not init, remove remaining devices and let
		// _gfx_devices_terminate handle the rest.
		gfx_vec_pop(&_groufix.devices, count - i);
		goto terminate;
	}

	// I really wanna know the detected devices :)
	GFXBufWriter* logger = gfx_logger_info();
	if (logger != NULL)
	{
		gfx_io_writef(logger, "Detected physical Vulkan devices:\n");

		for (uint32_t i = 0; i < count; ++i)
		{
			_GFXDevice* dev = gfx_vec_at(&_groufix.devices, i);
			gfx_io_writef(logger,
				"    [ %s ] (%s | v%u.%u.%u%s)\n",
				dev->name,
				_gfx_log_device_types[dev->base.type],
				(unsigned int)VK_API_VERSION_MAJOR(dev->api),
				(unsigned int)VK_API_VERSION_MINOR(dev->api),
				(unsigned int)VK_API_VERSION_PATCH(dev->api),
				dev->base.available ? "" : " | not available");

			if (dev->driverName[0] != '\0' || dev->driverInfo[0] != '\0')
				gfx_io_writef(logger,
					"        driver: %s (%s)\n",
					dev->driverName[0] == '\0' ? "???" : dev->driverName,
					dev->driverInfo[0] == '\0' ? "???" : dev->driverInfo);
		}

		gfx_logger_end(logger);
	}

	return 1;


	// Cleanup on failure.
terminate:
	gfx_log_error("Could not find or initialize physical devices.");
	_gfx_devices_terminate();

	return 0;
}

/****************************/
void _gfx_devices_terminate(void)
{
	// Destroy all Vulkan contexts.
	while (_groufix.contexts.head != NULL)
		_gfx_destroy_context((_GFXContext*)_groufix.contexts.head);

	// And free all groufix devices, only entails clearing its mutex/formats.
	// Devices are allocated in-place so no need to free anything else.
	for (size_t i = 0; i < _groufix.devices.size; ++i)
	{
		_GFXDevice* dev = gfx_vec_at(&_groufix.devices, i);
		_gfx_mutex_clear(&dev->lock);
		gfx_vec_clear(&dev->formats);
	}

	// Regular cleanup.
	gfx_vec_clear(&_groufix.devices);
	gfx_list_clear(&_groufix.contexts);
}

/****************************/
_GFXContext* _gfx_device_init_context(_GFXDevice* device)
{
	assert(device != NULL);

	// Lock the device's lock to sync access to the device's context.
	// Once this call returns successfully the context will not be modified anymore,
	// which means after this call, we can just read device->context directly.
	_gfx_mutex_lock(&device->lock);

	if (device->context == NULL)
	{
		// We only use the context lock here to sync the context array.
		// Other uses happen during initialization or termination,
		// any other operation must happen inbetween those two
		// function calls anyway so no need to lock in them.
		_gfx_mutex_lock(&_groufix.contextLock);

		// No context, go search for a compatible one.
		for (
			_GFXContext* context = (_GFXContext*)_groufix.contexts.head;
			context != NULL;
			context = (_GFXContext*)context->list.next)
		{
			for (size_t j = 0; j < context->numDevices; ++j)
				if (context->devices[j] == device->vk.device)
				{
					device->context = context;
					goto unlock;
				}
		}

		// If none found, create a new one.
		// It returns if it was successful, but just ignore it...
		_gfx_create_context(device);

	unlock:
		_gfx_mutex_unlock(&_groufix.contextLock);
	}

	// Read the result before unlock just in case it failed,
	// only when succeeded are we sure we don't write to it anymore.
	_GFXContext* ret = device->context;

	_gfx_mutex_unlock(&device->lock);

	return ret;
}

/****************************/
_GFXQueueSet* _gfx_pick_family(_GFXContext* context, uint32_t* family,
                               VkQueueFlags flags, bool present)
{
	assert(context != NULL);
	assert(family != NULL);
	assert(flags != 0 || present);

	// The queue sets only report the flags they were specifically
	// picked for, including the presentation flag.
	// Therefore we just loop over the queue sets and pick the first that
	// satisfies our requirements :)
	for (
		_GFXQueueSet* set = (_GFXQueueSet*)context->sets.head;
		set != NULL;
		set = (_GFXQueueSet*)set->list.next)
	{
		// Check if the required flags and present bit are set.
		if ((set->flags & flags) != flags || (present && !set->present))
			continue;

		// Return it.
		*family = set->family;
		return set;
	}

	return NULL;
}

/****************************/
_GFXQueueSet* _gfx_pick_queue(_GFXContext* context, _GFXQueue* queue,
                              VkQueueFlags flags, bool present)
{
	assert(context != NULL);
	assert(queue != NULL);
	assert(flags != 0 || present);

	// Ok so we can abuse _gfx_pick_family and just take its data.
	uint32_t family;
	_GFXQueueSet* set = _gfx_pick_family(context, &family, flags, present);

	if (set != NULL)
	{
		// Pick a queue from the set.
		const uint32_t index = _gfx_queue_index(set, flags, present);

		// Get queue & return it.
		queue->family = family;
		queue->index = index;
		queue->lock = &set->locks[index];

		context->vk.GetDeviceQueue(
			context->vk.device, family, index, &queue->vk.queue);

		return set;
	}

	return NULL;
}

/****************************/
uint32_t _gfx_queue_index(_GFXQueueSet* set,
                          VkQueueFlags flags, bool present)
{
	assert(set != NULL);
	assert(flags != 0 || present);
	assert((set->flags & flags) == flags);
	assert(!present || set->present);

	// Pick a queue from the set.
	// This is done according to the order defined by
	// _gfx_vk_queue_priorities, every entry is checked for existence.
	// The graphics and presentation abilities always get the same index,
	// so hopefully we submit and present on the same queue.
	uint32_t index =
		(flags & VK_QUEUE_GRAPHICS_BIT || present) ? 0 :
		(flags & VK_QUEUE_COMPUTE_BIT) ?
			// For compute queues,
			// pick the second if there is a graphics/present queue.
			// pick the first otherwise.
			((set->flags & VK_QUEUE_GRAPHICS_BIT || set->present) ?
			1 : 0) :
		(flags & VK_QUEUE_TRANSFER_BIT) ?
			// For transfer queues,
			// pick the second if there is a graphics/present queue,
			// pick the third if there is ALSO a compute queue.
			// pick the second if there is ONLY a compute queue.
			// pick the first otherwise.
			((set->flags & VK_QUEUE_GRAPHICS_BIT || set->present) ?
			(set->flags & VK_QUEUE_COMPUTE_BIT ? 2 : 1) :
			(set->flags & VK_QUEUE_COMPUTE_BIT ? 1 : 0)) :
		0; // Nothing matched, hmmm...

	// If the queue does not exist, pick the last queue.
	// This way we cascade back to higher priority queues.
	if (index >= set->count)
		index = (uint32_t)set->count - 1;

	return index;
}

/****************************/
uint32_t _gfx_filter_families(GFXMemoryFlags flags, uint32_t* families)
{
	assert(families != NULL);

	uint32_t graphics = families[0];
	uint32_t compute = families[1];
	uint32_t transfer = families[2];

	// Make sure to only pick unique indices.
	compute =
		(flags & GFX_MEMORY_COMPUTE_CONCURRENT) &&
		compute != graphics ?
		compute : UINT32_MAX;

	transfer =
		(flags & GFX_MEMORY_TRANSFER_CONCURRENT) &&
		transfer != graphics &&
		transfer != compute ?
		transfer : UINT32_MAX;

	// And output them linearly, without missing families inbetween.
	// We always output the graphics family, as every resource is expected
	// to function within a renderer.
	families[0] = graphics;
	families[1] = compute != UINT32_MAX ? compute : transfer;
	families[2] = compute != UINT32_MAX ? transfer : UINT32_MAX;

	return
		families[2] != UINT32_MAX ? 3 :
		families[1] != UINT32_MAX ? 2 : 1;
}

/****************************/
GFX_API size_t gfx_get_num_devices(void)
{
	assert(atomic_load(&_groufix.initialized));

	return _groufix.devices.size;
}

/****************************/
GFX_API GFXDevice* gfx_get_device(size_t index)
{
	assert(atomic_load(&_groufix.initialized));
	assert(_groufix.devices.size > 0);
	assert(index < _groufix.devices.size);

	return gfx_vec_at(&_groufix.devices, index);
}

/****************************/
GFX_API GFXDevice* gfx_get_primary_device(void)
{
	assert(atomic_load(&_groufix.initialized));
	assert(_groufix.devices.size > 0);

	return gfx_vec_at(&_groufix.devices, 0);
}
