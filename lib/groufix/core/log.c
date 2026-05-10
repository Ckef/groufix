/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if defined (GFX_UNIX)
	#include <unistd.h>
#endif


/****************************
 * Stringified logging level options for interpreting
 * the GROUFIX_DEFAULT_LOG_LEVEL environment variable.
 */
static const char* gfx_log_env_levels_[] = {
	"NONE", "FATAL", "ERROR", "WARN", "INFO", "DEBUG", "VERBOSE", "ALL"
};


/****************************
 * Stringified logging levels for output.
 * Verbose debug has the same name, but different color.
 */
static const char* gfx_log_levels_[] = {
	"FATAL", "ERROR", "WARN", "INFO", "DEBUG", "DEBUG"
};


#if defined (GFX_UNIX)

/****************************
 * Stringified logging colors for each level.
 */
static const char* gfx_log_colors_[] = {
	"\x1b[35m", "\x1b[31m", "\x1b[33m", "\x1b[32m", "\x1b[36m", "\x1b[94m"
};

#endif


/****************************
 * Retrieves the current time in seconds.
 * groufix must be initialized!
 */
static inline double gfx_time_s_(void)
{
	return
		(double)gfx_clock_get_time_(&groufix_.clock) /
		(double)groufix_.clock.frequency;
}

/****************************
 * Writes the log header to a buffered writer stream.
 */
static void gfx_log_header_(GFXBufWriter* out,
                            double time_s, uintmax_t thread, GFXLogLevel level,
                            const char* file, unsigned int line)
{
	const char* L = gfx_log_levels_[level-1];

	const int64_t m = (int64_t)(time_s / 60.0);
	const int64_t s = (int64_t)time_s % 60;
	const int64_t ms = (int64_t)(time_s * 1000.0) % 1000;

	// If file contains 'groufix' in it, only print it from there.
	// Makes the logs a little less bulky, cheeky but nice :)
	const char* f = strstr(file, "groufix");
	file = (f == NULL) ? file : f;

#if defined (GFX_UNIX)
	if (
		(out->dest == GFX_IO_STDOUT && isatty(STDOUT_FILENO)) ||
		(out->dest == GFX_IO_STDERR && isatty(STDERR_FILENO)))
	{
		// If on unix, logging to stdout/stderr and it is a tty, use color.
		const char* C = gfx_log_colors_[level-1];

		gfx_io_writef(out,
			"\x1b[90m[%"PRIi64"m:%02"PRIi64"s:%03"PRIi64"ms] "
			"%s%-5s \x1b[90mthread-%"PRIuMAX": %s:%u: \x1b[0m",
			m, s, ms, C, L, thread, file, line);
	}
	else
	{
#endif
		// If not, or not on unix at all, output regularly.
		gfx_io_writef(out,
			"[%"PRIi64"m:%02"PRIi64"s:%03"PRIi64"ms] "
			"%-5s thread-%"PRIuMAX": %s:%u: ",
			m, s, ms, L, thread, file, line);

#if defined (GFX_UNIX)
	}
#endif
}

/****************************/
void gfx_log_set_default_level_(void)
{
	// Get the env var for the default log level.
	const char* envLogDef = getenv(GFX_ENV_DEFAULT_LOG_LEVEL);
	const size_t options = sizeof(gfx_log_env_levels_)/sizeof(char*);

	if (envLogDef == NULL) return; // No value given.

	// Loop over all stringified options, get case insenstive match.
	for (GFXLogLevel level = 0; level < options; ++level)
	{
		const char* opt = gfx_log_env_levels_[level];
		const char* inp = envLogDef;

		for (; *opt != '\0' && *inp != '\0'; ++opt, ++inp)
			if (tolower(*opt) != tolower(*inp)) break;

		if (*opt == '\0' && *inp == '\0')
		{
			// On a match, set the global default!
			groufix_.logDef = level;
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
	if (atomic_load(&groufix_.initialized))
	{
		// Default to next thread id.
		uintmax_t thread =
			atomic_load_explicit(&groufix_.thread.id, memory_order_relaxed);

		GFXBufWriter* out = &gfx_io_buf_def_;
		GFXLogLevel logLevel = groufix_.logDef;

		// If there is thread local state, use its params.
		GFXThreadState_* state = gfx_get_local_();
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

			gfx_mutex_lock_(&groufix_.thread.ioLock);
			gfx_log_header_(out, gfx_time_s_(), thread, level, file, line);
			gfx_io_vwritef(out, fmt, args);
			gfx_io_write(&out->writer, "\n", sizeof(char));
			gfx_io_flush(out);
			gfx_mutex_unlock_(&groufix_.thread.ioLock);

			va_end(args);
		}
	}

	// Logging is special;
	// when groufix is not initialized we output to the default logger,
	// assuming thread id 0 and the default log level.
	else if (level <= groufix_.logDef)
	{
		va_start(args, fmt);

		gfx_log_header_(&gfx_io_buf_def_, 0.0, 0, level, file, line);
		gfx_io_vwritef(&gfx_io_buf_def_, fmt, args);
		gfx_io_write(&gfx_io_buf_def_.writer, "\n", sizeof(char));
		gfx_io_flush(&gfx_io_buf_def_);

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
	if (atomic_load(&groufix_.initialized))
	{
		// Default to next thread id.
		uintmax_t thread =
			atomic_load_explicit(&groufix_.thread.id, memory_order_relaxed);

		GFXBufWriter* out = &gfx_io_buf_def_;
		GFXLogLevel logLevel = groufix_.logDef;

		// If there is thread local state, use its params.
		GFXThreadState_* state = gfx_get_local_();
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
			gfx_mutex_lock_(&groufix_.thread.ioLock);
			gfx_log_header_(out, gfx_time_s_(), thread, level, file, line);
			return out;
		}
	}

	// And if not, output to default logger just like gfx_log().
	else if (level <= groufix_.logDef)
	{
		gfx_log_header_(&gfx_io_buf_def_, 0.0, 0, level, file, line);
		return &gfx_io_buf_def_;
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
	if (atomic_load(&groufix_.initialized))
		gfx_mutex_unlock_(&groufix_.thread.ioLock);
}

/****************************/
GFX_API bool gfx_log_set_level(GFXLogLevel level)
{
	assert(level >= GFX_LOG_NONE && level <= GFX_LOG_ALL);

	// Set default log level if not initialized.
	GFXLogLevel* logLevel = &groufix_.logDef;

	if (atomic_load(&groufix_.initialized))
	{
		GFXThreadState_* state = gfx_get_local_();
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
	GFXBufWriter* writer = &gfx_io_buf_def_;

	if (atomic_load(&groufix_.initialized))
	{
		GFXThreadState_* state = gfx_get_local_();
		if (state == NULL) return 0;

		writer = &state->log.out;
	}

	// No need to flush, we rely on gfx_log() and gfx_logger_end() for that!
	gfx_buf_writer(writer, out);

	return 1;
}
