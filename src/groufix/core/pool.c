/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */

#include "groufix/core/mem.h"
#include <assert.h>


/****************************/
int _gfx_pool_init(_GFXPool* pool, _GFXDevice* device, unsigned int flushes)
{
	assert(pool != NULL);
	assert(device != NULL);
	assert(device->context != NULL);

	pool->context = device->context;
	pool->flushes = flushes;

	// Initialize the locks.
	if (!_gfx_mutex_init(&pool->subLock))
		return 0;

	if (!_gfx_mutex_init(&pool->recLock))
	{
		_gfx_mutex_clear(&pool->subLock);
		return 0;
	}

	// Initialize all the lists & hashtables.
	gfx_list_init(&pool->free);
	gfx_list_init(&pool->allocd);
	gfx_list_init(&pool->subs);

	// Take the largest alignment of the key and element types.
	const size_t align =
		GFX_MAX(_Alignof(_GFXHashKey), _Alignof(_GFXPoolElem));

	gfx_map_init(&pool->immutable,
		sizeof(_GFXPoolElem), align, _gfx_hash_murmur3, _gfx_hash_cmp);
	gfx_map_init(&pool->recycled,
		sizeof(_GFXPoolElem), align, _gfx_hash_murmur3, _gfx_hash_cmp);

	return 1;
}

/****************************/
void _gfx_pool_clear(_GFXPool* pool)
{
	assert(pool != NULL);

	// Clear all the things.
	gfx_map_clear(&pool->immutable);
	gfx_map_clear(&pool->recycled);

	gfx_list_clear(&pool->free);
	gfx_list_clear(&pool->allocd);
	gfx_list_clear(&pool->subs);

	_gfx_mutex_clear(&pool->recLock);
	_gfx_mutex_clear(&pool->subLock);
}

/****************************/
int _gfx_pool_flush(_GFXPool* pool)
{
	assert(pool != NULL);

	// TODO: Implement.

	return 0;
}

/****************************/
void _gfx_pool_reset(_GFXPool* pool)
{
	assert(pool != NULL);

	// TODO: Implement.
}

/****************************/
_GFXPoolSub* _gfx_pool_sub(_GFXPool* pool)
{
	assert(pool != NULL);

	// TODO: Implement.

	return NULL;
}

/****************************/
void _gfx_pool_unsub(_GFXPool* pool, _GFXPoolSub* sub)
{
	assert(pool != NULL);
	assert(sub != NULL);

	// TODO: Implement.
}

/****************************/
_GFXPoolElem* _gfx_pool_get(_GFXPool* pool, _GFXPoolSub* sub,
                            const _GFXCacheElem* setLayout,
                            const _GFXHashKey* key,
                            const void* update)
{
	assert(pool != NULL);
	assert(sub != NULL);
	assert(setLayout != NULL);
	assert(setLayout->type == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
	assert(key != NULL);
	assert(update != NULL);

	// TODO: Implement.

	return NULL;
}
