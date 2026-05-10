/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <string.h>


#define GFX_GET_INSTANCE_PROC_ADDR_(pName) \
	do { \
		groufix_.vk.pName = (PFN_vk##pName)glfwGetInstanceProcAddress( \
			groufix_.vk.instance, "vk"#pName); \
		if (groufix_.vk.pName == NULL) { \
			gfx_log_error("Could not load vk"#pName"."); \
			goto clean; \
		} \
	} while (0)


#if !defined (NDEBUG)

/****************************
 * Callback for Vulkan debug messages.
 */
static VkBool32 VKAPI_PTR gfx_vulkan_message_(
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
const char* gfx_vulkan_result_string_(VkResult result)
{
	switch (result)
	{
	case VK_SUCCESS:
		return "Success.";

	case VK_NOT_READY:
		return "A fence or query has not yet completed.";

	case VK_TIMEOUT:
		return "A wait operation has not completed in the specified time.";

	case VK_EVENT_SET:
		return "An event is signaled.";

	case VK_EVENT_RESET:
		return "An event is unsignaled.";

	case VK_INCOMPLETE:
		return "A return array was too small for the result.";

	case VK_ERROR_OUT_OF_HOST_MEMORY:
		return "A host memory allocation has failed.";

	case VK_ERROR_OUT_OF_DEVICE_MEMORY:
		return "A device memory allocation has failed.";

	case VK_ERROR_INITIALIZATION_FAILED:
		return "initialization of an object could not be completed for "
			"implementation-specific reasons.";

	case VK_ERROR_DEVICE_LOST:
		return "A logical or physical device has been lost.";

	case VK_ERROR_MEMORY_MAP_FAILED:
		return "Mapping of a memory object has failed.";

	case VK_ERROR_LAYER_NOT_PRESENT:
		return "A requested layer is not present or could not be loaded.";

	case VK_ERROR_EXTENSION_NOT_PRESENT:
		return "A requested extension is not supported.";

	case VK_ERROR_FEATURE_NOT_PRESENT:
		return "A requested feature is not supported.";

	case VK_ERROR_INCOMPATIBLE_DRIVER:
		return "The requested version of Vulkan is not supported by the driver "
			"or is otherwise incompatible for implementation-specific reasons.";

	case VK_ERROR_TOO_MANY_OBJECTS:
		return "Too many objects of a type have already been created.";

	case VK_ERROR_FORMAT_NOT_SUPPORTED:
		return "A requested format is not supported on this device.";

	case VK_ERROR_FRAGMENTED_POOL:
		return "A pool allocation has failed due to fragmentation of the pool's memory.";

	case VK_ERROR_OUT_OF_POOL_MEMORY:
		return "A pool memory allocation has failed.";

	case VK_ERROR_INVALID_EXTERNAL_HANDLE:
		return "An external handle is not a valid handle of the specified type.";

	case VK_ERROR_FRAGMENTATION:
		return "A descriptor pool creation has failed due to fragmentation.";

	case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
		return "A buffer creation or memory allocation failed because the "
			"requested address is not available.";

	case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
		return "The requested window is already in use by Vulkan or another "
			"Vulkan API in a manner which prevents it from being used again.";

	case VK_ERROR_SURFACE_LOST_KHR:
		return "A surface is no longer available.";

	default:
		return "Unknown error.";
	}
}

/****************************/
bool gfx_vulkan_init_(void)
{
	assert(groufix_.vk.instance == NULL);

	// Set this to NULL so we don't accidentally call garbage on cleanup.
	groufix_.vk.DestroyInstance = NULL;

	// So first things first, we need to create a Vulkan instance.
	// For this we load the global level vkCreateInstance function and
	// we tell it what extensions we need, these include GLFW extensions.
	// Also, load _all_ global level Vulkan functions here.
	GFX_GET_INSTANCE_PROC_ADDR_(CreateInstance);
	GFX_GET_INSTANCE_PROC_ADDR_(EnumerateInstanceVersion);

	uint32_t glfwCount;
	const char** glfwExtensions =
		glfwGetRequiredInstanceExtensions(&glfwCount);

	if (glfwExtensions == NULL)
		goto clean;

	const char* extraExtensions[] = {
		// VK_EXT_debug_utils so we can log Vulkan debug messages.
#if !defined (NDEBUG)
		"VK_EXT_debug_utils",
#endif
		// VK_KHR_portability_enumeration for e.g. MoltenVK.
#if defined (GFX_USE_VK_SUBSET_DEVICES)
		"VK_KHR_portability_enumeration",
#endif
		NULL // Cannot have empty arrays.
	};

	// We use a scope here so the goto above is allowed.
	{
		const uint32_t extraCount = sizeof(extraExtensions)/sizeof(char*) - 1;
		const uint32_t extensionCount = glfwCount + extraCount;
		const char* extensions[GFX_MAX(1, extensionCount)];
		memcpy(extensions, glfwExtensions, sizeof(char*) * glfwCount);
		memcpy(extensions + glfwCount, extraExtensions, sizeof(char*) * extraCount);

		// Enable VK_LAYER_KHRONOS_validation if debug.
#if !defined (NDEBUG)
		const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
#endif

		// Ok now go create a Vulkan instance.
		// But check for the supported version first.
		uint32_t version = 0;
		groufix_.vk.EnumerateInstanceVersion(&version);

		if (version < GFX_VK_API_VERSION_)
		{
			gfx_log_error("Vulkan instance does not support version %u.%u.%u.",
				(unsigned int)VK_API_VERSION_MAJOR(GFX_VK_API_VERSION_),
				(unsigned int)VK_API_VERSION_MINOR(GFX_VK_API_VERSION_),
				(unsigned int)VK_API_VERSION_PATCH(GFX_VK_API_VERSION_));

			goto clean;
		}

#if !defined (NDEBUG)
		VkDebugUtilsMessengerCreateInfoEXT dumci = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,

			.pNext           = NULL,
			.flags           = 0,
			.pfnUserCallback = gfx_vulkan_message_,
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

			.pApplicationInfo        = &ai,
#if defined (NDEBUG)
			.pNext                   = NULL,
			.enabledLayerCount       = 0,
			.ppEnabledLayerNames     = NULL,
#else
			.pNext                   = &dumci,
			.enabledLayerCount       = sizeof(layers)/sizeof(char*),
			.ppEnabledLayerNames     = layers,
#endif
			.enabledExtensionCount   = extensionCount,
			.ppEnabledExtensionNames = extensions,

#if defined (GFX_USE_VK_SUBSET_DEVICES)
			.flags = 0x00000001 // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
#else
			.flags = 0
#endif
		};

		bool res = 1;
		GFX_VK_CHECK_(groufix_.vk.CreateInstance(
			&ici, NULL, &groufix_.vk.instance), res = 0);

		if (!res)
		{
#if !defined (NDEBUG)
			gfx_log_warn(
				"Perhaps you do not have the Vulkan SDK installed?\n"
				"    To build without needing the SDK, run `make clean` then build with DEBUG=OFF.\n"
				"    Or download the Vulkan SDK from https://vulkan.lunarg.com/sdk/home\n");
#endif

			goto clean;
		}

#if !defined (NDEBUG)
		// Traditional moment to celebrate.
		GFXBufWriter* logger = gfx_logger_debug();
		if (logger != NULL)
		{
			gfx_io_writef(logger,
				"Vulkan instance created:\n"
				"    API version: v%u.%u.%u\n"
				"    Enabled extensions: %s\n",
				(unsigned int)VK_API_VERSION_MAJOR(version),
				(unsigned int)VK_API_VERSION_MINOR(version),
				(unsigned int)VK_API_VERSION_PATCH(version),
				extensionCount > 0 ? "" : "None.");

			for (uint32_t e = 0; e < extensionCount; ++e)
				gfx_io_writef(logger, "        %s\n", extensions[e]);

			gfx_logger_end(logger);
		}
#endif


		// Now load all instance level Vulkan functions.
		// Load vkDestroyInstance first so we can clean properly.
		GFX_GET_INSTANCE_PROC_ADDR_(DestroyInstance);
#if !defined (NDEBUG)
		GFX_GET_INSTANCE_PROC_ADDR_(CreateDebugUtilsMessengerEXT);
		GFX_GET_INSTANCE_PROC_ADDR_(DestroyDebugUtilsMessengerEXT);
#endif
		GFX_GET_INSTANCE_PROC_ADDR_(CreateDevice);
		GFX_GET_INSTANCE_PROC_ADDR_(DestroySurfaceKHR);
#if defined (GFX_USE_VK_SUBSET_DEVICES)
		GFX_GET_INSTANCE_PROC_ADDR_(EnumerateDeviceExtensionProperties);
#endif
		GFX_GET_INSTANCE_PROC_ADDR_(EnumeratePhysicalDeviceGroups);
		GFX_GET_INSTANCE_PROC_ADDR_(EnumeratePhysicalDevices);
		GFX_GET_INSTANCE_PROC_ADDR_(GetDeviceProcAddr);
		GFX_GET_INSTANCE_PROC_ADDR_(GetPhysicalDeviceFeatures);
		GFX_GET_INSTANCE_PROC_ADDR_(GetPhysicalDeviceFeatures2);
		GFX_GET_INSTANCE_PROC_ADDR_(GetPhysicalDeviceFormatProperties);
		GFX_GET_INSTANCE_PROC_ADDR_(GetPhysicalDeviceMemoryProperties);
		GFX_GET_INSTANCE_PROC_ADDR_(GetPhysicalDeviceProperties);
		GFX_GET_INSTANCE_PROC_ADDR_(GetPhysicalDeviceProperties2);
		GFX_GET_INSTANCE_PROC_ADDR_(GetPhysicalDeviceQueueFamilyProperties);
		GFX_GET_INSTANCE_PROC_ADDR_(GetPhysicalDeviceSurfaceCapabilitiesKHR);
		GFX_GET_INSTANCE_PROC_ADDR_(GetPhysicalDeviceSurfaceFormatsKHR);
		GFX_GET_INSTANCE_PROC_ADDR_(GetPhysicalDeviceSurfacePresentModesKHR);
		GFX_GET_INSTANCE_PROC_ADDR_(GetPhysicalDeviceSurfaceSupportKHR);


#if !defined (NDEBUG)
		// Register the Vulkan debug messenger callback.
		GFX_VK_CHECK_(groufix_.vk.CreateDebugUtilsMessengerEXT(
			groufix_.vk.instance, &dumci, NULL, &groufix_.vk.messenger), goto clean);
#endif

		return 1;
	}


	// Cleanup on failure.
clean:
	gfx_log_error("Could not create or initialize a Vulkan instance.");

	// If vkDestroyInstance is available, properly clean the instance.
	if (groufix_.vk.DestroyInstance != NULL)
		groufix_.vk.DestroyInstance(groufix_.vk.instance, NULL);

	groufix_.vk.instance = NULL;

	return 0;
}

/****************************/
void gfx_vulkan_terminate_(void)
{
	// No assert, this function is a no-op if Vulkan is not initialized.
	if (groufix_.vk.instance == NULL)
		return;

	// Destroy the debug messenger and Vulkan instance.
#if !defined (NDEBUG)
	groufix_.vk.DestroyDebugUtilsMessengerEXT(
		groufix_.vk.instance, groufix_.vk.messenger, NULL);
#endif
	groufix_.vk.DestroyInstance(
		groufix_.vk.instance, NULL);

	// Signal that termination is done.
	groufix_.vk.instance = NULL;
}
