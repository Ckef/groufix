/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/window.h"
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

/****************************/
GFX_API GFXWindow* gfx_create_window(GFXWindowFlags flags,
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
	window->flags = flags;

	// Create a GLFW window.
	glfwDefaultWindowHints();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	glfwWindowHint(GLFW_DECORATED,
		flags & GFX_WINDOW_BORDERLESS ? GLFW_FALSE : GLFW_TRUE);
	glfwWindowHint(GLFW_FOCUSED,
		flags & GFX_WINDOW_FOCUSED ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_MAXIMIZED,
		flags & GFX_WINDOW_MAXIMIZED ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_RESIZABLE,
		flags & GFX_WINDOW_RESIZABLE ? GLFW_TRUE : GLFW_FALSE);

	if (monitor != NULL)
	{
		if (flags & GFX_WINDOW_BORDERLESS)
		{
			// If borderless fullscreen, use the current video mode.
			const GLFWvidmode* vid =
				glfwGetVideoMode(((_GFXMonitor*)monitor)->handle);

			glfwWindowHint(GLFW_RED_BITS, vid->redBits);
			glfwWindowHint(GLFW_GREEN_BITS, vid->greenBits);
			glfwWindowHint(GLFW_BLUE_BITS, vid->blueBits);
			glfwWindowHint(GLFW_REFRESH_RATE, vid->refreshRate);

			mode.width = (size_t)vid->width;
			mode.height = (size_t)vid->height;
		}
		else
		{
			// If normal fullscreen, use the given video mode.
			glfwWindowHint(GLFW_REFRESH_RATE, (int)mode.refresh);
		}
	}

	window->handle = glfwCreateWindow(
		(int)mode.width,
		(int)mode.height,
		title,
		(monitor != NULL) ? ((_GFXMonitor*)monitor)->handle : NULL,
		NULL);

	if (window->handle == NULL)
		goto clean;

	// Set the input mode so we register caps/num lock as well.
	// Then associate with GLFW using the user pointer and
	// finally register all callbacks.
	glfwSetInputMode(window->handle, GLFW_LOCK_KEY_MODS, GLFW_TRUE);
	glfwSetWindowUserPointer(window->handle, window);

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

	// And finally attempt to create a Vulkan surface for the window.
	VkResult result = glfwCreateWindowSurface(
		_groufix.vk.instance, window->handle, NULL, &window->vk.surface);

	if (result != VK_SUCCESS)
	{
		_gfx_vulkan_log(result);
		glfwDestroyWindow(window->handle);

		goto clean;
	}

	return &window->base;

	// Cleanup on failure.
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

	_groufix.vk.DestroySurfaceKHR(
		_groufix.vk.instance, ((_GFXWindow*)window)->vk.surface, NULL);
	glfwDestroyWindow(
		((_GFXWindow*)window)->handle);

	free(window);
}

/****************************/
GFX_API void gfx_window_set_monitor(GFXWindow* window,
                                    GFXMonitor* monitor, GFXVideoMode mode)
{
	assert(window != NULL);
	assert(mode.width > 0);
	assert(mode.height > 0);

	// If borderless fullscreen, use the current video mode.
	if (monitor != NULL && ((_GFXWindow*)window)->flags & GFX_WINDOW_BORDERLESS)
		mode = gfx_monitor_get_current_mode(monitor);

	glfwSetWindowMonitor(
		((_GFXWindow*)window)->handle,
		(monitor != NULL) ? ((_GFXMonitor*)monitor)->handle : NULL,
		0,
		0,
		(int)mode.width,
		(int)mode.height,
		(int)mode.refresh);
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
