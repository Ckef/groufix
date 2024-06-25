/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GROUFIX_H
#define GROUFIX_H

#include "groufix/core/deps.h"
#include "groufix/core/device.h"
#include "groufix/core/env.h"
#include "groufix/core/formats.h"
#include "groufix/core/heap.h"
#include "groufix/core/keys.h"
#include "groufix/core/log.h"
#include "groufix/core/refs.h"
#include "groufix/core/renderer.h"
#include "groufix/core/shader.h"
#include "groufix/core/window.h"
#include "groufix/def.h"


/**
 * Initializes the engine, attaching the calling thread as the main thread.
 * This call MUST be made before any groufix calls can be made.
 * @return Non-zero on success.
 */
GFX_API bool gfx_init(void);

/**
 * Terminates the engine, detaching the calling thread in the process.
 * MUST be called by the same thread that called gfx_init (the main thread).
 */
GFX_API void gfx_terminate(void);

/**
 * Initializes the calling thread for groufix use.
 * This call MUST be made on the calling thread before any other groufix calls,
 * the main thread (the one that called gfx_init) is the only exception.
 * @return Non-zero on success.
 */
GFX_API bool gfx_attach(void);

/**
 * Detaches the thread from groufix, cleaning up any thread-local data.
 * All attached threads (except main) MUST call this before gfx_terminate.
 */
GFX_API void gfx_detach(void);

/**
 * Polls all window manager events and calls the window and monitor callbacks.
 * Must be called after gfx_init has succesfully returned.
 * Must be called from the main thread.
 */
GFX_API void gfx_poll_events(void);

/**
 * Puts the main thread to sleep until at least one event is available,
 * then calls the window and monitor callbacks.
 * Must be called after gfx_init has succesfully returned.
 * Must be called from the main thread.
 */
GFX_API void gfx_wait_events(void);

/**
 * Wakes the main thread if it is waiting on events through gfx_wait_events().
 * Must be called after gfx_init has succesfully returned.
 * Can be called from any thread.
 */
GFX_API void gfx_wake(void);


#endif
