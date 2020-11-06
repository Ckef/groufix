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


#endif
