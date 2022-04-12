/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef _GFX_CORE_THREADS_H
#define _GFX_CORE_THREADS_H

#include "groufix/def.h"

#if defined (GFX_UNIX)
	#include <pthread.h>
#elif defined (GFX_WIN32)
	#include <processthreadsapi.h>
	#include <synchapi.h>
#endif


/**
 * Thread local data key.
 */
#if defined (GFX_UNIX)
	typedef pthread_key_t _GFXThreadKey;
#elif defined (GFX_WIN32)
	typedef DWORD         _GFXThreadKey;
#endif


/**
 * Mutual exclusion lock.
 */
#if defined (GFX_UNIX)
	typedef pthread_mutex_t  _GFXMutex;
#elif defined (GFX_WIN32)
	typedef CRITICAL_SECTION _GFXMutex;
#endif


/****************************
 * Thread local data key.
 ****************************/

/**
 * Initializes a thread local data key.
 * The object pointed to by key cannot be moved or copied!
 * @return Non-zero on success.
 */
static inline bool _gfx_thread_key_init(_GFXThreadKey* key)
{
#if defined (GFX_UNIX)
	return !pthread_key_create(key, NULL);

#elif defined (GFX_WIN32)
	*key = TlsAlloc();
	return *key != TLS_OUT_OF_INDEXES;

#endif
}

/**
 * Clears a thread local data key.
 */
static inline void _gfx_thread_key_clear(_GFXThreadKey key)
{
#if defined (GFX_UNIX)
	pthread_key_delete(key);

#elif defined (GFX_WIN32)
	TlsFree(key);

#endif
}

/**
 * Associate a thread specific value with a local data key.
 * @return Non-zero on success.
 */
static inline bool _gfx_thread_key_set(_GFXThreadKey key, const void* value)
{
#if defined (GFX_UNIX)
	return !pthread_setspecific(key, value);

#elif defined (GFX_WIN32)
	return TlsSetValue(key, (LPVOID)value);

#endif
}

/**
 * Retrieve the thread specific value associated with a local data key.
 * @return The stored value, NULL if no value is associated.
 */
static inline void* _gfx_thread_key_get(_GFXThreadKey key)
{
#if defined (GFX_UNIX)
	return pthread_getspecific(key);

#elif defined (GFX_WIN32)
	return (void*)TlsGetValue(key);

#endif
}


/****************************
 * Mutual exclusion lock.
 ****************************/

/**
 * Initializes a mutex.
 * The object pointed to by mutex cannot be moved or copied!
 * @return Non-zero on success.
 */
static inline bool _gfx_mutex_init(_GFXMutex* mutex)
{
#if defined (GFX_UNIX)
	return !pthread_mutex_init(mutex, NULL);

#elif defined (GFX_WIN32)
	InitializeCriticalSection(mutex);
	return 1;

#endif
}

/**
 * Clears a mutex.
 * Clearing a locked mutex is undefined behaviour.
 */
static inline void _gfx_mutex_clear(_GFXMutex* mutex)
{
#if defined (GFX_UNIX)
	pthread_mutex_destroy(mutex);

#elif defined (GFX_WIN32)
	DeleteCriticalSection(mutex);

#endif
}

/**
 * Blocks until the calling thread is granted ownership of the mutex.
 * Locking an already owned mutex is undefined behaviour.
 */
static inline void _gfx_mutex_lock(_GFXMutex* mutex)
{
#if defined (GFX_UNIX)
	pthread_mutex_lock(mutex);

#elif defined (GFX_WIN32)
	EnterCriticalSection(mutex);

#endif
}

/**
 * Try to get ownership of the mutex without blocking.
 * @return Non-zero if ownership was granted.
 */
static inline bool _gfx_mutex_try_lock(_GFXMutex* mutex)
{
#if defined (GFX_UNIX)
	return !pthread_mutex_trylock(mutex);

#elif defined (GFX_WIN32)
	return TryEnterCriticalSection(mutex);

#endif
}

/**
 * Releases the mutex, making it available to other threads.
 * Unlocking an already unlocked mutex is undefined behaviour.
 */
static inline void _gfx_mutex_unlock(_GFXMutex* mutex)
{
#if defined (GFX_UNIX)
	pthread_mutex_unlock(mutex);

#elif defined (GFX_WIN32)
	LeaveCriticalSection(mutex);

#endif
}


#endif
