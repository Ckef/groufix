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


/****************************
 * GLFW window close callback.
 */
static void _gfx_glfw_window_close(GLFWwindow* handle)
{
	GFXWindow* window = glfwGetWindowUserPointer(handle);

	if (window->events.close != NULL)
		window->events.close(window);
}

/****************************
 * GLFW drop callback.
 */
static void _gfx_glfw_drop(GLFWwindow* handle, int count, const char** paths)
{
	GFXWindow* window = glfwGetWindowUserPointer(handle);

	if (window->events.drop != NULL)
		window->events.drop(window, (size_t)count, paths);
}

/****************************
 * GLFW window focus callback.
 */
static void _gfx_glfw_window_focus(GLFWwindow* handle, int focused)
{
	GFXWindow* window = glfwGetWindowUserPointer(handle);

	if (focused)
	{
		if (window->events.focus != NULL)
			window->events.focus(window);
	}
	else
	{
		if (window->events.blur != NULL)
			window->events.blur(window);
	}
}

/****************************
 * GLFW window maximize callback.
 */
static void _gfx_glfw_window_maximize(GLFWwindow* handle, int maximized)
{
	GFXWindow* window = glfwGetWindowUserPointer(handle);

	if (maximized)
	{
		if (window->events.maximize != NULL)
			window->events.maximize(window);
	}
	else
	{
		if (window->events.restore != NULL)
			window->events.restore(window);
	}
}

/****************************
 * GLFW window iconify callback.
 */
static void _gfx_glfw_window_iconify(GLFWwindow* handle, int iconified)
{
	GFXWindow* window = glfwGetWindowUserPointer(handle);

	if (iconified)
	{
		if (window->events.minimize != NULL)
			window->events.minimize(window);
	}
	else
	{
		if (window->events.restore != NULL)
			window->events.restore(window);
	}
}

/****************************
 * GLFW window pos callback.
 */
static void _gfx_glfw_window_pos(GLFWwindow* handle, int x, int y)
{
	GFXWindow* window = glfwGetWindowUserPointer(handle);

	if (window->events.move != NULL)
		window->events.move(window, x, y);
}

/****************************
 * GLFW window size callback.
 */
static void _gfx_glfw_window_size(GLFWwindow* handle, int width, int height)
{
	GFXWindow* window = glfwGetWindowUserPointer(handle);

	if (window->events.resize != NULL)
		window->events.resize(window, (size_t)width, (size_t)height);
}

/****************************
 * GLFW key callback.
 */
static void _gfx_glfw_key(GLFWwindow* handle,
                          int key, int scancode, int action, int mods)
{
	GFXWindow* window = glfwGetWindowUserPointer(handle);

	switch (action)
	{
	case GLFW_PRESS:
		if (window->events.key.press != NULL)
			window->events.key.press(window, key, scancode, mods);
		break;
	case GLFW_RELEASE:
		if (window->events.key.release != NULL)
			window->events.key.release(window, key, scancode, mods);
		break;
	case GLFW_REPEAT:
		if (window->events.key.repeat != NULL)
			window->events.key.repeat(window, key, scancode, mods);
		break;
	}
}

/****************************
 * GLFW char callback.
 */
static void _gfx_glfw_char(GLFWwindow* handle, unsigned int codepoint)
{
	GFXWindow* window = glfwGetWindowUserPointer(handle);

	if (window->events.key.text != NULL)
		window->events.key.text(window, codepoint);
}

/****************************
 * GLFW cursor enter callback.
 */
static void _gfx_glfw_cursor_enter(GLFWwindow* handle, int entered)
{
	GFXWindow* window = glfwGetWindowUserPointer(handle);

	if (entered)
	{
		if (window->events.mouse.enter != NULL)
			window->events.mouse.enter(window);
	}
	else
	{
		if (window->events.mouse.leave != NULL)
			window->events.mouse.leave(window);
	}
}

/****************************
 * GLFW cursor position callback.
 */
static void _gfx_glfw_cursor_pos(GLFWwindow* handle, double x, double y)
{
	GFXWindow* window = glfwGetWindowUserPointer(handle);

	if (window->events.mouse.move != NULL)
		window->events.mouse.move(window, x, y);
}

/****************************
 * GLFW mouse button callback.
 */
static void _gfx_glfw_mouse_button(GLFWwindow* handle,
                                   int button, int action, int mods)
{
	GFXWindow* window = glfwGetWindowUserPointer(handle);

	switch (action)
	{
	case GLFW_PRESS:
		if (window->events.mouse.press != NULL)
			window->events.mouse.press(window, button, mods);
		break;
	case GLFW_RELEASE:
		if (window->events.mouse.release != NULL)
			window->events.mouse.release(window, button, mods);
		break;
	}
}

/****************************
 * GLFW scroll callback.
 */
static void _gfx_glfw_scroll(GLFWwindow* handle, double x, double y)
{
	GFXWindow* window = glfwGetWindowUserPointer(handle);

	if (window->events.mouse.scroll != NULL)
		window->events.mouse.scroll(window, x, y);
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
	_gfx_mutex_lock(&window->frame.lock);

	window->frame.resized = 1;
	window->frame.width = (size_t)width;
	window->frame.height = (size_t)height;

	_gfx_mutex_unlock(&window->frame.lock);
}

/****************************
 * Picks a presentation queue family (including a specific presentation queue).
 * window->vk.surface must be initialized to a valid Vulkan surface.
 * _gfx_device_init_context(window->device) must have returned successfully.
 * @param window Cannot be NULL.
 * @return Non-zero on success.
 *
 * This can only be called once for each window!
 */
static int _gfx_window_pick_present(_GFXWindow* window)
{
	assert(window != NULL);
	assert(window->device != NULL);
	assert(window->device->context != NULL);

	_GFXContext* context = window->device->context;

	window->present.queue = NULL;
	gfx_vec_init(&window->present.access, sizeof(uint32_t));

	for (size_t i = 0; i < context->sets.size; ++i)
	{
		_GFXQueueSet* set = *(_GFXQueueSet**)gfx_vec_at(&context->sets, i);

		// We only care about the family if it is a graphics family OR
		// it specifically tells us it is capable of presenting.
		if (!(set->flags & VK_QUEUE_GRAPHICS_BIT || set->present))
			continue;

		if (!gfx_vec_push(&window->present.access, 1, &set->family))
			return 0;

		// We checked presentation support in a surface-agnostic manner
		// during logical device creation, now go check for the given surface.
		VkBool32 support = VK_FALSE;
		_groufix.vk.GetPhysicalDeviceSurfaceSupportKHR(
			window->device->vk.device,
			set->family,
			window->vk.surface,
			&support);

		// Just take a presentation queue from whatever family supports it.
		// We take the first queue in this family.
		if (support == VK_TRUE)
		{
			window->present.family = set->family;
			window->present.mutex = &set->mutexes[0];

			context->vk.GetDeviceQueue(
				context->vk.device, set->family, 0, &window->present.queue);
		}
	}

	// Uuuuuh hold up...
	if (window->present.queue == NULL)
	{
		gfx_log_error(
			"Could not find a queue family with surface presentation "
			"support on physical device: %s.",
			window->device->base.name);

		return 0;
	}

	return 1;
}

/****************************/
GFX_API GFXWindow* gfx_create_window(GFXWindowFlags flags, GFXDevice* device,
                                     GFXMonitor* monitor, GFXVideoMode mode,
                                     const char* title)
{
	assert(mode.width > 0);
	assert(mode.height > 0);
	assert(title != NULL);
	assert(_groufix.vk.instance != NULL);

	// Allocate and set a new window.
	// Just set the user pointer and all callbacks to all 0's.
	_GFXWindow* window = malloc(sizeof(_GFXWindow));
	if (window == NULL)
		goto clean;

	memset(&window->base, 0, sizeof(GFXWindow));

	// Create a GLFW window.
	glfwDefaultWindowHints();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

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
		glfwWindowHint(GLFW_REFRESH_RATE, (int)mode.refresh);

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
	int cursor =
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
	// Initialize the locks for resize signal and set the current
	// width and height of the window's framebuffer.
	if (!_gfx_mutex_init(&window->frame.lock))
		goto clean_window;

	int width;
	int height;
	glfwGetFramebufferSize(window->handle, &width, &height);

	gfx_vec_init(&window->frame.images, sizeof(VkImage));

	window->frame.resized = 0;
	window->frame.width = (size_t)width;
	window->frame.height = (size_t)height;
	window->frame.flags = flags;

	// Now we need to somehow connect it to a GPU.
	// So attempt to create a Vulkan surface for the window.
	VkResult result = glfwCreateWindowSurface(
		_groufix.vk.instance, window->handle, NULL, &window->vk.surface);

	if (result != VK_SUCCESS)
	{
		_gfx_vulkan_log(result);
		goto clean_frame;
	}

	// Get the physical device and make sure it's initialized.
	window->device =
		(_GFXDevice*)((device != NULL) ? device : gfx_get_primary_device());

	if (!_gfx_device_init_context(window->device))
		goto clean_surface;

	_GFXContext* context =
		window->device->context;

	// Ok so we have a physical device with a context (logical Vulkan device).
	// Now go create a swapchain. Unfortunately we cannot clean the context
	// if it was just created for us, but that's why we do this stuff last.
	// First find all we pick a presentation queue.
	if (!_gfx_window_pick_present(window))
		goto clean_present;

	// Make sure to set it to a NULL handle here so a new one gets created.
	window->vk.swapchain = VK_NULL_HANDLE;

	if (!_gfx_swapchain_recreate(window))
		goto clean_present;

	// Don't forget the synchronization primitives.
	// We use these to signal when a new swapchain image is available
	// and wait for rendering to be done so we can present.
	// These aren't initialized by the swapchain because they do not
	// need to be recreated.
	window->vk.available = VK_NULL_HANDLE;
	window->vk.rendered = VK_NULL_HANDLE;
	window->vk.fence = VK_NULL_HANDLE;

	// Firstly, two semaphores for device synchronization.
	VkSemaphoreCreateInfo sci = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0
	};

	VkResult resAvail = context->vk.CreateSemaphore(
		context->vk.device, &sci, NULL, &window->vk.available);

	VkResult resRend = context->vk.CreateSemaphore(
		context->vk.device, &sci, NULL, &window->vk.rendered);

	// Secondly, a fence for host synchronization.
	 VkFenceCreateInfo fci = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = NULL,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT
	};

	VkResult resFen = context->vk.CreateFence(
		context->vk.device, &fci, NULL, &window->vk.fence);

	if (resAvail != VK_SUCCESS || resRend != VK_SUCCESS || resFen != VK_SUCCESS)
		goto clean_swapchain;

	// All successful!
	return &window->base;


	// Cleanup on failure.
clean_swapchain:
	context->vk.DestroyFence(
		context->vk.device, window->vk.fence, NULL);
	context->vk.DestroySemaphore(
		context->vk.device, window->vk.rendered, NULL);
	context->vk.DestroySemaphore(
		context->vk.device, window->vk.available, NULL);
	context->vk.DestroySwapchainKHR(
		context->vk.device, window->vk.swapchain, NULL);

clean_present:
	gfx_vec_clear(&window->present.access);
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
	_GFXContext* context = win->device->context;

	// First wait for all pending presentation to be completely done.
	context->vk.WaitForFences(
		context->vk.device, 1, &win->vk.fence, VK_TRUE, UINT64_MAX);
	context->vk.QueueWaitIdle(
		win->present.queue);

	// Destroy the swapchain built on the logical Vulkan device...
	// Creation was done through _gfx_swapchain_recreate(window).
	context->vk.DestroyFence(
		context->vk.device, win->vk.fence, NULL);
	context->vk.DestroySemaphore(
		context->vk.device, win->vk.rendered, NULL);
	context->vk.DestroySemaphore(
		context->vk.device, win->vk.available, NULL);
	context->vk.DestroySwapchainKHR(
		context->vk.device, win->vk.swapchain, NULL);

	// Destroy the surface and the window itself.
	_groufix.vk.DestroySurfaceKHR(
		_groufix.vk.instance, win->vk.surface, NULL);

	gfx_vec_clear(&win->present.access);
	gfx_vec_clear(&win->frame.images);
	_gfx_mutex_clear(&win->frame.lock);

	glfwDestroyWindow(win->handle);
	free(window);
}

/****************************/
GFX_API void gfx_window_set_flags(GFXWindow* window, GFXWindowFlags flags)
{
	assert(window != NULL);

	_GFXWindow* win = (_GFXWindow*)window;

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
	int cursor =
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

	// If buffer settings changed, pretend it's a resize, which actually just
	// recreates the swapchain, which is exactly what we need, so it's fine.
	if ((flags & bufferBits) != (win->frame.flags & bufferBits))
		win->frame.resized = 1;

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

	glfwSetWindowMonitor(
		((_GFXWindow*)window)->handle,
		(monitor != NULL) ? ((_GFXMonitor*)monitor)->handle : NULL,
		0, 0,
		(int)mode.width,
		(int)mode.height,
		(int)mode.refresh);
}

/****************************/
GFX_API void gfx_window_set_video(GFXWindow* window, GFXVideoMode mode)
{
	assert(window != NULL);
	assert(mode.width > 0);
	assert(mode.height > 0);

	_GFXWindow* win = (_GFXWindow*)window;
	GLFWmonitor* monitor = glfwGetWindowMonitor(win->handle);

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
			(int)mode.refresh);
}

/****************************/
GFX_API void gfx_window_set_title(GFXWindow* window, const char* title)
{
	assert(window != NULL);
	assert(title != NULL);

	glfwSetWindowTitle(((_GFXWindow*)window)->handle, title);
}

/****************************/
GFX_API int gfx_window_should_close(GFXWindow* window)
{
	assert(window != NULL);

	return glfwWindowShouldClose(((_GFXWindow*)window)->handle);
}

/****************************/
GFX_API void gfx_window_set_close(GFXWindow* window, int close)
{
	assert(window != NULL);

	glfwSetWindowShouldClose(((_GFXWindow*)window)->handle, close);
}

/****************************/
GFX_API void gfx_window_maximize(GFXWindow* window)
{
	assert(window != NULL);

	glfwMaximizeWindow(((_GFXWindow*)window)->handle);
}

/****************************/
GFX_API void gfx_window_minimize(GFXWindow* window)
{
	assert(window != NULL);

	glfwIconifyWindow(((_GFXWindow*)window)->handle);
}

/****************************/
GFX_API void gfx_window_restore(GFXWindow* window)
{
	assert(window != NULL);

	glfwRestoreWindow(((_GFXWindow*)window)->handle);
}
