/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <assert.h>
#include <string.h>


#define _GFX_GET_INSTANCE_PROC_ADDR(pName) \
	_groufix.vk.pName = (PFN_vk##pName)glfwGetInstanceProcAddress( \
		_groufix.vk.instance, "vk"#pName); \
	if (_groufix.vk.pName == NULL) \
	{ \
		gfx_log_error("Could not load vk"#pName"."); \
		goto clean; \
	}


#if !defined (NDEBUG)

/****************************
 * Callback for Vulkan debug messages.
 */
static VkBool32 VKAPI_PTR _gfx_vulkan_message(
	VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT             messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void*                                       pUserData)
{
	// General events go to verbose debug...
	if (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
		gfx_log_verbose("Vulkan: %s", pCallbackData->pMessage);
	else
	{
		// Info goes to debug, verbose goes to verbose debug.
		// We don't use info as this is a debug feature anyway.
		switch (messageSeverity)
		{
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
			gfx_log_verbose("Vulkan: %s", pCallbackData->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
			gfx_log_debug("Vulkan: %s", pCallbackData->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
			gfx_log_warn("Vulkan: %s", pCallbackData->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			gfx_log_error("Vulkan: %s", pCallbackData->pMessage);
			break;

		default:
			break;
		}
	}

	return VK_FALSE;
}

#endif

/****************************/
void _gfx_vulkan_log(VkResult result)
{
	switch (result)
	{
	case VK_SUCCESS:
		gfx_log_debug("Vulkan: Success.");
		break;
	case VK_NOT_READY:
		gfx_log_warn("Vulkan: A fence or query has not yet completed.");
		break;
	case VK_TIMEOUT:
		gfx_log_warn("Vulkan: A wait operation has not completed in the "
		             "specified time.");
		break;
	case VK_EVENT_SET:
		gfx_log_debug("Vulkan: An event is signaled.");
		break;
	case VK_EVENT_RESET:
		gfx_log_debug("Vulkan: An event is unsignaled.");
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
	case VK_ERROR_SURFACE_LOST_KHR:
		gfx_log_error("A surface is no longer available.");
		break;
	case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
		gfx_log_error("The requested window is already in use by Vulkan or "
		              "another API in a manner which prevents it from being "
		              "used again.");
		break;

	default:
		gfx_log_error("Vulkan: Unknown error.");
		break;
	}
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

	uint32_t glfwCount;
	const char** glfwExtensions =
		glfwGetRequiredInstanceExtensions(&glfwCount);

	if (glfwExtensions == NULL)
		goto clean;

	// We use a scope here so the goto above is allowed.
	{
		// Add our own extensions and layers if in debug mode.
#if !defined (NDEBUG)
		uint32_t count = glfwCount + 1;
		const char* extensions[count];
		memcpy(extensions, glfwExtensions, sizeof(char*) * glfwCount);

		// VK_EXT_debug_utils so we can log Vulkan debug messages.
		extensions[glfwCount] = "VK_EXT_debug_utils";

		// Enable VK_LAYER_KHRONOS_validation if debug.
		const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
#endif

		// Ok now go create a Vulkan instance.
		uint32_t version;
		_groufix.vk.EnumerateInstanceVersion(&version);

#if !defined (NDEBUG)
		VkDebugUtilsMessengerCreateInfoEXT dumci = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,

			.pNext           = NULL,
			.flags           = 0,
			.pfnUserCallback = _gfx_vulkan_message,
			.pUserData       = NULL,
			.messageSeverity =
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
			.messageType =
				VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT
		};
#endif

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

			.flags                   = 0,
			.pApplicationInfo        = &ai,
#if defined (NDEBUG)
			.pNext                   = NULL,
			.enabledLayerCount       = 0,
			.ppEnabledLayerNames     = NULL,
			.enabledExtensionCount   = glfwCount,
			.ppEnabledExtensionNames = glfwExtensions
#else
			.pNext                   = &dumci,
			.enabledLayerCount       = sizeof(layers)/sizeof(char*),
			.ppEnabledLayerNames     = layers,
			.enabledExtensionCount   = count,
			.ppEnabledExtensionNames = extensions
#endif
		};

		VkResult result = _groufix.vk.CreateInstance(
			&ici, NULL, &_groufix.vk.instance);

		if (result != VK_SUCCESS)
		{
			_gfx_vulkan_log(result);

#if !defined (NDEBUG)
			gfx_log_warn("Perhaps you do not have the Vulkan SDK installed?"
			             " To build without needing the SDK, run "
			             "`make clean` then build with DEBUG=OFF."
			             " Or download the Vulkan SDK from `https://vulkan.lunarg.com/sdk/home`.");
#endif

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
#if !defined (NDEBUG)
		_GFX_GET_INSTANCE_PROC_ADDR(CreateDebugUtilsMessengerEXT);
		_GFX_GET_INSTANCE_PROC_ADDR(DestroyDebugUtilsMessengerEXT);
#endif
		_GFX_GET_INSTANCE_PROC_ADDR(CreateDevice);
		_GFX_GET_INSTANCE_PROC_ADDR(DestroySurfaceKHR);
		_GFX_GET_INSTANCE_PROC_ADDR(EnumeratePhysicalDeviceGroups);
		_GFX_GET_INSTANCE_PROC_ADDR(EnumeratePhysicalDevices);
		_GFX_GET_INSTANCE_PROC_ADDR(GetDeviceProcAddr);
		_GFX_GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceProperties);
		_GFX_GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceQueueFamilyProperties);
		_GFX_GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfaceCapabilitiesKHR);
		_GFX_GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfaceFormatsKHR);
		_GFX_GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfacePresentModesKHR);
		_GFX_GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfaceSupportKHR);


#if !defined (NDEBUG)
		// Register the Vulkan debug messenger callback.
		result = _groufix.vk.CreateDebugUtilsMessengerEXT(
			_groufix.vk.instance, &dumci, NULL, &_groufix.vk.messenger);

		if (result != VK_SUCCESS)
		{
			_gfx_vulkan_log(result);
			goto clean;
		}
#endif

		return 1;
	}

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

	// Destroy the debug messenger and Vulkan instance.
#if !defined (NDEBUG)
	_groufix.vk.DestroyDebugUtilsMessengerEXT(
		_groufix.vk.instance, _groufix.vk.messenger, NULL);
#endif
	_groufix.vk.DestroyInstance(
		_groufix.vk.instance, NULL);

	// Signal that termination is done.
	_groufix.vk.instance = NULL;
}
