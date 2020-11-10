/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/device.h"
#include "groufix/core.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>


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
	(vType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) ? \
		GFX_DEVICE_VIRTUAL_GPU : \
	(vType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) ? \
		GFX_DEVICE_INTEGRATED_GPU : \
	(vType == VK_PHYSICAL_DEVICE_TYPE_CPU) ? \
		GFX_DEVICE_CPU : \
		GFX_DEVICE_UNKNOWN)


/****************************
 * Creates an appropriate context (Vulkan device + fp's) suited for a device.
 * @param device Cannot be NULL.
 * @return NULL on failure.
 */
static int _gfx_device_init_context(_GFXDevice* device)
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
		device->index = j;
		device->context = context;

		// Set this to NULL so we don't accidentally call garbage on cleanup.
		context->vk.DestroyDevice = NULL;

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
		_GFX_GET_DEVICE_PROC_ADDR(DeviceWaitIdle);

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
int _gfx_devices_init(void)
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
void _gfx_devices_terminate(void)
{
	// Destroy all logical Vulkan devices and free the contexts.
	// Luckily this relies on things being inserted in the vector,
	// therefore this is a no-op when devices are not initialized.
	for (size_t i = 0; i < _groufix.contexts.size; ++i)
	{
		_GFXContext* context =
			*(_GFXContext**)gfx_vec_at(&_groufix.contexts, i);

		context->vk.DeviceWaitIdle(context->vk.device);
		context->vk.DestroyDevice(context->vk.device, NULL);

		free(context);
	}

	// Regular cleanup.
	gfx_vec_clear(&_groufix.devices);
	gfx_vec_clear(&_groufix.contexts);
}

/****************************/
_GFXContext* _gfx_device_get_context(_GFXDevice* device)
{
	assert(device != NULL);

	if (device->context == NULL)
	{
		// We only use the context lock here.
		// Other uses happen during initialization or termination,
		// any other operation must happen inbetween those two
		// function calls anyway so no need to lock in them.
		_gfx_mutex_lock(&_groufix.contextLock);

		// No context, go search for a compatible one.
		for (size_t i = 0; i < _groufix.contexts.size; ++i)
		{
			_GFXContext* context =
				*(_GFXContext**)gfx_vec_at(&_groufix.contexts, i);

			for (size_t j = 0; j < context->numDevices; ++j)
				if (context->devices[j] == device->vk.device)
				{
					device->index = j;
					device->context = context;

					goto unlock;
				}
		}

		// If none found, create a new one.
		// It returns whether it succeeded, but just ignore this result.
		// If it failed, device->context will be NULL anyway.
		_gfx_device_init_context(device);

	unlock:
		_gfx_mutex_unlock(&_groufix.contextLock);
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
