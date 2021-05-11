/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <assert.h>
#include <stdarg.h>
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
 * Logs a new line to stderr.
 */
static void _gfx_log_out(unsigned int thread,
                         GFXLogLevel level, double timeMs,
                         const char* file, unsigned int line,
                         const char* fmt, va_list args)
{
	const char* L = _gfx_log_levels[level-1];

#if defined (GFX_UNIX)
	if (isatty(STDERR_FILENO))
	{
		// If on unix and stderr is a tty, use color.
		const char* C = _gfx_log_colors[level-1];

		fprintf(stderr,
			"%.2ems %s%-5s\x1b[0m \x1b[90mthread-%u: %s:%u:\x1b[0m ",
			timeMs, C, L, thread, file, line);
	}
	else
	{
#endif
		// If not, or not on unix at all, output regularly.
		fprintf(stderr,
			"%.2ems %-5s thread-%u: %s:%u: ",
			timeMs, L, thread, file, line);

#if defined (GFX_UNIX)
	}
#endif

	vfprintf(stderr, fmt, args);
	putc('\n', stderr);
}

/****************************
 * Logs a new line to a log file.
 */
static void _gfx_log_file(FILE* out,
                          GFXLogLevel level, double timeMs,
                          const char* file, unsigned int line,
                          const char* fmt, va_list args)
{
	const char* L = _gfx_log_levels[level-1];

	fprintf(out, "%.2ems %-5s %s:%u: ",
		timeMs, L, file, line);

	vfprintf(out, fmt, args);
	fputc('\n', out);
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

	// So we get seconds that the CPU has spent on this program...
	// We calculate it here so stderr and the file record the same time.
	double timeMs = 1000.0 * (double)clock() / CLOCKS_PER_SEC;
	unsigned int thread = 0;

	// Logging is special, when groufix is not initialized,
	// we still output to stderr.
	// Check against default log level.
	if (!_groufix.initialized)
	{
		if (level <= _groufix.logDef)
		{
			va_start(args, fmt);
			_gfx_log_out(thread, level, timeMs, file, line, fmt, args);
			va_end(args);
		}
		return;
	}

	// Get the thread local state.
	_GFXThreadState* state = _gfx_get_local();
	if (state == NULL)
	{
		// If no state, check against default log level.
		if (level > _groufix.logDef)
			return;
	}
	else
	{
		// Check against thread log level.
		if (level > state->log.level)
			return;

		// If the state contains a file, output to that.
		if (state->log.file != NULL)
		{
			va_start(args, fmt);

			_gfx_log_file(state->log.file,
				level, timeMs, file, line, fmt, args);

			va_end(args);
		}

		// If the state says not to output to stderr, we're done.
		if (!state->log.std)
			return;

		thread = state->id;
	}

	// If no thread local state was present
	// OR the state said so, we output to stderr.
	// But groufix is initialized, so we need to lock access to stderr.
	va_start(args, fmt);

	_gfx_mutex_lock(&_groufix.thread.ioLock);
	_gfx_log_out(thread, level, timeMs, file, line, fmt, args);
	_gfx_mutex_unlock(&_groufix.thread.ioLock);

	va_end(args);
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
GFX_API int gfx_log_set_out(int out)
{
	// Again, logging is special.
	if (!_groufix.initialized)
		return 0;

	_GFXThreadState* state = _gfx_get_local();
	if (state == NULL)
		return 0;

	state->log.std = out;

	return 1;
}

/****************************/
GFX_API int gfx_log_set_file(const char* file)
{
	if (!_groufix.initialized)
		return 0;

	_GFXThreadState* state = _gfx_get_local();
	if (state == NULL)
		return 0;

	// If a previous file was present, close it.
	if (state->log.file != NULL)
		fclose(state->log.file);

	state->log.file = NULL;

	if (file != NULL)
	{
		// Now open the appropriate logging file, if any.
		// We are going to append the thread id to the filename...
		// First find the length of the thread id.
		size_t idLen = (size_t)snprintf(NULL, 0, "%.4u", state->id);

		// Now find the point at which to insert the thread id.
		// This is before the first '.' character.
		// Append to end if no '.' found.
		size_t s = strlen(file);
		size_t i;

		for (i = 0; i < s; ++i)
			if (file[i] == '.') break;

		// Create a string for the file name.
		char f[s + idLen + 1];
		sprintf(f, "%.*s%.4u%s", (int)i, file, state->id, file + i);

		// Now finally attempt to open the file.
		state->log.file = fopen(f, "w");

		// Log error in case we output to stderr.
		if (state->log.file == NULL)
		{
			gfx_log_error("Could not open log file: %s", f);
			return 0;
		}
	}

	return 1;
}
