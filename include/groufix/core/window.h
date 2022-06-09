/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_WINDOW_H
#define GFX_CORE_WINDOW_H

#include "groufix/core/device.h"
#include "groufix/core/keys.h"
#include "groufix/def.h"


/**
 * Window configuration flags.
 */
typedef enum GFXWindowFlags
{
	GFX_WINDOW_NONE          = 0x0000,
	GFX_WINDOW_HIDDEN        = 0x0001, // Overrules all.
	GFX_WINDOW_BORDERLESS    = 0x0002,
	GFX_WINDOW_FOCUS         = 0x0004, // One-time action.
	GFX_WINDOW_MAXIMIZE      = 0x0008, // One-time action.
	GFX_WINDOW_RESIZABLE     = 0x0010,
	GFX_WINDOW_CAPTURE_MOUSE = 0x0020, // Implies GFX_WINDOW_HIDE_MOUSE.
	GFX_WINDOW_HIDE_MOUSE    = 0x0040,
	GFX_WINDOW_DOUBLE_BUFFER = 0x0080,
	GFX_WINDOW_TRIPLE_BUFFER = 0x0100  // Overrules GFX_WINDOW_DOUBLE_BUFFER.

} GFXWindowFlags;

GFX_BIT_FIELD(GFXWindowFlags)


/**
 * Monitor/Window video mode.
 */
typedef struct GFXVideoMode
{
	uint32_t     width;
	uint32_t     height;
	unsigned int refresh;

} GFXVideoMode;


/**
 * Monitor definition.
 */
typedef struct GFXMonitor
{
	// User pointer, can be used for any purpose.
	// Defaults to NULL.
	void*       ptr;
	const char* name; // Read-only.

} GFXMonitor;


/**
 * Window definition.
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
		void (*blur    )(struct GFXWindow*);
		void (*maximize)(struct GFXWindow*);
		void (*minimize)(struct GFXWindow*);
		void (*restore )(struct GFXWindow*);
		void (*move    )(struct GFXWindow*, int32_t x, int32_t y);
		void (*resize  )(struct GFXWindow*, uint32_t width, uint32_t height);

		// Keyboard events.
		struct
		{
			void (*press  )(struct GFXWindow*, GFXKey, int scan, GFXModifier);
			void (*release)(struct GFXWindow*, GFXKey, int scan, GFXModifier);
			void (*repeat )(struct GFXWindow*, GFXKey, int scan, GFXModifier);
			void (*text   )(struct GFXWindow*, uint32_t codepoint);

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
 * Monitor handling.
 ****************************/

/**
 * Sets the configuration change event callback.
 * The callback takes the monitor in question and a zero or non-zero value,
 * zero if the monitor is disconnected, non-zero if it is connected.
 * @param event NULL to disable the event callback.
 */
GFX_API void gfx_monitor_event_set(void (*event)(GFXMonitor*, bool));

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
 * Retrieves the number of video modes available for a monitor.
 * @param monitor Cannot be NULL.
 */
GFX_API size_t gfx_monitor_get_num_modes(GFXMonitor* monitor);

/**
 * Retrieves a video mode of a monitor.
 * @param monitor Cannot be NULL.
 * @param index   Must be < gfx_monitor_get_num_modes(monitor).
 */
GFX_API GFXVideoMode gfx_monitor_get_mode(GFXMonitor* monitor, size_t index);

/**
 * Retrieves the current video mode of a monitor.
 * @param monitor Cannot be NULL.
 */
GFX_API GFXVideoMode gfx_monitor_get_current_mode(GFXMonitor* monitor);


/****************************
 * Window handling.
 ****************************/

/**
 * Creates a window.
 * @param device  NULL is equivalent to gfx_get_primary_device().
 * @param monitor NULL for windowed mode, fullscreen monitor otherwise.
 * @param mode    Width and height must be > 0.
 * @param title   Cannot be NULL.
 * @return NULL on failure.
 *
 * mode.refresh is ignored if monitor is set to NULL.
 */
GFX_API GFXWindow* gfx_create_window(GFXWindowFlags flags, GFXDevice* device,
                                     GFXMonitor* monitor, GFXVideoMode mode,
                                     const char* title);

/**
 * Destroys a window.
 * Must NOT be called from within a window event.
 */
GFX_API void gfx_destroy_window(GFXWindow* window);

/**
 * Retrieves the current flags of a window.
 * @param window Cannot be NULL.
 * @return Window's flags minus any one-time action bits.
 */
GFX_API GFXWindowFlags gfx_window_get_flags(GFXWindow* window);

/**
 * Sets new window flags.
 * @param window Cannot be NULL.
 *
 * If the window is in fullscreen and GFX_WINDOW_HIDDEN is set,
 * the window will exit fullscreen first.
 */
GFX_API void gfx_window_set_flags(GFXWindow* window, GFXWindowFlags flags);

/**
 * Retrieves the monitor the window is fullscreened to.
 * @param window Cannot be NULL.
 * @return NULL if the window is not assigned to a monitor.
 */
GFX_API GFXMonitor* gfx_window_get_monitor(GFXWindow* window);

/**
 * Sets the monitor to fullscreen to.
 * @param window  Cannot be NULL.
 * @param monitor NULL for windowed mode, fullscreen monitor otherwise.
 * @param mode    Width and height must be > 0.
 *
 * mode.refresh is ignored if monitor is set to NULL.
 * If the window is hidden and monitor is not NULL, this will unhide it.
 */
GFX_API void gfx_window_set_monitor(GFXWindow* window, GFXMonitor* monitor,
                                    GFXVideoMode mode);

/**
 * Retrieves the video mode of a window.
 * @param window Cannot be NULL.
 *
 * Returned refresh is set to 0 if window is not assigned to a monitor.
 */
GFX_API GFXVideoMode gfx_window_get_video(GFXWindow* window);

/**
 * Sets the video mode of a window.
 * @param window Cannot be NULL.
 * @param mode   Width and height must be > 0.
 *
 * mode.refresh is ignored if window is not assigned to a monitor.
 * If the window is hidden, this will do nothing.
 */
GFX_API void gfx_window_set_video(GFXWindow* window, GFXVideoMode mode);

/**
 * Sets a new window title.
 * @param window Cannot be NULL.
 * @param title  Cannot be NULL.
 */
GFX_API void gfx_window_set_title(GFXWindow* window, const char* title);

/**
 * Retrieves whether the close flag is set.
 * This flag is set by either the window manager or gfx_window_set_close.
 * @param window Cannot be NULL.
 */
GFX_API bool gfx_window_should_close(GFXWindow* window);

/**
 * Explicitally set the close flag of a window.
 * This is the only way to tell a window to close from within a window event.
 * @param window Cannot be NULL.
 */
GFX_API void gfx_window_set_close(GFXWindow* window, bool close);

/**
 * Focusses the window, bringing it to the front and sets input focus.
 * Does nothing if the window is hidden or minimized.
 * @param window Cannot be NULL.
 *
 * If the window is hidden, this will do nothing.
 */
GFX_API void gfx_window_focus(GFXWindow* window);

/**
 * Maximizes the window.
 * @param window Cannot be NULL.
 *
 * If the window is hidden, this will do nothing.
 */
GFX_API void gfx_window_maximize(GFXWindow* window);

/**
 * Minimizes the window.
 * @param window Cannot be NULL.
 *
 * If the window is hidden, this will do nothing.
 */
GFX_API void gfx_window_minimize(GFXWindow* window);

/**
 * Restores the window from maximization or minimization.
 * @param window Cannot be NULL.
 *
 * If the window is hidden, this will do nothing.
 */
GFX_API void gfx_window_restore(GFXWindow* window);


#endif
