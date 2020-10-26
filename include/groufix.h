/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GROUFIX_H
#define GROUFIX_H

#include "groufix/def.h"


/**
 * Initializes the engine.
 * @return Non-zero on success.
 */
GFX_API int gfx_init(void);

/**
 * Terminates the engine.
 */
GFX_API void gfx_terminate(void);


#endif
