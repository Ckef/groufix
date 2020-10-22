/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#define GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"


/****************************/
int gfx_init(void)
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
void gfx_terminate(void)
{
	glfwTerminate();
}
