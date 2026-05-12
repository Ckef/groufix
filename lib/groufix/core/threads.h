/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_THREADS_H_
#define GFX_CORE_THREADS_H_

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
	typedef pthread_key_t GFXThreadKey_;
#elif defined (GFX_WIN32)
	typedef DWORD         GFXThreadKey_;
#endif


/**
 * Mutual exclusion lock.
 */
#if defined (GFX_UNIX)
	typedef pthread_mutex_t GFXMutex_;
#elif defined (GFX_WIN32)
	typedef SRWLOCK         GFXMutex_;
#endif


/**
 * Readers/Writer lock.
 */
#if defined (GFX_UNIX)
	typedef pthread_rwlock_t GFXRWLock_;
#elif defined (GFX_WIN32)
	typedef SRWLOCK          GFXRWLock_;
#endif


/****************************
 * Thread local data key.
 ****************************/

/**
 * Initializes a thread local data key.
 * The object pointed to by key cannot be moved or copied!
 * @return Non-zero on success.
 */
static inline bool gfx_thread_key_init_(GFXThreadKey_* key)
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
static inline void gfx_thread_key_clear_(GFXThreadKey_ key)
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
static inline bool gfx_thread_key_set_(GFXThreadKey_ key, const void* value)
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
static inline void* gfx_thread_key_get_(GFXThreadKey_ key)
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
static inline bool gfx_mutex_init_(GFXMutex_* mutex)
{
#if defined (GFX_UNIX)
	return !pthread_mutex_init(mutex, NULL);

#elif defined (GFX_WIN32)
	InitializeSRWLock(mutex);
	return 1;

#endif
}

/**
 * Clears a mutex.
 * Clearing a locked mutex is undefined behaviour.
 */
static inline void gfx_mutex_clear_(GFXMutex_* mutex)
{
#if defined (GFX_UNIX)
	pthread_mutex_destroy(mutex);

#elif defined (GFX_WIN32)
	/* No-op */

#endif
}

/**
 * Blocks until the calling thread is granted ownership of the mutex.
 * Locking an already owned mutex is undefined behaviour.
 *
 * Non-recursive!
 */
static inline void gfx_mutex_lock_(GFXMutex_* mutex)
{
#if defined (GFX_UNIX)
	pthread_mutex_lock(mutex);

#elif defined (GFX_WIN32)
	AcquireSRWLockExclusive(mutex);

#endif
}

/**
 * Try to get ownership of the mutex without blocking.
 * @return Non-zero if ownership was granted.
 *
 * Non-recursive!
 */
static inline bool gfx_mutex_try_lock_(GFXMutex_* mutex)
{
#if defined (GFX_UNIX)
	return !pthread_mutex_trylock(mutex);

#elif defined (GFX_WIN32)
	return TryAcquireSRWLockExclusive(mutex);

#endif
}

/**
 * Releases the mutex, making it available to other threads.
 * Unlocking an already unlocked mutex is undefined behaviour.
 */
static inline void gfx_mutex_unlock_(GFXMutex_* mutex)
{
#if defined (GFX_UNIX)
	pthread_mutex_unlock(mutex);

#elif defined (GFX_WIN32)
	ReleaseSRWLockExclusive(mutex);

#endif
}


/****************************
 * Readers/Writer lock.
 ****************************/

/**
 * Initializes a readers/writer lock.
 * The object pointed to by lock cannot be moved or copied!
 * @return Non-zero on success.
 */
static inline bool gfx_rwlock_init_(GFXRWLock_* lock)
{
#if defined (GFX_UNIX)
	return !pthread_rwlock_init(lock, NULL);

#elif defined (GFX_WIN32)
	InitializeSRWLock(lock);
	return 1;

#endif
}

/**
 * Clears a readers/writer lock.
 * Clearing a locked readers/writer lock is undefined behaviour.
 */
static inline void gfx_rwlock_clear_(GFXRWLock_* lock)
{
#if defined (GFX_UNIX)
	pthread_rwlock_destroy(lock);

#elif defined (GFX_WIN32)
	/* No-op */

#endif
}

/**
 * Blocks until the calling thread is granted reader ownership of the lock.
 * Locking an already owned readers/writer lock is undefined behaviour.
 *
 * Non-recursive!
 */
static inline void gfx_rwlock_rlock_(GFXRWLock_* lock)
{
#if defined (GFX_UNIX)
	pthread_rwlock_rdlock(lock);

#elif defined (GFX_WIN32)
	AcquireSRWLockShared(lock);

#endif
}

/**
 * Try to get reader ownership of the lock without blocking.
 * @return Non-zero if ownership was granted.
 *
 * Non-recursive!
 */
static inline bool gfx_rwlock_try_rlock_(GFXRWLock_* lock)
{
#if defined (GFX_UNIX)
	return !pthread_rwlock_tryrdlock(lock);

#elif defined (GFX_WIN32)
	return TryAcquireSRWLockShared(lock);

#endif
}

/**
 * Releases the reader lock, making it available to writers.
 * Unlocking an already unlocked readers/writer lock is undefined behaviour.
 */
static inline void gfx_rwlock_runlock_(GFXRWLock_* lock)
{
#if defined (GFX_UNIX)
	pthread_rwlock_unlock(lock);

#elif defined (GFX_WIN32)
	ReleaseSRWLockShared(lock);

#endif
}

/**
 * Blocks until the calling thread is granted writer ownership of the lock.
 * Locking an already owned readers/writer lock is undefined behaviour.
 *
 * Non-recursive!
 */
static inline void gfx_rwlock_wlock_(GFXRWLock_* lock)
{
#if defined (GFX_UNIX)
	pthread_rwlock_wrlock(lock);

#elif defined (GFX_WIN32)
	AcquireSRWLockExclusive(lock);

#endif
}

/**
 * Try to get writer ownership of the lock without blocking.
 * @return Non-zero if ownership was granted.
 *
 * Non-recursive!
 */
static inline bool gfx_rwlock_try_wlock_(GFXRWLock_* lock)
{
#if defined (GFX_UNIX)
	return !pthread_rwlock_trywrlock(lock);

#elif defined (GFX_WIN32)
	return TryAcquireSRWLockExclusive(lock);

#endif
}

/**
 * Releases the writer lock, making it available to other readers/writers.
 * Unlocking an already unlocked readers/writer lock is undefined behaviour.
 */
static inline void gfx_rwlock_wunlock_(GFXRWLock_* lock)
{
#if defined (GFX_UNIX)
	pthread_rwlock_unlock(lock);

#elif defined (GFX_WIN32)
	ReleaseSRWLockExclusive(lock);

#endif
}


#endif
