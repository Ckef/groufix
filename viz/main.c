/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include <groufix.h>
#include <stdlib.h>


/****************************
 * grouviz entry point.
 */
int main(void)
{
	if (!gfx_init())
		exit(EXIT_FAILURE);

	// TODO: Do things :)

	gfx_terminate();

	exit(EXIT_SUCCESS);
}
