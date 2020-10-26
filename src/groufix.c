/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/def.h"

#define GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"


/****************************/
GFX_API int gfx_init(void)
{
	if (!glfwInit())
		return 0;

	if (!glfwVulkanSupported())
	{
		glfwTerminate();
		return 0;
	}

	return 1;
}

/****************************/
GFX_API void gfx_terminate(void)
{
	glfwTerminate();
}
