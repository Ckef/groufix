/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef _GFX_CORE_TIME_H
#define _GFX_CORE_TIME_H

#include "groufix/def.h"

#if defined (GFX_UNIX)
	#include <time.h>
#elif defined (GFX_WIN32)
	#include <profileapi.h>
#endif


/**
 * High resolution clock, suitable for time measurement.
 */
typedef struct _GFXClock
{
#if defined (GFX_UNIX)
	clockid_t id;
	struct timespec start;
#elif defined (GFX_WIN32)
	LARGE_INTEGER start;
#endif

	// Ticks per second, read-only.
	int64_t frequency;

} _GFXClock;


/**
 * Initializes (and starts) a high resolution clock.
 * Does not need to be cleared, hence no _init postfix.
 */
static inline void _gfx_clock(_GFXClock* clock)
{
#if defined (GFX_UNIX)
	if (!clock_gettime(CLOCK_MONOTONIC, &clock->start))
		clock->id = CLOCK_MONOTONIC;
	else
	{
		clock_gettime(CLOCK_REALTIME, &clock->start);
		clock->id = CLOCK_REALTIME;
	}

	clock->frequency = 1000000000;

#elif defined (GFX_WIN32)
	LARGE_INTEGER freq;
	QueryPerformanceCounter(&clock->start);
	QueryPerformanceFrequency(&freq);

	clock->frequency = (int64_t)freq.QuadPart;

#endif
}

/**
 * Retrieves monotic (if supported) time from a high resolution clock.
 * @return Ticks since _gfx_clock was called.
 */
static inline int64_t _gfx_clock_get_time(_GFXClock* clock)
{
#if defined (GFX_UNIX)
	struct timespec ts;
	clock_gettime(clock->id, &ts);

	return
		(int64_t)(ts.tv_sec - clock->start.tv_sec) * 1000000000 +
		(int64_t)(ts.tv_nsec - clock->start.tv_nsec);

#elif defined (GFX_WIN32)
	LARGE_INTEGER cnt;
	QueryPerformanceCounter(&cnt);

	return (int64_t)(cnt.QuadPart - clock->start.QuadPart);

#endif
}


#endif
