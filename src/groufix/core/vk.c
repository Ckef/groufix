/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/log.h"
#include "groufix/core.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define _GFX_GET_INSTANCE_PROC_ADDR(pName) \
	_groufix.vk.pName = (PFN_vk##pName)glfwGetInstanceProcAddress( \
		_groufix.vk.instance, "vk"#pName); \
	if (_groufix.vk.pName == NULL) \
	{ \
		gfx_log_error("Could not load vk"#pName"."); \
		goto clean; \
	}

#define _GFX_GET_DEVICE_PROC_ADDR(pName) \
	context->vk.pName = (PFN_vk##pName)_groufix.vk.GetDeviceProcAddr( \
		context->vk.device, "vk"#pName); \
	if (context->vk.pName == NULL) \
	{ \
		gfx_log_error("Could not load vk"#pName"."); \
		goto clean; \
	}

#define _GFX_GET_DEVICE_TYPE(vType) \
	((vType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ? \
		GFX_DEVICE_DEDICATED_GPU : \
	(vType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) ? \
		GFX_DEVICE_INTEGRATED_GPU : \
	(vType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) ? \
		GFX_DEVICE_VIRTUAL_GPU : \
	(vType == VK_PHYSICAL_DEVICE_TYPE_CPU) ? \
		GFX_DEVICE_CPU : \
		GFX_DEVICE_UNKNOWN)


/****************************/
void _gfx_vulkan_log(VkResult result)
{
	switch (result)
	{
	case VK_SUCCESS:
		gfx_log_info("Vulkan: Success.");
		break;
	case VK_NOT_READY:
		gfx_log_warn("Vulkan: A fence or query has not yet completed.");
		break;
	case VK_TIMEOUT:
		gfx_log_warn("Vulkan: A wait operation has not completed in the "
		             "specified time.");
		break;
	case VK_EVENT_SET:
		gfx_log_info("Vulkan: An event is signaled.");
		break;
	case VK_EVENT_RESET:
		gfx_log_info("Vulkan: An event is unsignaled.");
		break;
	case VK_INCOMPLETE:
		gfx_log_warn("Vulkan: A return array was too small for the result.");
		break;
	case VK_ERROR_OUT_OF_HOST_MEMORY:
		gfx_log_error("Vulkan: A host memory allocation has failed.");
		break;
	case VK_ERROR_OUT_OF_DEVICE_MEMORY:
		gfx_log_error("Vulkan: A device memory allocation has failed.");
		break;
	case VK_ERROR_INITIALIZATION_FAILED:
		gfx_log_error("Vulkan: initialization of an object could not be "
		              "completed for implementation-specific reasons.");
		break;
	case VK_ERROR_DEVICE_LOST:
		gfx_log_error("Vulkan: A logical or physical device has been lost.");
		break;
	case VK_ERROR_MEMORY_MAP_FAILED:
		gfx_log_error("Vulkan: Mapping of a memory object has failed.");
		break;
	case VK_ERROR_LAYER_NOT_PRESENT:
		gfx_log_error("Vulkan: A requested layer is not present or could not "
		              "be loaded.");
		break;
	case VK_ERROR_EXTENSION_NOT_PRESENT:
		gfx_log_error("Vulkan: A requested extension is not supported.");
		break;
	case VK_ERROR_FEATURE_NOT_PRESENT:
		gfx_log_error("Vulkan: A requested feature is not supported.");
		break;
	case VK_ERROR_INCOMPATIBLE_DRIVER:
		gfx_log_error("Vulkan: The requested version of Vulkan is not "
		              "supported by the driver or is otherwise incompatible "
		              "for implementation-specific reasons.");
		break;
	case VK_ERROR_TOO_MANY_OBJECTS:
		gfx_log_error("Vulkan: Too many objects of a type have already been "
		              "created.");
		break;
	case VK_ERROR_FORMAT_NOT_SUPPORTED:
		gfx_log_error("Vulkan: A requested format is not supported on this "
		              "device.");
		break;
	case VK_ERROR_FRAGMENTED_POOL:
		gfx_log_error("Vulkan: A pool allocation has failed due to "
		              "fragmentation of the pool's memory.");
		break;
	case VK_ERROR_OUT_OF_POOL_MEMORY:
		gfx_log_error("Vulkan: A pool memory allocation has failed.");
		break;
	case VK_ERROR_INVALID_EXTERNAL_HANDLE:
		gfx_log_error("Vulkan: An external handle is not a valid handle of "
		              "the specified type.");
		break;
	case VK_ERROR_FRAGMENTATION:
		gfx_log_error("Vulkan: A descriptor pool creation has failed due to "
		              "fragmentation.");
		break;
	case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
		gfx_log_error("Vulkan: A buffer creation or memory allocation failed "
		              "because the requested address is not available.");
		break;

	default:
		gfx_log_error("Vulkan: Unknown error.");
		break;
	}
}

/****************************/
static int _gfx_vulkan_init_devices(void)
{
	assert(_groufix.devices.size == 0);

	// Reserve and create groufix devices.
	// There are no callbacks, so no user pointer,
	// this means we do not have to dynamically allocate the devices.
	uint32_t count;
	VkResult result = _groufix.vk.EnumeratePhysicalDevices(
		_groufix.vk.instance, &count, NULL);

	if (result != VK_SUCCESS || count == 0)
		goto clean;

	{
		// Enumerate all devices.
		// We use a scope here so the goto above is allowed.
		VkPhysicalDevice devices[count];

		result = _groufix.vk.EnumeratePhysicalDevices(
			_groufix.vk.instance, &count, devices);

		if (result != VK_SUCCESS)
			goto clean;

		// Fill the array of groufix devices.
		if (!gfx_vec_reserve(&_groufix.devices, (size_t)count))
			goto clean;

		for (uint32_t i = 0; i < count; ++i)
		{
			// Get some Vulkan properties.
			VkPhysicalDeviceProperties pdp;
			_groufix.vk.GetPhysicalDeviceProperties(devices[i], &pdp);

			_GFXDevice dev = {
				.base    = { .type = _GFX_GET_DEVICE_TYPE(pdp.deviceType) },
				.index   = 0,
				.context = NULL,
				.vk      = { .device = devices[i] }
			};

			gfx_vec_push(&_groufix.devices, 1, &dev);
		}

		return 1;
	}

	// Cleanup on failure.
clean:
	gfx_log_error("Could not find or initialize physical devices.");
	gfx_vec_clear(&_groufix.devices);

	return 0;
}

/****************************/
static int _gfx_vulkan_init_context(_GFXDevice* device)
{
	assert(device->context == NULL);

	_GFXContext* context = NULL;

	// So first of all we find a device group which this device is part of.
	// Then we create a logical Vulkan device for this entire group.
	// Later on, any other device in the group will also use this context.
	uint32_t count;
	VkResult result = _groufix.vk.EnumeratePhysicalDeviceGroups(
		_groufix.vk.instance, &count, NULL);

	if (result != VK_SUCCESS || count == 0)
		goto clean;

	{
		// Enumerate all device groups.
		// Again with the goto-proof scope.
		VkPhysicalDeviceGroupProperties groups[count];

		result = _groufix.vk.EnumeratePhysicalDeviceGroups(
			_groufix.vk.instance, &count, groups);

		if (result != VK_SUCCESS)
			goto clean;

		// Loop over all groups and see if one contains this device.
		// We keep track of the index of the group and the device in it.
		size_t i, j;
		for (i = 0; i < count; ++i)
		{
			for (j = 0; j < groups[i].physicalDeviceCount; ++j)
				if (groups[i].physicalDevices[j] == device->vk.device)
					break;

			if (j < groups[i].physicalDeviceCount)
				break;
		}

		if (i >= count)
			goto clean;

		// Ok so we found a group, now go create a context.
		// We allocate an array at the end of the context,
		// holding the physical devices in the device group.
		// This is used to check if a future device can use this context.
		if (!gfx_vec_reserve(&_groufix.contexts, _groufix.contexts.size + 1))
			goto clean;

		context = malloc(
			sizeof(_GFXContext) +
			groups[i].physicalDeviceCount * sizeof(VkPhysicalDevice));

		if (context == NULL)
			goto clean;

		gfx_vec_push(&_groufix.contexts, 1, &context);

		// Set this to NULL so we don't accidentally call garbage on cleanup.
		context->vk.DestroyDevice = NULL;

		device->index = j;
		device->context = context;
		context->numDevices = groups[i].physicalDeviceCount;

		memcpy(
			context->devices,
			groups[i].physicalDevices,
			context->numDevices * sizeof(VkPhysicalDevice));

		// Finally go create the logical Vulkan device.
		VkDeviceGroupDeviceCreateInfo dgdci = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO,

			.pNext               = NULL,
			.physicalDeviceCount = (uint32_t)context->numDevices,
			.pPhysicalDevices    = context->devices
		};

		VkDeviceCreateInfo dci = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,

			.pNext                   = &dgdci,
			.flags                   = 0,
			.queueCreateInfoCount    = 0,    // TODO: obviously > 0.
			.pQueueCreateInfos       = NULL, // TODO: Cannot be NULL!
			.enabledLayerCount       = 0,
			.ppEnabledLayerNames     = NULL,
			.enabledExtensionCount   = 0,
			.ppEnabledExtensionNames = NULL,
			.pEnabledFeatures        = NULL
		};

		result = _groufix.vk.CreateDevice(
			device->vk.device, &dci, NULL, &context->vk.device);

		if (result != VK_SUCCESS)
		{
			_gfx_vulkan_log(result);
			goto clean;
		}

		// This is like a moment to celebrate, right?
		gfx_log_info("Logical Vulkan device created.");

		// Now load all device level Vulkan functions.
		// Load vkDestroyDevice first so we can clean properly.
		_GFX_GET_DEVICE_PROC_ADDR(DestroyDevice);

		return 1;
	}

	// Cleanup on failure.
clean:
	gfx_log_error("Could not create or initialize a logical Vulkan device.");

	if (device->context != NULL)
	{
		gfx_vec_pop(&_groufix.contexts, 1);

		// If vkDestroyDevice is available, properly clean the device.
		if (context->vk.DestroyDevice != NULL)
			context->vk.DestroyDevice(context->vk.device, NULL);
	}

	free(context);
	device->index = 0;
	device->context = NULL;

	return 0;
}

/****************************/
int _gfx_vulkan_init(void)
{
	assert(_groufix.vk.instance == NULL);

	// Set this to NULL so we don't accidentally call garbage on cleanup.
	_groufix.vk.DestroyInstance = NULL;

	// So first things first, we need to create a Vulkan instance.
	// For this we load the global level vkCreateInstance function and
	// we tell it what extensions we need, these include GLFW extensions.
	// Also, load _all_ global level Vulkan functions here.
	_GFX_GET_INSTANCE_PROC_ADDR(CreateInstance);
	_GFX_GET_INSTANCE_PROC_ADDR(EnumerateInstanceVersion);

	uint32_t count;
	const char** extensions =
		glfwGetRequiredInstanceExtensions(&count);

	if (extensions == NULL)
		goto clean;

	// Ok now go create a Vulkan instance.
	uint32_t version;
	_groufix.vk.EnumerateInstanceVersion(&version);

	VkApplicationInfo ai = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,

		.pNext              = NULL,
		.pApplicationName   = NULL,
		.applicationVersion = 0,
		.pEngineName        = "groufix",
		.engineVersion      = 0,
		.apiVersion         = version
	};

	VkInstanceCreateInfo ici = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,

		.pNext                   = NULL,
		.flags                   = 0,
		.pApplicationInfo        = &ai,
		.enabledLayerCount       = 0,
		.ppEnabledLayerNames     = NULL,
		.enabledExtensionCount   = count,
		.ppEnabledExtensionNames = extensions
	};

	VkResult result = _groufix.vk.CreateInstance(
		&ici, NULL, &_groufix.vk.instance);

	if (result != VK_SUCCESS)
	{
		_gfx_vulkan_log(result);
		goto clean;
	}

	// Knowing the Vulkan version is always useful.
	gfx_log_info("Vulkan instance of version %u.%u.%u created.",
		(unsigned int)VK_VERSION_MAJOR(version),
		(unsigned int)VK_VERSION_MINOR(version),
		(unsigned int)VK_VERSION_PATCH(version));

	// Now load all instance level Vulkan functions.
	// Load vkDestroyInstance first so we can clean properly.
	_GFX_GET_INSTANCE_PROC_ADDR(DestroyInstance);
	_GFX_GET_INSTANCE_PROC_ADDR(CreateDevice);
	_GFX_GET_INSTANCE_PROC_ADDR(EnumeratePhysicalDeviceGroups);
	_GFX_GET_INSTANCE_PROC_ADDR(EnumeratePhysicalDevices);
	_GFX_GET_INSTANCE_PROC_ADDR(GetDeviceProcAddr);
	_GFX_GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceProperties);

	// Initialize physical devices.
	if (!_gfx_vulkan_init_devices())
		goto clean;

	return 1;

	// Cleanup on failure.
clean:
	gfx_log_error("Could not create or initialize a Vulkan instance.");

	// If vkDestroyInstance is available, properly clean the instance.
	if (_groufix.vk.DestroyInstance != NULL)
		_groufix.vk.DestroyInstance(_groufix.vk.instance, NULL);

	_groufix.vk.instance = NULL;

	return 0;
}

/****************************/
void _gfx_vulkan_terminate(void)
{
	// No assert, this function is a no-op if Vulkan is not initialized.
	if (_groufix.vk.instance == NULL)
		return;

	// Destroy all logical Vulkan devices and free the contexts.
	for (size_t i = 0; i < _groufix.contexts.size; ++i)
	{
		_GFXContext* context =
			*(_GFXContext**)gfx_vec_at(&_groufix.contexts, i);

		context->vk.DestroyDevice(context->vk.device, NULL);
		free(context);
	}

	// Some cleanup.
	gfx_vec_clear(&_groufix.devices);
	gfx_vec_clear(&_groufix.contexts);

	_groufix.vk.DestroyInstance(_groufix.vk.instance, NULL);

	// Signal that termination is done.
	_groufix.vk.instance = NULL;
}

/****************************/
_GFXContext* _gfx_vulkan_get_context(_GFXDevice* device)
{
	assert(device != NULL);

	// First check if it already has a context.
	if (device->context == NULL)
	{
		// If it doesn't, go search for a compatible one.
		for (size_t i = 0; i < _groufix.contexts.size; ++i)
		{
			_GFXContext* context =
				*(_GFXContext**)gfx_vec_at(&_groufix.contexts, i);

			for (size_t j = 0; j < context->numDevices; ++j)
				if (context->devices[j] == device->vk.device)
				{
					device->index = j;
					device->context = context;

					return context;
				}
		}

		// If none found, create a new one.
		if (!_gfx_vulkan_init_context(device))
			return NULL;
	}

	return device->context;
}

/****************************/
GFX_API size_t gfx_get_num_devices(void)
{
	return _groufix.devices.size;
}

/****************************/
GFX_API GFXDevice* gfx_get_device(size_t index)
{
	assert(_groufix.devices.size > 0);
	assert(index < _groufix.devices.size);

	return gfx_vec_at(&_groufix.devices, index);
}
