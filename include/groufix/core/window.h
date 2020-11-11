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
	void*       ptr;
	const char* name; // Read-only.

} GFXMonitor;


/**
 * Logical window definition.
 * Must be handled on the main thread.
 */
typedef struct GFXWindow
{
	// User pointer, can be used for any purpose.
	// Defaults to NULL.
	void* ptr;

	// Event callbacks.
	struct
	{
		void (*close   )(struct GFXWindow*);
		void (*drop    )(struct GFXWindow*, size_t count, const char** paths);
		void (*focus   )(struct GFXWindow*);
		void (*maximize)(struct GFXWindow*);
		void (*minimize)(struct GFXWindow*);
		void (*move    )(struct GFXWindow*, int x, int y);
		void (*resize  )(struct GFXWindow*, size_t width, size_t height);

		// Keyboard events.
		struct
		{
			void (*press  )(struct GFXWindow*, GFXKey, int scan, GFXModifier);
			void (*release)(struct GFXWindow*, GFXKey, int scan, GFXModifier);
			void (*repeat )(struct GFXWindow*, GFXKey, int scan, GFXModifier);
			void (*text   )(struct GFXWindow*, char32_t codepoint);

		} key;

		// Mouse events.
		struct
		{
			void (*enter  )(struct GFXWindow*);
			void (*leave  )(struct GFXWindow*);
			void (*move   )(struct GFXWindow*, double x, double y);
			void (*press  )(struct GFXWindow*, GFXMouseButton, GFXModifier);
			void (*release)(struct GFXWindow*, GFXMouseButton, GFXModifier);
			void (*scroll )(struct GFXWindow*, double x, double y);

		} mouse;

	} events;

} GFXWindow;


/****************************
 * Logical monitor.
 ****************************/

/**
 * Retrieves the number of currently connected monitors.
 * @return 0 if no monitors were found.
 */
GFX_API size_t gfx_get_num_monitors(void);

/**
 * Retrieves a currently connected monitor.
 * The primary monitor is always stored at index 0.
 * @param index Must be < gfx_get_num_monitors().
 */
GFX_API GFXMonitor* gfx_get_monitor(size_t index);

/**
 * Retrieves the primary (user's preferred) monitor.
 * This is equivalent to gfx_get_monitor(0).
 */
GFX_API GFXMonitor* gfx_get_primary_monitor(void);

/**
 * Sets the configuration change event callback.
 * The callback takes the monitor in question and a zero or non-zero value,
 * zero if the monitor is disconnected, non-zero if it is connected.
 * @param event NULL to disable the event callback.
 */
GFX_API void gfx_set_monitor_events(void (*event)(GFXMonitor*, int));


/****************************
 * Logical window.
 ****************************/

/**
 * Creates a logical window.
 * @param width  Must be > 0.
 * @param height Must be > 0.
 * @param title  Cannot be NULL.
 * @return NULL on failure.
 */
GFX_API GFXWindow* gfx_create_window(size_t width, size_t height,
                                     const char* title, GFXMonitor* monitor);

/**
 * Destroys a logical window.
 * @param window Cannot be NULL.
 */
GFX_API void gfx_destroy_window(GFXWindow* window);


#endif
