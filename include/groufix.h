/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GROUFIX_H
#define GROUFIX_H

#include "groufix/core/log.h"
#include "groufix/core/window.h"
#include "groufix/def.h"


/**
 * Initializes the engine, attaching the calling thread as the main thread.
 * This call must be made before any groufix calls can be made.
 * @return Non-zero on success.
 */
GFX_API int gfx_init(void);

/**
 * Terminates the engine, detaching the calling thread in the process.
 * Must be called by the same thread that called gfx_init.
 */
GFX_API void gfx_terminate(void);

/**
 * Initializes the calling thread for groufix use.
 * Must be called after gfx_init has succesfully returned.
 * @return Non-zero on success.
 */
GFX_API int gfx_attach(void);

/**
 * Detaches the thread from groufix, cleaning up any thread-local data.
 * All attached threads (except main) need to call this before gfx_terminate.
 */
GFX_API void gfx_detach(void);


#endif
