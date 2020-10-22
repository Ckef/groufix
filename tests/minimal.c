/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include <groufix.h>
#include <stdio.h>
#include <stdlib.h>


/****************************/
int main()
{
	// Initialize
	if (!gfx_init())
	{
		puts("Failure!");
		exit(EXIT_FAILURE);
	}

	// Terminate
	gfx_terminate();

	puts("Success!");
	exit(EXIT_SUCCESS);
}
