/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"


/****************************/
GFX_API GFXDependency* gfx_create_dep(GFXDevice* device)
{
	return NULL;
}

/****************************/
GFX_API void gfx_destroy_dep(GFXDependency* dep)
{
	if (dep == NULL)
		return;
}
