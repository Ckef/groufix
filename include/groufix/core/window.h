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
 * Physical device definition (e.g. a GPU).
 */
typedef struct GFXDevice GFXDevice;


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
 * Physical device (e.g. GPU).
 ****************************/

/**
 * Retrieves all initialized devices.
 * @param count Cannot be NULL.
 * @return NULL if no devices were found.
 *
 * The returned array is freed by groufix itself, it is a valid pointer
 * until the engine is terminated.
 */
GFX_API GFXDevice* gfx_get_devices(size_t* count);


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
