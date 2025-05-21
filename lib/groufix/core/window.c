/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>


// Retrieve the GFXListNode* from a public user data pointer.
#define _GFX_GET_NODE(data) \
	(GFXListNode*)((char*)data - \
		GFX_ALIGN_UP(sizeof(GFXWindowEvents), alignof(max_align_t)) - \
		GFX_ALIGN_UP(sizeof(GFXListNode), alignof(GFXWindowEvents)))

// Retrieve the GFXWindowEvents* from a GFXListNode*.
#define _GFX_GET_EVENTS(node) \
	(GFXWindowEvents*)((char*)node + \
		GFX_ALIGN_UP(sizeof(GFXListNode), alignof(GFXWindowEvents)))

// Retrieve the user data from a GFXListNode*.
#define _GFX_GET_DATA(node) \
	(void*)((char*)_GFX_GET_EVENTS(node) + \
		GFX_ALIGN_UP(sizeof(GFXWindowEvents), alignof(max_align_t)))


// Executes all events on the stack, from top to bottom.
#define _GFX_CALL_STACK(handle, cb, ...) \
	do { \
		GFXWindow* window = glfwGetWindowUserPointer(handle); \
		bool blocked = 0; \
		for ( \
			GFXListNode* node = ((_GFXWindow*)window)->events.tail; \
			node != NULL && !blocked; \
			node = node->prev) \
		{ \
			GFXWindowEvents* events = _GFX_GET_EVENTS(node); \
			if (events->cb != NULL) \
				blocked = events->cb(__VA_ARGS__, _GFX_GET_DATA(node)); \
		} \
		if (!blocked && window->events.cb != NULL) \
			window->events.cb(__VA_ARGS__, NULL); \
	} while (0)


/****************************
 * GLFW window close callback.
 */
static void _gfx_glfw_window_close(GLFWwindow* handle)
{
	_GFX_CALL_STACK(handle, close, window);
}

/****************************
 * GLFW drop callback.
 */
static void _gfx_glfw_drop(GLFWwindow* handle, int count, const char** paths)
{
	_GFX_CALL_STACK(handle, drop, window, (size_t)count, paths);
}

/****************************
 * GLFW window focus callback.
 */
static void _gfx_glfw_window_focus(GLFWwindow* handle, int focused)
{
	if (focused)
		_GFX_CALL_STACK(handle, focus, window);
	else
		_GFX_CALL_STACK(handle, blur, window);
}

/****************************
 * GLFW window maximize callback.
 */
static void _gfx_glfw_window_maximize(GLFWwindow* handle, int maximized)
{
	if (maximized)
		_GFX_CALL_STACK(handle, maximize, window);
	else
		_GFX_CALL_STACK(handle, restore, window);
}

/****************************
 * GLFW window iconify callback.
 */
static void _gfx_glfw_window_iconify(GLFWwindow* handle, int iconified)
{
	if (iconified)
		_GFX_CALL_STACK(handle, minimize, window);
	else
		_GFX_CALL_STACK(handle, restore, window);
}

/****************************
 * GLFW window pos callback.
 */
static void _gfx_glfw_window_pos(GLFWwindow* handle, int x, int y)
{
	_GFX_CALL_STACK(handle, move, window, (int32_t)x, (int32_t)y);
}

/****************************
 * GLFW window size callback.
 */
static void _gfx_glfw_window_size(GLFWwindow* handle, int width, int height)
{
	_GFX_CALL_STACK(handle, resize, window, (uint32_t)width, (uint32_t)height);
}

/****************************
 * GLFW key callback.
 */
static void _gfx_glfw_key(GLFWwindow* handle,
                          int key, int scancode, int action, int mods)
{
	switch (action)
	{
	case GLFW_PRESS:
		_GFX_CALL_STACK(handle, key.press,
			window, (GFXKey)key, scancode, (GFXModifier)mods);
		break;
	case GLFW_RELEASE:
		_GFX_CALL_STACK(handle, key.release,
			window, (GFXKey)key, scancode, (GFXModifier)mods);
		break;
	case GLFW_REPEAT:
		_GFX_CALL_STACK(handle, key.repeat,
			window, (GFXKey)key, scancode, (GFXModifier)mods);
		break;
	}
}

/****************************
 * GLFW char callback.
 */
static void _gfx_glfw_char(GLFWwindow* handle, unsigned int codepoint)
{
	_GFX_CALL_STACK(handle, key.text, window, (uint32_t)codepoint);
}

/****************************
 * GLFW cursor enter callback.
 */
static void _gfx_glfw_cursor_enter(GLFWwindow* handle, int entered)
{
	if (entered)
		_GFX_CALL_STACK(handle, mouse.enter, window);
	else
		_GFX_CALL_STACK(handle, mouse.leave, window);
}

/****************************
 * GLFW cursor position callback.
 */
static void _gfx_glfw_cursor_pos(GLFWwindow* handle, double x, double y)
{
	_GFX_CALL_STACK(handle, mouse.move, window, x, y);
}

/****************************
 * GLFW mouse button callback.
 */
static void _gfx_glfw_mouse_button(GLFWwindow* handle,
                                   int button, int action, int mods)
{
	switch (action)
	{
	case GLFW_PRESS:
		_GFX_CALL_STACK(handle, mouse.press,
			window, (GFXMouseButton)button, (GFXModifier)mods);
		break;
	case GLFW_RELEASE:
		_GFX_CALL_STACK(handle, mouse.release,
			window, (GFXMouseButton)button, (GFXModifier)mods);
		break;
	}
}

/****************************
 * GLFW scroll callback.
 */
static void _gfx_glfw_scroll(GLFWwindow* handle, double x, double y)
{
	_GFX_CALL_STACK(handle, mouse.scroll, window, x, y);
}

/****************************
 * GLFW framebuffer size callback.
 */
static void _gfx_glfw_framebuffer_size(GLFWwindow* handle,
                                       int width, int height)
{
	_GFXWindow* window = glfwGetWindowUserPointer(handle);

	// We lock such that setting the size and signaling it has been resized
	// are both in the same atomic operation.
	// We set a proxy size though, not the actually used one,
	// that one gets updated by the thread that uses it.
	_gfx_mutex_lock(&window->frame.lock);

	atomic_store_explicit(&window->frame.recreate, 1, memory_order_relaxed);
	window->frame.rWidth = (uint32_t)width;
	window->frame.rHeight = (uint32_t)height;

	_gfx_mutex_unlock(&window->frame.lock);
}

/****************************
 * Picks and validates queue families with image access and
 * subsequently fills the window->access array.
 * @param window Cannot be NULL.
 * @return Non-zero on success.
 *
 * window->vk.surface must be initialized to a valid Vulkan surface.
 * This can only be called once for each window!
 */
static bool _gfx_window_pick_access(_GFXWindow* window)
{
	assert(window != NULL);

	_GFXContext* context = window->context;

	// Pick the presentation AND graphics queues.
	// The graphics queue will need access to these images.
	uint32_t graphics, present;
	_gfx_pick_family(context, &graphics, VK_QUEUE_GRAPHICS_BIT, 0);
	_gfx_pick_family(context, &present, 0, 1);

	// So we checked presentation support in a surface-agnostic manner during
	// logical device creation, now go check for the given surface.
	// I mean all things sharing this device will pick the same presentation
	// queue, so we might as well preemptively check.
	// What if a queue not chosen for presentation supports this surface
	// I hear you asking.. well.. shutup >:(
	VkBool32 support = VK_FALSE;
	_groufix.vk.GetPhysicalDeviceSurfaceSupportKHR(
		window->device->vk.device, present, window->vk.surface, &support);

	if (support == VK_FALSE)
	{
		gfx_log_error(
			"[ %s ] picked queue set (family) for presentation that does "
			"not support presentation to a surface.",
			window->device->name);

		return 0;
	}

	// Store the chosen families.
	// Make sure to not put in duplicate indices!
	window->access[0] = graphics;
	window->access[1] = (present != graphics) ? present : UINT32_MAX;

	return 1;
}

/****************************/
GFX_API GFXWindow* gfx_create_window(GFXWindowFlags flags, GFXDevice* device,
                                     GFXMonitor* monitor, GFXVideoMode mode,
                                     const char* title)
{
	assert(atomic_load(&_groufix.initialized));
	assert(_groufix.vk.instance != NULL);
	assert(mode.width > 0);
	assert(mode.height > 0);
	assert(title != NULL);

	// Allocate and set a new window.
	// Just set the user pointer and all callbacks to all NULL's.
	_GFXWindow* window = malloc(sizeof(_GFXWindow));
	if (window == NULL) goto clean;

	window->base = (GFXWindow){
		.ptr = NULL,
		.events = {
			.close = NULL,
			.drop = NULL,
			.focus = NULL,
			.blur = NULL,
			.maximize = NULL,
			.minimize = NULL,
			.restore = NULL,
			.move = NULL,
			.resize = NULL,

			.key = {
				.press = NULL,
				.release = NULL,
				.repeat = NULL,
				.text = NULL
			},

			.mouse = {
				.enter = NULL,
				.leave = NULL,
				.move = NULL,
				.press = NULL,
				.release = NULL,
				.scroll = NULL
			}
		}
	};

	// Create a GLFW window.
	glfwDefaultWindowHints();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	glfwWindowHint(GLFW_VISIBLE,
		flags & GFX_WINDOW_HIDDEN ? GLFW_FALSE : GLFW_TRUE);
	glfwWindowHint(GLFW_DECORATED,
		flags & GFX_WINDOW_BORDERLESS ? GLFW_FALSE : GLFW_TRUE);
	glfwWindowHint(GLFW_FOCUSED,
		flags & GFX_WINDOW_FOCUS ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_MAXIMIZED,
		flags & GFX_WINDOW_MAXIMIZE ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_RESIZABLE,
		flags & GFX_WINDOW_RESIZABLE ? GLFW_TRUE : GLFW_FALSE);

	// If entering fullscreen, use the given refresh rate.
	if (monitor != NULL)
		glfwWindowHint(GLFW_REFRESH_RATE,
			mode.refresh == 0 ? GLFW_DONT_CARE : (int)mode.refresh);

	window->handle = glfwCreateWindow(
		(int)mode.width,
		(int)mode.height,
		title,
		(monitor != NULL) ? ((_GFXMonitor*)monitor)->handle : NULL,
		NULL);

	if (window->handle == NULL)
		goto clean;

	// Associate with GLFW using the user pointer.
	glfwSetWindowUserPointer(window->handle, window);

	// Set the input mode for the cursor and caps/num lock.
	const int cursor =
		(flags & GFX_WINDOW_CAPTURE_MOUSE) ? GLFW_CURSOR_DISABLED :
		(flags & GFX_WINDOW_HIDE_MOUSE) ? GLFW_CURSOR_HIDDEN :
		GLFW_CURSOR_NORMAL;

	glfwSetInputMode(window->handle, GLFW_CURSOR, cursor);
	glfwSetInputMode(window->handle, GLFW_LOCK_KEY_MODS, GLFW_TRUE);

	// Use raw mouse position if GFX_WINDOW_CAPTURE_MOUSE is set.
	if (cursor == GLFW_CURSOR_DISABLED && glfwRawMouseMotionSupported())
		glfwSetInputMode(window->handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

	// Register all callbacks.
	glfwSetWindowCloseCallback(
		window->handle, _gfx_glfw_window_close);
	glfwSetDropCallback(
		window->handle, _gfx_glfw_drop);
	glfwSetWindowFocusCallback(
		window->handle, _gfx_glfw_window_focus);
	glfwSetWindowMaximizeCallback(
		window->handle, _gfx_glfw_window_maximize);
	glfwSetWindowIconifyCallback(
		window->handle, _gfx_glfw_window_iconify);
	glfwSetWindowPosCallback(
		window->handle, _gfx_glfw_window_pos);
	glfwSetWindowSizeCallback(
		window->handle, _gfx_glfw_window_size);
	glfwSetKeyCallback(
		window->handle, _gfx_glfw_key);
	glfwSetCharCallback(
		window->handle, _gfx_glfw_char);
	glfwSetCursorEnterCallback(
		window->handle, _gfx_glfw_cursor_enter);
	glfwSetCursorPosCallback(
		window->handle, _gfx_glfw_cursor_pos);
	glfwSetMouseButtonCallback(
		window->handle, _gfx_glfw_mouse_button);
	glfwSetScrollCallback(
		window->handle, _gfx_glfw_scroll);
	glfwSetFramebufferSizeCallback(
		window->handle, _gfx_glfw_framebuffer_size);

	// So we setup everything related to GLFW, now set the frame properties.
	// Initialize signal & lock for swapping and resizing.
	atomic_store_explicit(&window->swap, 0, memory_order_relaxed);

	if (!_gfx_mutex_init(&window->frame.lock))
		goto clean_window;

	// And set the current width/height and such of the framebuffer.
	int width;
	int height;
	glfwGetFramebufferSize(window->handle, &width, &height);

	gfx_vec_init(&window->frame.images, sizeof(VkImage));
	window->frame.format = VK_FORMAT_UNDEFINED;
	window->frame.width = (uint32_t)width;
	window->frame.height = (uint32_t)height;

	atomic_store_explicit(&window->frame.recreate, 0, memory_order_relaxed);
	window->frame.rWidth = (uint32_t)width;
	window->frame.rHeight = (uint32_t)height;
	window->frame.flags = flags;

	// Now we need to somehow connect it to a GPU.
	// So attempt to create a Vulkan surface for the window.
	_GFX_VK_CHECK(
		glfwCreateWindowSurface(
			_groufix.vk.instance, window->handle, NULL, &window->vk.surface),
		goto clean_frame);

	// Get physical device and its associated (Vulkan) context.
	// Unfortunately we cannot clean the context if it's newly created for us,
	// that's why we do stuff dependent on it last.
	_GFX_GET_DEVICE(window->device, device);
	_GFX_GET_CONTEXT(window->context, device, goto clean_surface);

	// Pick a swapchain format, for potential pipeline warmups!
	if (!_gfx_swapchain_format(window))
		goto clean_surface;

	// Pick all the queue families that need image access.
	if (!_gfx_window_pick_access(window))
		goto clean_surface;

	// Make sure to set the swapchain to a NULL handle here so a new one will
	// eventually get created when an image is acquired.
	window->vk.swapchain = VK_NULL_HANDLE;
	window->vk.oldSwapchain = VK_NULL_HANDLE;
	gfx_vec_init(&window->vk.retired, sizeof(VkSwapchainKHR));

	// And lastly the event stack.
	gfx_list_init(&window->events);

	return &window->base;


	// Cleanup on failure.
clean_surface:
	_groufix.vk.DestroySurfaceKHR(
		_groufix.vk.instance, window->vk.surface, NULL);
clean_frame:
	gfx_vec_clear(&window->frame.images);
	_gfx_mutex_clear(&window->frame.lock);
clean_window:
	glfwDestroyWindow(window->handle);
clean:
	gfx_log_error("Could not create a new window.");
	free(window);

	return NULL;
}

/****************************/
GFX_API void gfx_destroy_window(GFXWindow* window)
{
	if (window == NULL)
		return;

	_GFXWindow* win = (_GFXWindow*)window;
	_GFXContext* context = win->context;

	// Purge retired swapchains.
	_gfx_swapchain_purge(win);

	// Destroy the swapchain, surface and the window itself.
	context->vk.DestroySwapchainKHR(
		context->vk.device, win->vk.oldSwapchain, NULL);
	context->vk.DestroySwapchainKHR(
		context->vk.device, win->vk.swapchain, NULL);
	_groufix.vk.DestroySurfaceKHR(
		_groufix.vk.instance, win->vk.surface, NULL);

	gfx_vec_clear(&win->frame.images);
	gfx_vec_clear(&win->vk.retired);
	_gfx_mutex_clear(&win->frame.lock);

	glfwDestroyWindow(win->handle);

	// Free all nodes in the event stack.
	while (win->events.tail != NULL)
	{
		GFXListNode* node = win->events.tail;
		gfx_list_erase(&win->events, node);
		free(node);
	}

	gfx_list_clear(&win->events);
	free(window);
}

/****************************/
GFX_API GFXDevice* gfx_window_get_device(GFXWindow* window)
{
	if (window == NULL)
		return NULL;

	return (GFXDevice*)((_GFXWindow*)window)->device;
}

/****************************/
GFX_API void* gfx_window_push_events(GFXWindow* window, GFXWindowEvents events,
                                     size_t dataSize, const void* data)
{
	assert(window != NULL);

	_GFXWindow* win = (_GFXWindow*)window;

	// Allocate memory for events and some data.
	GFXListNode* node = malloc(
		GFX_ALIGN_UP(sizeof(GFXListNode), alignof(GFXWindowEvents)) +
		GFX_ALIGN_UP(sizeof(GFXWindowEvents), alignof(max_align_t)) +
		GFX_MAX(dataSize, 1)); // Minimum 1 byte, so we have a valid pointer.

	if (node == NULL)
		return NULL;

	// Initialize events & user data.
	*_GFX_GET_EVENTS(node) = events;

	if (dataSize > 0 && data != NULL)
		memcpy(_GFX_GET_DATA(node), data, dataSize);

	// Link itself into the window.
	gfx_list_insert_after(&win->events, node, NULL);

	return _GFX_GET_DATA(node);
}

/****************************/
GFX_API void gfx_window_erase_events(GFXWindow* window, void* data)
{
	assert(window != NULL);
	assert(data != NULL);

	_GFXWindow* win = (_GFXWindow*)window;
	GFXListNode* node = _GFX_GET_NODE(data);

	// Unlink from window & free.
	gfx_list_erase(&win->events, node);
	free(node);
}

/****************************/
GFX_API GFXWindowFlags gfx_window_get_flags(GFXWindow* window)
{
	assert(window != NULL);

	// We don't actually need to use the frame lock.
	// Only the main thread ever writes the flags, every other thread only
	// reads, so this can never result in a race condition.
	// Also we filter out any one-time actions.
	return
		((_GFXWindow*)window)->frame.flags &
		(GFXWindowFlags)~(GFX_WINDOW_FOCUS | GFX_WINDOW_MAXIMIZE);
}

/****************************/
GFX_API void gfx_window_set_flags(GFXWindow* window, GFXWindowFlags flags)
{
	assert(window != NULL);

	_GFXWindow* win = (_GFXWindow*)window;

	// Always hide/unhide at the start, so all other flags act appropriately.
	if (!(flags & GFX_WINDOW_HIDDEN))
		glfwShowWindow(win->handle);
	else
	{
		// If fullscreen, exit fullscreen before hiding.
		GLFWmonitor* monitor = glfwGetWindowMonitor(win->handle);
		if (monitor != NULL)
		{
			int width, height;
			glfwGetWindowSize(win->handle, &width, &height);
			glfwSetWindowMonitor(win->handle, NULL, 0, 0, width, height, 0);
		}

		glfwHideWindow(win->handle);
	}

	// Set attributes and perform one-time actions.
	// Preemptively maximize window in case resizable is set to false here.
	if ((flags & GFX_WINDOW_MAXIMIZE) && !(flags & GFX_WINDOW_RESIZABLE))
		glfwMaximizeWindow(win->handle);

	glfwSetWindowAttrib(win->handle, GLFW_DECORATED,
		flags & GFX_WINDOW_BORDERLESS ? GLFW_FALSE : GLFW_TRUE);
	glfwSetWindowAttrib(win->handle, GLFW_RESIZABLE,
		flags & GFX_WINDOW_RESIZABLE ? GLFW_TRUE : GLFW_FALSE);

	if (flags & GFX_WINDOW_FOCUS)
		glfwFocusWindow(win->handle);
	if ((flags & GFX_WINDOW_MAXIMIZE) && (flags & GFX_WINDOW_RESIZABLE))
		glfwMaximizeWindow(win->handle);

	// Set the input mode for the cursor.
	const int cursor =
		(flags & GFX_WINDOW_CAPTURE_MOUSE) ? GLFW_CURSOR_DISABLED :
		(flags & GFX_WINDOW_HIDE_MOUSE) ? GLFW_CURSOR_HIDDEN :
		GLFW_CURSOR_NORMAL;

	glfwSetInputMode(win->handle, GLFW_CURSOR, cursor);

	// Use raw mouse position if GFX_WINDOW_CAPTURE_MOUSE is set.
	if (cursor == GLFW_CURSOR_DISABLED && glfwRawMouseMotionSupported())
		glfwSetInputMode(win->handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

	// Finally check if we changed the buffer settings.
	// We lock such that setting the flags and signaling it
	// are both in the same atomic operation.
	GFXWindowFlags bufferBits =
		GFX_WINDOW_DOUBLE_BUFFER | GFX_WINDOW_TRIPLE_BUFFER;

	_gfx_mutex_lock(&win->frame.lock);

	// If buffer settings changed, signal a swapchain recreate.
	if ((flags & bufferBits) != (win->frame.flags & bufferBits))
		atomic_store_explicit(&win->frame.recreate, 1, memory_order_relaxed);

	win->frame.flags = flags;

	_gfx_mutex_unlock(&win->frame.lock);
}

/****************************/
GFX_API GFXMonitor* gfx_window_get_monitor(GFXWindow* window)
{
	assert(window != NULL);

	GLFWmonitor* monitor =
		glfwGetWindowMonitor(((_GFXWindow*)window)->handle);

	// Each GLFW monitor should have a user pointer to the groufix monitor :)
	return (monitor == NULL) ? NULL :
		glfwGetMonitorUserPointer(monitor);
}

/****************************/
GFX_API void gfx_window_set_monitor(GFXWindow* window, GFXMonitor* monitor,
                                    GFXVideoMode mode)
{
	assert(window != NULL);
	assert(mode.width > 0);
	assert(mode.height > 0);

	// If it's hidden, GLFW unhides for us.
	glfwSetWindowMonitor(
		((_GFXWindow*)window)->handle,
		(monitor != NULL) ? ((_GFXMonitor*)monitor)->handle : NULL,
		0, 0,
		(int)mode.width,
		(int)mode.height,
		mode.refresh == 0 ? GLFW_DONT_CARE : (int)mode.refresh);
}

/****************************/
GFX_API GFXVideoMode gfx_window_get_video(GFXWindow* window)
{
	assert(window != NULL);

	_GFXWindow* win = (_GFXWindow*)window;
	GLFWmonitor* monitor = glfwGetWindowMonitor(win->handle);

	GFXVideoMode mode = { 0, 0, 0 };

	if (monitor == NULL)
	{
		int width;
		int height;

		glfwGetWindowSize(win->handle, &width, &height);
		mode.width = (uint32_t)width;
		mode.height = (uint32_t)height;
	}
	else
	{
		const GLFWvidmode* vid = glfwGetVideoMode(monitor);
		mode.width = (uint32_t)vid->width;
		mode.height = (uint32_t)vid->height;
		mode.refresh = (unsigned int)vid->refreshRate;
	}

	return mode;
}

/****************************/
GFX_API void gfx_window_set_video(GFXWindow* window, GFXVideoMode mode)
{
	assert(window != NULL);
	assert(mode.width > 0);
	assert(mode.height > 0);

	_GFXWindow* win = (_GFXWindow*)window;
	GLFWmonitor* monitor = glfwGetWindowMonitor(win->handle);

	// If it's hidden, monitor will be NULL and nothing will happen.
	if (monitor == NULL)
		glfwSetWindowSize(
			win->handle,
			(int)mode.width,
			(int)mode.height);
	else
		glfwSetWindowMonitor(
			win->handle,
			monitor,
			0, 0,
			(int)mode.width,
			(int)mode.height,
			mode.refresh == 0 ? GLFW_DONT_CARE : (int)mode.refresh);
}

/****************************/
GFX_API const char* gfx_window_get_title(GFXWindow* window)
{
	assert(window != NULL);

	return glfwGetWindowTitle(((_GFXWindow*)window)->handle);
}

/****************************/
GFX_API void gfx_window_set_title(GFXWindow* window, const char* title)
{
	assert(window != NULL);
	assert(title != NULL);

	glfwSetWindowTitle(((_GFXWindow*)window)->handle, title);
}

/****************************/
GFX_API bool gfx_window_should_close(GFXWindow* window)
{
	assert(window != NULL);

	return glfwWindowShouldClose(((_GFXWindow*)window)->handle);
}

/****************************/
GFX_API void gfx_window_set_close(GFXWindow* window, bool close)
{
	assert(window != NULL);

	glfwSetWindowShouldClose(((_GFXWindow*)window)->handle, close);
}

/****************************/
GFX_API void gfx_window_focus(GFXWindow* window)
{
	assert(window != NULL);

	// GLFW won't do anything if hidden.
	glfwFocusWindow(((_GFXWindow*)window)->handle);
}

/****************************/
GFX_API void gfx_window_maximize(GFXWindow* window)
{
	assert(window != NULL);

	// GLFW won't do anything if hidden.
	glfwMaximizeWindow(((_GFXWindow*)window)->handle);
}

/****************************/
GFX_API void gfx_window_minimize(GFXWindow* window)
{
	assert(window != NULL);

	// GLFW won't do anything if hidden.
	glfwIconifyWindow(((_GFXWindow*)window)->handle);
}

/****************************/
GFX_API void gfx_window_restore(GFXWindow* window)
{
	assert(window != NULL);

	// GLFW won't do anything if hidden.
	glfwRestoreWindow(((_GFXWindow*)window)->handle);
}
