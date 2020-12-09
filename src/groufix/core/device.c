/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

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
		GFX_DEVICE_DISCRETE_GPU : \
	(vType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) ? \
		GFX_DEVICE_VIRTUAL_GPU : \
	(vType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) ? \
		GFX_DEVICE_INTEGRATED_GPU : \
	(vType == VK_PHYSICAL_DEVICE_TYPE_CPU) ? \
		GFX_DEVICE_CPU : \
		GFX_DEVICE_UNKNOWN)


/****************************
 * Array of Vulkan queue priority values in [0,1].
 * TODO: For now just a singular 1, changes when more queues get created...
 */
static const float _gfx_vk_queue_priorities[] = { 1.0f };


/****************************
 * Creates an array of queue families and VkDeviceQueueCreateInfo structures
 * desired by the groufix implementation, for a given device.
 * @param families    Output families, must call free() to release it.
 * @param createInfos Output create info, must call free() to release it.
 * @return The size of the arrays.
 */
static uint32_t _gfx_device_get_queues(
	VkPhysicalDevice          device,
	_GFXQueueFamily**         families,
	VkDeviceQueueCreateInfo** createInfos)
{
	// We allocate enough for the maximum number of queue families we create.
	// The following properties need to be supported by at least one queue:
	// 1) A general graphics family.
	// 2) A family that supports presentation to surfaces.
	// TODO: 3) A compute-only family for use when others are stalling.
	// TODO: 4) A transfer-only family? Is this useful?
	// For each property, we keep track of whether we found an appropriate
	// queue or not.
	uint32_t num    = 2;
	int hasGraphics = 0;
	int hasPresent  = 0;

	*families = malloc(sizeof(_GFXQueueFamily) * num);
	*createInfos = malloc(sizeof(VkDeviceQueueCreateInfo) * num);

	if (*families == NULL || *createInfos == NULL)
		goto clean;

	// Just initialize all structures to some defaults.
	// Default is to create 1 queue of each family.
	for (uint32_t i = 0; i < num; ++i)
	{
		(*families)[i] = (_GFXQueueFamily){
			.present = 0,
			.count   = 1
		};

		(*createInfos)[i] = (VkDeviceQueueCreateInfo){
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,

			.pNext            = NULL,
			.flags            = 0,
			.queueCount       = 1,
			.pQueuePriorities = _gfx_vk_queue_priorities
		};
	}

	// So get all queue families, do the searching...
	uint32_t count;
	_groufix.vk.GetPhysicalDeviceQueueFamilyProperties(
		device, &count, NULL);

	// We use a scope here so the goto above is allowed.
	{
		// Enumerate all queue families.
		VkQueueFamilyProperties props[count];

		_groufix.vk.GetPhysicalDeviceQueueFamilyProperties(
			device, &count, props);

		// 1) A general graphics family:
		// We use the family with VK_QUEUE_GRAPHICS BIT set and
		// as few other bits set as possible.
		// 2) A family that supports presentation to surfaces:
		// Having presentation support has precedence over fewer flags.
		// So if we find a graphics family with presentation support, gogogo!
		// Note we do not check for presentation to a specific surface yet.
		for (uint32_t i = 0; i < count; ++i)
			if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				int present = glfwGetPhysicalDevicePresentationSupport(
					_groufix.vk.instance, device, i);
				int better =
					(!(*families[0]).present && present) ||
					(props[i].queueFlags < (*families)[0].flags &&
					(!(*families[0]).present || present));

				if (hasGraphics && !better)
					continue;

				hasGraphics = 1;

				// Pick this family as graphics family.
				(*createInfos)[0].queueFamilyIndex = i;
				(*families)[0].flags   = props[i].queueFlags;
				(*families)[0].present = present;
				(*families)[0].index   = i;

				// Also pick it as presentation family.
				if (!hasPresent && present)
				{
					hasPresent = 1;
					num -= 1; // We do not rquire an additional family.
				}
			}

		// Check if we found a graphics family.
		if (!hasGraphics)
		{
			gfx_log_error("Could not find a queue family with VK_QUEUE_GRAPHICS_BIT set.");
			goto clean;
		}

		// 2) A family that supports presentation to surfaces:
		// If no graphics family supports presentation, find another family.
		// Again we prefer fewer bits.
		if (!hasPresent) for (uint32_t i = 0; i < count; ++i)
			if (glfwGetPhysicalDevicePresentationSupport(
				_groufix.vk.instance, device, i))
			{
				if (hasPresent && props[i].queueFlags >= (*families)[1].flags)
					continue;

				hasPresent = 1;

				// Pick this family as novel presentation family.
				(*createInfos)[1].queueFamilyIndex = i;
				(*families)[1].flags   = props[i].queueFlags;
				(*families)[1].present = 1;
				(*families)[1].index   = i;
			}

		// Check if we found a presentation family.
		if (!hasPresent)
		{
			gfx_log_error("Could not find a queue family with presentation support.");
			goto clean;
		}

		return num;
	}

	// Cleanup on failure.
clean:
	free(*families);
	*families = NULL;

	free(*createInfos);
	*createInfos = NULL;

	return 0;
}

/****************************
 * Creates an appropriate context (Vulkan device + fp's) suited for a device.
 * @param device Cannot be NULL.
 * @return Zero on failure.
 */
static int _gfx_device_create_context(_GFXDevice* device)
{
	assert(device->context == NULL);

	_GFXContext* context = NULL;

	// First of all, check Vulkan version.
	if (device->api < _GFX_VK_VERSION)
	{
		gfx_log_error("Physical device does not support Vulkan version %u.%u.%u.",
			(unsigned int)VK_VERSION_MAJOR(_GFX_VK_VERSION),
			(unsigned int)VK_VERSION_MINOR(_GFX_VK_VERSION),
			(unsigned int)VK_VERSION_PATCH(_GFX_VK_VERSION));

		goto error;
	}

	// Call the thing that gets us the arrays of desired queues.
	// These are explicitly freed on cleanup or success.
	// When a future device also uses this context,
	// it is assumed it has equivalent queue family properties.
	// If there are any device groups such that this is the case,
	// you probably have equivalent GPUs in an SLI/CrossFire setup anyway...
	_GFXQueueFamily* families;
	VkDeviceQueueCreateInfo* createInfos;

	uint32_t famCount = _gfx_device_get_queues(
		device->vk.device, &families, &createInfos);

	if (famCount == 0)
		goto clean;

	// Now that we have desired queues, we can go create a context.
	// So first of all we find a device group which this device is part of.
	// We take the first device group we find, this assumes a device is never
	// seen in multiple groups...
	// Then we create a logical Vulkan device for this entire group.
	// Later on, any other device in the group will also use this context.
	uint32_t count;
	VkResult result = _groufix.vk.EnumeratePhysicalDeviceGroups(
		_groufix.vk.instance, &count, NULL);

	if (result != VK_SUCCESS || count == 0)
		goto clean;

	// Again with the goto-proof scope.
	{
		// Enumerate all device groups.
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
		{
			// Probably want to know when a device is somehow invalid...
			gfx_log_error("Physical device could not be found in any device group.");
			goto clean;
		}

		// Ok so we found a group, now go create a context.
		// We allocate two arrays at the end of the context:
		// 1) holding the queue families created for the device.
		// 2) holding the physical devices in the device group,
		// this is used to check if a future device can use this context.
		context = malloc(
			sizeof(_GFXContext) +
			sizeof(_GFXQueueFamily) * famCount +
			sizeof(VkPhysicalDevice) * groups[i].physicalDeviceCount);

		if (context == NULL)
			goto clean;

		if (!gfx_vec_push(&_groufix.contexts, 1, &context))
			goto clean;

		device->index = j;
		device->context = context;

		context->numFamilies = famCount;
		context->families = (_GFXQueueFamily*)(context + 1);

		context->numDevices = groups[i].physicalDeviceCount;
		context->devices = (VkPhysicalDevice*)(context->families + famCount);

		memcpy(
			context->families,
			families,
			sizeof(_GFXQueueFamily) * context->numFamilies);

		memcpy(
			context->devices,
			groups[i].physicalDevices,
			sizeof(VkPhysicalDevice) * context->numDevices);

		// Set this to NULL so we don't accidentally call garbage on cleanup.
		context->vk.DestroyDevice = NULL;

		// Finally go create the logical Vulkan device.
		// Enable VK_KHR_swapchain so we can interact with surfaces from GLFW.
		// Enable VK_LAYER_KHRONOS_validation if debug,
		// this is deprecated by now, but for older Vulkan versions.
		const char* extensions[] = { "VK_KHR_swapchain" };
#if !defined (NDEBUG)
		const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
#endif

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
			.queueCreateInfoCount    = famCount,
			.pQueueCreateInfos       = createInfos,
#if defined (NDEBUG)
			.enabledLayerCount       = 0,
			.ppEnabledLayerNames     = NULL,
#else
			.enabledLayerCount       = sizeof(layers)/sizeof(char*),
			.ppEnabledLayerNames     = layers,
#endif
			.enabledExtensionCount   = sizeof(extensions)/sizeof(char*),
			.ppEnabledExtensionNames = extensions,
			.pEnabledFeatures        = NULL // TODO: Will probably want to populate this.
		};

		result = _groufix.vk.CreateDevice(
			device->vk.device, &dci, NULL, &context->vk.device);

		if (result != VK_SUCCESS)
		{
			_gfx_vulkan_log(result);
			goto clean;
		}

		// This is like a moment to celebrate, right?
		// We count the number of actual queues here.
		uint32_t queueCount = 0;
		for (size_t k = 0; k < context->numFamilies; ++k)
			queueCount += context->families[k].count;

		gfx_log_debug(
			"Logical Vulkan device of version %u.%u.%u with %u physical "
			"device(s) and %u queue(s) created for physical device group "
			"containing at least: %s.",
			(unsigned int)VK_VERSION_MAJOR(device->api),
			(unsigned int)VK_VERSION_MINOR(device->api),
			(unsigned int)VK_VERSION_PATCH(device->api),
			(unsigned int)context->numDevices,
			(unsigned int)queueCount,
			device->base.name);

		// Now load all device level Vulkan functions.
		// Load vkDestroyDevice first so we can clean properly.
		_GFX_GET_DEVICE_PROC_ADDR(DestroyDevice);
		_GFX_GET_DEVICE_PROC_ADDR(AcquireNextImageKHR);
		_GFX_GET_DEVICE_PROC_ADDR(AllocateCommandBuffers);
		_GFX_GET_DEVICE_PROC_ADDR(BeginCommandBuffer);
		_GFX_GET_DEVICE_PROC_ADDR(CmdClearColorImage);
		_GFX_GET_DEVICE_PROC_ADDR(CmdPipelineBarrier);
		_GFX_GET_DEVICE_PROC_ADDR(CreateCommandPool);
		_GFX_GET_DEVICE_PROC_ADDR(CreateFence);
		_GFX_GET_DEVICE_PROC_ADDR(CreateSemaphore);
		_GFX_GET_DEVICE_PROC_ADDR(CreateSwapchainKHR);
		_GFX_GET_DEVICE_PROC_ADDR(DestroyCommandPool);
		_GFX_GET_DEVICE_PROC_ADDR(DestroyFence);
		_GFX_GET_DEVICE_PROC_ADDR(DestroySemaphore);
		_GFX_GET_DEVICE_PROC_ADDR(DestroySwapchainKHR);
		_GFX_GET_DEVICE_PROC_ADDR(DeviceWaitIdle);
		_GFX_GET_DEVICE_PROC_ADDR(EndCommandBuffer);
		_GFX_GET_DEVICE_PROC_ADDR(FreeCommandBuffers);
		_GFX_GET_DEVICE_PROC_ADDR(GetDeviceQueue);
		_GFX_GET_DEVICE_PROC_ADDR(GetSwapchainImagesKHR);
		_GFX_GET_DEVICE_PROC_ADDR(QueuePresentKHR);
		_GFX_GET_DEVICE_PROC_ADDR(QueueSubmit);
		_GFX_GET_DEVICE_PROC_ADDR(QueueWaitIdle);
		_GFX_GET_DEVICE_PROC_ADDR(ResetCommandPool);
		_GFX_GET_DEVICE_PROC_ADDR(ResetFences);
		_GFX_GET_DEVICE_PROC_ADDR(WaitForFences);

		free(families);
		free(createInfos);

		return 1;
	}

	// Cleanup on failure.
clean:
	if (device->context != NULL)
	{
		gfx_vec_pop(&_groufix.contexts, 1);

		// If vkDestroyDevice is available, properly clean the device.
		if (context->vk.DestroyDevice != NULL)
			context->vk.DestroyDevice(context->vk.device, NULL);
	}

	free(context);
	free(families);
	free(createInfos);

error:
	gfx_log_error(
		"Could not create or initialize a logical Vulkan device for physical "
		"device group containing at least: %s.",
		device->base.name);

	device->index = 0;
	device->context = NULL;

	return 0;
}

/****************************/
int _gfx_devices_init(void)
{
	assert(_groufix.vk.instance != NULL);
	assert(_groufix.devices.size == 0);

	// Reserve and create groufix devices.
	// The number or order of devices never changes after initialization,
	// nor is there a user pointer for callbacks, as there are no callbacks.
	// This means we do not have to dynamically allocate the devices.
	uint32_t count;
	VkResult result = _groufix.vk.EnumeratePhysicalDevices(
		_groufix.vk.instance, &count, NULL);

	if (result != VK_SUCCESS || count == 0)
		goto terminate;

	// Again with the goto-proof scope.
	{
		// Enumerate all devices.
		VkPhysicalDevice devices[count];

		result = _groufix.vk.EnumeratePhysicalDevices(
			_groufix.vk.instance, &count, devices);

		if (result != VK_SUCCESS)
			goto terminate;

		// Fill the array of groufix devices.
		// While doing so, keep track of the primary device,
		// this to make sure the primary device is at index 0.
		if (!gfx_vec_reserve(&_groufix.devices, (size_t)count))
			goto terminate;

		GFXDeviceType type = GFX_DEVICE_UNKNOWN;
		uint32_t ver = 0;

		for (uint32_t i = 0; i < count; ++i)
		{
			// Get some Vulkan properties and create new device.
			VkPhysicalDeviceProperties pdp;
			_groufix.vk.GetPhysicalDeviceProperties(devices[i], &pdp);

			_GFXDevice dev = {
				.base    = { .type = _GFX_GET_DEVICE_TYPE(pdp.deviceType) },
				.api     = pdp.apiVersion,
				.index   = 0,
				.context = NULL,
				.vk      = { .device = devices[i] }
			};

			// Init mutex and name string.
			if (!_gfx_mutex_init(&dev.lock))
				goto terminate;

			size_t len = strlen(pdp.deviceName);
			dev.base.name = malloc(sizeof(char*) * (len+1));

			if (dev.base.name == NULL)
			{
				_gfx_mutex_clear(&dev.lock);
				goto terminate;
			}

			strcpy((char*)dev.base.name, pdp.deviceName);
			((char*)dev.base.name)[len] = '\0';

			// Check if the new device is a better pick as primary.
			// If the type of device is superior, pick it as primary.
			// If the type is equal, pick the greater Vulkan version.
			int isPrim = (i == 0) ||
				dev.base.type < type ||
				(dev.base.type == type && pdp.apiVersion > ver);

			if (!isPrim)
				gfx_vec_push(&_groufix.devices, 1, &dev);
			else
			{
				// If new primary, insert it at index 0.
				gfx_vec_insert(&_groufix.devices, 1, &dev, 0);
				type = dev.base.type;
				ver = pdp.apiVersion;
			}
		}

		return 1;
	}

	// Cleanup on failure.
terminate:
	gfx_log_error("Could not find or initialize physical devices.");
	_gfx_devices_terminate();

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

	// And free all groufix devices, this only entails freeing the name string.
	// Devices are allocated in-place so no need to free anything else.
	for (size_t i = 0; i < _groufix.devices.size; ++i)
	{
		_GFXDevice* device = gfx_vec_at(&_groufix.devices, i);

		free((char*)device->base.name);
		_gfx_mutex_clear(&device->lock);
	}

	// Regular cleanup.
	gfx_vec_clear(&_groufix.devices);
	gfx_vec_clear(&_groufix.contexts);
}

/****************************/
int _gfx_device_init_context(_GFXDevice* device)
{
	assert(device != NULL);

	int ret = 0;

	// Lock the device's lock to sync access to the device's context.
	// Once this call returns successfully the context will not be set anymore,
	// which means after this call, we can just read device->context directly.
	_gfx_mutex_lock(&device->lock);

	ret = device->context != NULL;

	if (!ret)
	{
		// We only use the context lock here to sync the context array.
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

					ret = 1;
					goto unlock;
				}
		}

		// If none found, create a new one.
		ret = _gfx_device_create_context(device);

	unlock:
		_gfx_mutex_unlock(&_groufix.contextLock);
	}

	_gfx_mutex_unlock(&device->lock);

	return ret;
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

/****************************/
GFX_API GFXDevice* gfx_get_primary_device(void)
{
	assert(_groufix.devices.size > 0);

	return gfx_vec_at(&_groufix.devices, 0);
}
