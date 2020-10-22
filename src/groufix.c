/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "GLFW/glfw3.h"


/****************************/
int gfx_init(void)
{
	if (!glfwInit())
		return 0;

	return 1;
}

/****************************/
void gfx_terminate(void)
{
	glfwTerminate();
}
