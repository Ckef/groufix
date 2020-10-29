/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/log.h"
#include "groufix/core.h"
#include <assert.h>
#include <stdarg.h>
#include <time.h>


/****************************/
static const char* _gfx_log_levels[] = {
	"FATAL", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"
};

/****************************/
static const char* _gfx_log_colors[] = {
	"\x1b[35m", "\x1b[31m", "\x1b[33m", "\x1b[32m", "\x1b[36m", "\x1b[94m"
};


/****************************/
static void _gfx_log_out(GFXLogLevel level, double timeMs,
                         const char* file, const char* func, size_t line,
                         const char* fmt, va_list args)
{
	const char* L = _gfx_log_levels[level-1];
	const char* C = _gfx_log_colors[level-1];

	printf("%.2ems %s%-5s\x1b[0m \x1b[90m%s:%zu: %s:\x1b[0m ",
		timeMs, C, L, file, line, func);

	vprintf(fmt, args);
	putc('\n', stdout);
}

/****************************/
static void _gfx_log_file(FILE* out,
                          GFXLogLevel level, double timeMs,
                          const char* file, const char* func, size_t line,
                          const char* fmt, va_list args)
{
	const char* L = _gfx_log_levels[level-1];

	fprintf(out, "%.2ems %-5s %s:%zu: %s: ",
		timeMs, L, file, line, func);

	vfprintf(out, fmt, args);
	fputc('\n', out);
}

/****************************/
GFX_API void gfx_log(GFXLogLevel level, const char* file, const char* func,
                     size_t line, const char* fmt, ...)
{
	assert(level > GFX_LOG_NONE && level < GFX_LOG_ALL);
	assert(file != NULL);
	assert(func != NULL);
	assert(fmt != NULL);

	va_list args;

	// So we get seconds that the CPU has spent on this program...
	// We calculate it here so stdout and the file record the same time.
	double timeMs = 1000.0 * (double)clock() / CLOCKS_PER_SEC;

	// Logging is special, when groufix is not initialized,
	// we still output to stdout.
	// Check against default log level.
	if (!_groufix.initialized)
	{
		if (level <= GFX_LOG_DEFAULT)
		{
			va_start(args, fmt);
			_gfx_log_out(level, timeMs, file, func, line, fmt, args);
			va_end(args);
		}
		return;
	}

	// Get the thread local state.
	_GFXThreadState* state = _gfx_state_get_local();
	if (state == NULL)
	{
		// If no state, check against default log level.
		if (level > GFX_LOG_DEFAULT)
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
				level, timeMs, file, func, line, fmt, args);

			va_end(args);
		}

		// If the state says not to output to stdout, we're done.
		if (!state->log.std)
			return;
	}

	// If no thread local state was present
	// OR the state said so, we output to stdout.
	// But groufix is initialized, so we need to lock access to stdout.
	va_start(args, fmt);

	_gfx_mutex_lock(&_groufix.thread.ioLock);
	_gfx_log_out(level, timeMs, file, func, line, fmt, args);
	_gfx_mutex_unlock(&_groufix.thread.ioLock);

	va_end(args);
}

/****************************/
GFX_API int gfx_log_set_level(GFXLogLevel level)
{
	assert(level >= GFX_LOG_NONE && level <= GFX_LOG_ALL);

	// Because logging is special, we actually check this here.
	if (!_groufix.initialized)
		return 0;

	// Get the thread local state and set its level.
	_GFXThreadState* state = _gfx_state_get_local();
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

	_GFXThreadState* state = _gfx_state_get_local();
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

	_GFXThreadState* state = _gfx_state_get_local();
	if (state == NULL)
		return 0;

	// If a previous file was present, close it.
	if (state->log.file != NULL)
		fclose(state->log.file);

	state->log.file = NULL;

	if (file != NULL)
	{
		// Now open the appropriate logging file, if any.
		// We are going to prepend the thread id to the filename...
		// First find the length of the thread id.
		size_t idLen = (size_t)snprintf(NULL, 0, "%.4u", state->id);

		// Now find the point at which to insert the thread id.
		// This is after the last '/' character.
		size_t i;
		size_t li = 0;
		for (i = 0; file[i] != '\0'; ++i)
			if (file[i] == '/') li = i + 1;

		// Create a string for the file name.
		char f[i + idLen + 1];
		sprintf(f, "%.*s%.4u%s", (int)li, file, state->id, file + li);

		// Now finally attempt to open the file.
		state->log.file = fopen(f, "w");

		if (state->log.file == NULL)
			return 0;
	}

	return 1;
}
