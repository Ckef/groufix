/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined (GFX_UNIX)
	#include <unistd.h>
#endif


/****************************
 * Stringified logging level options for interpreting
 * the GROUFIX_DEFAULT_LOG_LEVEL environment variable.
 */
static const char* _gfx_log_env_levels[] = {
	"NONE", "FATAL", "ERROR", "WARN", "INFO", "DEBUG", "VERBOSE", "ALL"
};


/****************************
 * Stringified logging levels for output.
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
 * Writes the log header to a buffered writer stream.
 */
static void _gfx_log_header(GFXBufWriter* out,
                            uintmax_t thread, GFXLogLevel level,
                            const char* file, unsigned int line)
{
	const char* L = _gfx_log_levels[level-1];

	// If file contains 'groufix' in it, only print it from there.
	// Makes the logs a little less bulky, cheeky but nice :)
	const char* f = strstr(file, "groufix");
	file = (f == NULL) ? file : f;

	// So we get seconds that the CPU has spent on this program.
	const double timeMs = 1000.0 * (double)clock() / CLOCKS_PER_SEC;

#if defined (GFX_UNIX)
	if (
		(out->dest == GFX_IO_STDOUT && isatty(STDOUT_FILENO)) ||
		(out->dest == GFX_IO_STDERR && isatty(STDERR_FILENO)))
	{
		// If on unix, logging to stdout/stderr and it is a tty, use color.
		const char* C = _gfx_log_colors[level-1];

		gfx_io_writef(out,
			"%.2ems %s%-5s \x1b[90mthread-%"PRIuMAX": %s:%u: \x1b[0m",
			timeMs, C, L, thread, file, line);
	}
	else
	{
#endif
		// If not, or not on unix at all, output regularly.
		gfx_io_writef(out,
			"%.2ems %-5s thread-%"PRIuMAX": %s:%u: ",
			timeMs, L, thread, file, line);

#if defined (GFX_UNIX)
	}
#endif
}

/****************************/
void _gfx_log_set_default_level(void)
{
	// Get the env var for the default log level.
	const char* envLogDef = getenv(GFX_ENV_DEFAULT_LOG_LEVEL);
	const size_t options = sizeof(_gfx_log_env_levels)/sizeof(char*);

	if (envLogDef == NULL) return; // No value given.

	// Loop over all stringified options, get case insenstive match.
	for (GFXLogLevel level = 0; level < options; ++level)
	{
		const char* opt = _gfx_log_env_levels[level];
		const char* inp = envLogDef;

		for (; *opt != '\0' && *inp != '\0'; ++opt, ++inp)
			if (tolower(*opt) != tolower(*inp)) break;

		if (*opt == '\0' && *inp == '\0')
		{
			// On a match, set the global default!
			_groufix.logDef = level;
			return;
		}
	}

	// If no match against a log level, silently ignore.
}

/****************************/
GFX_API void gfx_log(GFXLogLevel level,
                     const char* file, unsigned int line,
                     const char* fmt, ...)
{
	assert(level > GFX_LOG_NONE && level < GFX_LOG_ALL);
	assert(file != NULL);
	assert(fmt != NULL);

	va_list args;

	// If groufix is initialized..
	if (atomic_load(&_groufix.initialized))
	{
		// Default to next thread id.
		uintmax_t thread =
			atomic_load_explicit(&_groufix.thread.id, memory_order_relaxed);

		GFXBufWriter* out = &_gfx_io_buf_def;
		GFXLogLevel logLevel = _groufix.logDef;

		// If there is thread local state, use its params.
		_GFXThreadState* state = _gfx_get_local();
		if (state != NULL)
		{
			out = &state->log.out;
			thread = state->id;
			logLevel = state->log.level;
		}

		// Check output's destination stream & log level.
		if (out->dest != NULL && level <= logLevel)
		{
			va_start(args, fmt);

			_gfx_mutex_lock(&_groufix.thread.ioLock);
			_gfx_log_header(out, thread, level, file, line);
			gfx_io_vwritef(out, fmt, args);
			gfx_io_write(&out->writer, "\n", sizeof(char));
			gfx_io_flush(out);
			_gfx_mutex_unlock(&_groufix.thread.ioLock);

			va_end(args);
		}
	}

	// Logging is special;
	// when groufix is not initialized we output to the default logger,
	// assuming thread id 0 and the default log level.
	else if (level <= _groufix.logDef)
	{
		va_start(args, fmt);

		_gfx_log_header(&_gfx_io_buf_def, 0, level, file, line);
		gfx_io_vwritef(&_gfx_io_buf_def, fmt, args);
		gfx_io_write(&_gfx_io_buf_def.writer, "\n", sizeof(char));
		gfx_io_flush(&_gfx_io_buf_def);

		va_end(args);
	}
}

/****************************/
GFX_API GFXBufWriter* gfx_logger(GFXLogLevel level,
                                 const char* file, unsigned int line)
{
	assert(level >= GFX_LOG_NONE && level < GFX_LOG_ALL);
	assert(file != NULL);

	if (level == GFX_LOG_NONE) return NULL; // No-op by design.

	// If groufix is initialized..
	if (atomic_load(&_groufix.initialized))
	{
		// Default to next thread id.
		uintmax_t thread =
			atomic_load_explicit(&_groufix.thread.id, memory_order_relaxed);

		GFXBufWriter* out = &_gfx_io_buf_def;
		GFXLogLevel logLevel = _groufix.logDef;

		// If there is thread local state, use its params.
		_GFXThreadState* state = _gfx_get_local();
		if (state != NULL)
		{
			out = &state->log.out;
			thread = state->id;
			logLevel = state->log.level;
		}

		// Check output's destination stream & log level.
		if (out->dest != NULL && level <= logLevel)
		{
			// Leave locked for gfx_logger_end()!
			_gfx_mutex_lock(&_groufix.thread.ioLock);
			_gfx_log_header(out, thread, level, file, line);
			return out;
		}
	}

	// And if not, output to default logger just like gfx_log().
	else if (level <= _groufix.logDef)
	{
		_gfx_log_header(&_gfx_io_buf_def, 0, level, file, line);
		return &_gfx_io_buf_def;
	}

	return NULL;
}

/****************************/
GFX_API void gfx_logger_end(GFXBufWriter* logger)
{
	if (logger == NULL)
		return;

	// First write \n and flush.
	gfx_io_write(&logger->writer, "\n", sizeof(char));
	gfx_io_flush(logger);

	// Unlock if groufix is initialized!
	// Note: it is not allowed to initialize/terminate before this call!
	if (atomic_load(&_groufix.initialized))
		_gfx_mutex_unlock(&_groufix.thread.ioLock);
}

/****************************/
GFX_API bool gfx_log_set_level(GFXLogLevel level)
{
	assert(level >= GFX_LOG_NONE && level <= GFX_LOG_ALL);

	// Set default log level if not initialized.
	GFXLogLevel* logLevel = &_groufix.logDef;

	if (atomic_load(&_groufix.initialized))
	{
		_GFXThreadState* state = _gfx_get_local();
		if (state == NULL) return 0;

		logLevel = &state->log.level;
	}

	*logLevel = level;

	return 1;
}

/****************************/
GFX_API bool gfx_log_set(const GFXWriter* out)
{
	assert(out != NULL);

	// Set default logger if not initialized.
	GFXBufWriter* writer = &_gfx_io_buf_def;

	if (atomic_load(&_groufix.initialized))
	{
		_GFXThreadState* state = _gfx_get_local();
		if (state == NULL) return 0;

		writer = &state->log.out;
	}

	// No need to flush, we rely on gfx_log() and gfx_logger_end() for that!
	gfx_buf_writer(writer, out);

	return 1;
}
