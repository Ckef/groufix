/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_WINDOW_H
#define GFX_CORE_WINDOW_H

#include "groufix/core/keys.h"
#include "groufix/def.h"
#include <stddef.h>
#include <uchar.h>


/**
 * Logical monitor definition.
 * Must be handled on the main thread.
 */
typedef struct GFXMonitor
{
	// User pointer, can be used for any purpose.
	// Defaults to NULL.
	void* ptr;

} GFXMonitor;


/**
 * Forward declaration of logical window for event callbacks.
 */
typedef struct GFXWindow GFXWindow;

/**
 * Logical window definition.
 * Must be handled on the main thread.
 */
struct GFXWindow
{
	// User pointer, can be used for any purpose.
	// Defaults to NULL.
	void* ptr;

	// Event callbacks.
	struct
	{
		void (*close   )(GFXWindow*);
		void (*drop    )(GFXWindow*, size_t count, const char** paths);
		void (*focus   )(GFXWindow*);
		void (*maximize)(GFXWindow*);
		void (*minimize)(GFXWindow*);
		void (*move    )(GFXWindow*, int x, int y);
		void (*resize  )(GFXWindow*, size_t width, size_t height);

		// Keyboard events.
		struct
		{
			void (*press  )(GFXWindow*, GFXKey, int scan, GFXModifier);
			void (*release)(GFXWindow*, GFXKey, int scan, GFXModifier);
			void (*repeat )(GFXWindow*, GFXKey, int scan, GFXModifier);
			void (*text   )(GFXWindow*, char32_t codepoint);

		} key;

		// Mouse events.
		struct
		{
			void (*enter  )(GFXWindow*);
			void (*leave  )(GFXWindow*);
			void (*move   )(GFXWindow*, double x, double y);
			void (*press  )(GFXWindow*, GFXMouseButton, GFXModifier);
			void (*release)(GFXWindow*, GFXMouseButton, GFXModifier);
			void (*scroll )(GFXWindow*, double x, double y);

		} mouse;

	} events;
};


/****************************
 * Logical monitor.
 ****************************/

/**
 * Retrieves the primary (user's preferred) monitor.
 * @return NULL if no monitors were found.
 */
GFX_API GFXMonitor* gfx_get_primary_monitor(void);

/**
 * Retrieves all currently connected monitors.
 * The primary monitor is always first.
 * @param count Cannot be NULL.
 * @return NULL if no monitors were found.
 *
 * The returned array is freed by groufix itself, it is a valid pointer
 * until the monitor configuration changes or the engine is terminated.
 */
GFX_API GFXMonitor** gfx_get_monitors(size_t* count);

/**
 * Sets the configuration change event callback.
 * The callback takes the monitor in question and a zero or non-zero value,
 * zero if the monitor is disconnected, non-zero if it is connected.
 * @param event NULL to disable the event callback.
 */
GFX_API void gfx_set_monitor_events(void (*event)(GFXMonitor*, int));


#endif
