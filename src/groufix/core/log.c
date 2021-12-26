/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <assert.h>
#include <string.h>
#include <time.h>

#if defined (GFX_UNIX)
	#include <unistd.h>
#endif


/****************************
 * Stringified logging levels.
 * Verbose debug has the same name, but different color.
 */
static const char* _gfx_log_levels[] = {
	"FATAL", "ERROR", "WARN", "INFO", "DEBUG", "DEBUG"
};


#if defined (GFX_UNIX)

/****************************
 * Stringified logging colors for each level.
 */
static const char* _gfx_log_colors[] = {
	"\x1b[35m", "\x1b[31m", "\x1b[33m", "\x1b[32m", "\x1b[36m", "\x1b[94m"
};

#endif


/****************************
 * Logs a new line to a writer stream.
 */
static void _gfx_log(const GFXWriter* out, unsigned long thread,
                     GFXLogLevel level, double timeMs,
                     const char* file, unsigned int line,
                     const char* fmt, va_list args)
{
	const char* L = _gfx_log_levels[level-1];

#if defined (GFX_UNIX)
	if (
		(out == GFX_IO_STDOUT && isatty(STDOUT_FILENO)) ||
		(out == GFX_IO_STDERR && isatty(STDERR_FILENO)))
	{
		// If on unix, logging to stdout/stderr and it is a tty, use color.
		const char* C = _gfx_log_colors[level-1];

		gfx_io_writef(out,
			"%.2ems %s%-5s \x1b[90mthread-%lu: %s:%u: \x1b[0m",
			timeMs, C, L, thread, file, line);
	}
	else
	{
#endif
		// If not, or not on unix at all, output regularly.
		gfx_io_writef(out,
			"%.2ems %-5s thread-%lu: %s:%u: ",
			timeMs, L, thread, file, line);

#if defined (GFX_UNIX)
	}
#endif

	gfx_io_vwritef(out, fmt, args);
	gfx_io_write(out, "\n", sizeof(char));
}

/****************************/
GFX_API void gfx_log(GFXLogLevel level, const char* file, unsigned int line,
                     const char* fmt, ...)
{
	assert(level > GFX_LOG_NONE && level < GFX_LOG_ALL);
	assert(file != NULL);
	assert(fmt != NULL);

	va_list args;

	// If file contains 'groufix' in it, only print it from there.
	// Makes the logs a little less bulky, cheeky but nice :)
	const char* f = strstr(file, "groufix");
	file = (f == NULL) ? file : f;

	// So we get seconds that the CPU has spent on this program.
	double timeMs = 1000.0 * (double)clock() / CLOCKS_PER_SEC;

	// If groufix is initialized..
	if (_groufix.initialized)
	{
		// Default to stderr with default log level.
		const GFXWriter* out = GFX_IO_STDERR;
		unsigned long thread = 0;
		GFXLogLevel logLevel = _groufix.logDef;

		// If there is thread local state, use its params.
		_GFXThreadState* state = _gfx_get_local();
		if (state != NULL)
		{
			out = state->log.out;
			thread = state->id;
			logLevel = state->log.level;
		}

		// Check output stream & log level.
		if (out != NULL && level <= logLevel)
		{
			va_start(args, fmt);

			_gfx_mutex_lock(&_groufix.thread.ioLock);
			_gfx_log(out, thread, level, timeMs, file, line, fmt, args);
			_gfx_mutex_unlock(&_groufix.thread.ioLock);

			va_end(args);
		}
	}

	// Logging is special;
	// when groufix is not initialized we output to stderr,
	// assuming thread id 0 and the default log level.
	else if (level <= _groufix.logDef)
	{
		va_start(args, fmt);
		_gfx_log(GFX_IO_STDERR, 0, level, timeMs, file, line, fmt, args);
		va_end(args);
	}
}

/****************************/
GFX_API int gfx_log_set_level(GFXLogLevel level)
{
	assert(level >= GFX_LOG_NONE && level <= GFX_LOG_ALL);

	// Adjust the only pre-gfx_init() initialized setting.
	if (!_groufix.initialized)
	{
		_groufix.logDef = level;
		return 1;
	}

	// Get the thread local state and set its level.
	_GFXThreadState* state = _gfx_get_local();
	if (state == NULL)
		return 0;

	state->log.level = level;

	return 1;
}

/****************************/
GFX_API int gfx_log_set(const GFXWriter* out)
{
	// Again, logging is special.
	if (!_groufix.initialized)
		return 0;

	_GFXThreadState* state = _gfx_get_local();
	if (state == NULL)
		return 0;

	state->log.out = out;

	return 1;
}
