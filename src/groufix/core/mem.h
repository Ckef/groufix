/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef _GFX_CORE_MEM_H
#define _GFX_CORE_MEM_H

#include "groufix/containers/list.h"
#include "groufix/containers/map.h"
#include "groufix/containers/tree.h"
#include "groufix/containers/vec.h"
#include "groufix/core.h"


/****************************
 * Hashable key building & hashing.
 ****************************/

/**
 * Hashable key definition.
 */
typedef struct _GFXHashKey
{
	size_t len;
	char bytes[];

} _GFXHashKey;


/**
 * Hashable key builder.
 */
typedef struct _GFXHashBuilder
{
	GFXVec out;

} _GFXHashBuilder;


/**
 * Returns the size of a hash key in bytes.
 */
#define _GFX_HASH_KEY_SIZE(key) \
	(sizeof(_GFXHashKey) + sizeof(char) * (key)->len)

/**
 * Pushes data on top of a hash key builder, extending its key.
 * @return Non-zero on success.
 */
#define _GFX_HASH_BUILDER_PUSH(builder, size, data) \
	gfx_vec_push(&(builder)->out, size, data)


/**
 * Initializes a hash key builder.
 * Needs to eventually be 'cleared' with a call to _gfx_hash_builder_get().
 * @param builder Cannot be NULL.
 * @return Non-zero on success.
 */
int _gfx_hash_builder(_GFXHashBuilder* builder);

/**
 * Claims ownership over the memory allocated by a hash key builder.
 * The hash key builder itself is invalidated.
 * @param builder Cannot be NULL.
 * @return Allocated key data, must call free().
 */
_GFXHashKey* _gfx_hash_builder_get(_GFXHashBuilder* builder);

/**
 * GFXMap key comparison function,
 * l and r are of type _GFXHashKey*, assumes packed data.
 */
int _gfx_hash_cmp(const void* l, const void* r);

/**
 * MurmurHash3 (32 bits) implementation as GFXMap hash function,
 * key is of type _GFXHashKey*.
 */
uint64_t _gfx_hash_murmur3(const void* key);


/****************************
 * Vulkan memory management.
 ****************************/

/**
 * Memory block (i.e. Vulkan memory object to be subdivided).
 */
typedef struct _GFXMemBlock
{
	GFXListNode  list; // Base-type.
	uint32_t     type; // Vulkan memory type index.
	VkDeviceSize size;


	// Related memory nodes.
	struct
	{
		GFXTree free; // Stores { VkDeviceSize, VkDeviceSize } : _GFXMemNode.
		GFXList list; // References _GFXMemNode | _GFXMemAlloc.

	} nodes;


	// Mapped memory pointer.
	struct
	{
		uintmax_t refs;
		void*     ptr; // NULL if not mapped.
		_GFXMutex lock;

	} map;


	// Vulkan fields.
	struct
	{
		VkDeviceMemory memory;

	} vk;

} _GFXMemBlock;


/**
 * Memory node, linked to neighbours in actual memory.
 */
typedef struct _GFXMemNode
{
	GFXListNode list; // Base-type.

	int free; // isa _GFXMemAlloc if zero, isa search tree node if non-zero.

} _GFXMemNode;


/**
 * Allocated memory node (contains everything necessary for use).
 */
typedef struct _GFXMemAlloc
{
	_GFXMemNode   node; // Base-type.
	_GFXMemBlock* block;

	VkDeviceSize  size;
	VkDeviceSize  offset;

	VkMemoryPropertyFlags flags; // Actual used flags.

	// For granularity constraints.
	int linear;


	// Vulkan fields.
	struct
	{
		VkDeviceMemory memory; // Redundant for locality.

	} vk;

} _GFXMemAlloc;


/**
 * Vulkan memory allocator definition.
 */
typedef struct _GFXAllocator
{
	_GFXContext* context;

	GFXList free;   // References _GFXMemBlock.
	GFXList allocd; // References _GFXMemBlock.

	// Constant, queried once.
	VkDeviceSize granularity;


	// Vulkan fields.
	struct
	{
		VkPhysicalDeviceMemoryProperties properties; // Queried once.

	} vk;

} _GFXAllocator;


/**
 * Initializes an allocator.
 * @param alloc  Cannot be NULL.
 * @param device Cannot be NULL.
 *
 * _gfx_device_init_context must have returned successfully at least once
 * for the given device.
 */
void _gfx_allocator_init(_GFXAllocator* alloc, _GFXDevice* device);

/**
 * Clears an allocator, freeing all allocations.
 * @param alloc Cannot be NULL.
 */
void _gfx_allocator_clear(_GFXAllocator* alloc);

/**
 * Allocate some Vulkan memory.
 * The object pointed to by mem cannot be moved or copied!
 * @param alloc    Cannot be NULL.
 * @param mem      Cannot be NULL.
 * @param linear   Non-zero for a linear resource, 0 for a non-linear one.
 * @param required Required flags, if they cannot be satisfied, it will fail.
 * @param optimal  Optimal, i.e. preferred flags.
 * @param reqs     Must be valid (size > 0, align = a power of two, bits != 0).
 * @return Non-zero on success.
 *
 * Not thread-safe at all.
 */
int _gfx_alloc(_GFXAllocator* alloc, _GFXMemAlloc* mem, int linear,
               VkMemoryPropertyFlags required, VkMemoryPropertyFlags optimal,
               VkMemoryRequirements reqs);

/**
 * Allocate some 'dedicated' Vulkan memory,
 * meaning it will not be sub-allocated from a larger memory block.
 * @see _gfx_alloc.
 *
 * To allocate Vulkan dedicated (for a Vulkan buffer or image) memory,
 * either a buffer _OR_ image can be passed.
 */
int _gfx_allocd(_GFXAllocator* alloc, _GFXMemAlloc* mem,
                VkMemoryPropertyFlags required, VkMemoryPropertyFlags optimal,
                VkMemoryRequirements reqs,
                VkBuffer buffer, VkImage image);

/**
 * Free some Vulkan memory.
 * @param alloc Cannot be NULL.
 * @param mem   Cannot be NULL, must be allocated from alloc.
 *
 * Not thread-safe at all.
 * The content of mem is invalidated after this call.
 * Silently warns when not able to modify the free structure appropriately.
 */
void _gfx_free(_GFXAllocator* alloc, _GFXMemAlloc* mem);

/**
 * Maps some Vulkan memory to a host virtual address pointer, this can be
 * called multiple times, the actual memory object is reference counted.
 * @param alloc Cannot be NULL.
 * @param mem   Cannot be NULL, must be allocated from alloc.
 * @return NULL on failure.
 *
 * This function is reentrant!
 * The given object must be allocated with VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT.
 */
void* _gfx_map(_GFXAllocator* alloc, _GFXMemAlloc* mem);

/**
 * Unmaps Vulkan memory, invalidating a mapped pointer.
 * Must be called exactly once for every successful call to _gfx_map.
 * @param alloc Cannot be NULL.
 * @param mem   Cannot be NULL, must be allocated from alloc.
 *
 * This function is reentrant!
 */
void _gfx_unmap(_GFXAllocator* alloc, _GFXMemAlloc* mem);


/****************************
 * Vulkan object cache.
 ****************************/

/**
 * Cached element (i.e. cachable Vulkan object).
 */
typedef struct _GFXCacheElem
{
	// Input structure type.
	VkStructureType type;


	// Vulkan field (isa union!).
	union
	{
		VkDescriptorSetLayout setLayout;
		VkPipelineLayout      layout;
		VkSampler             sampler;
		VkRenderPass          pass;
		VkPipeline            pipeline;

	} vk;

} _GFXCacheElem;


/**
 * Vulkan object cache definition.
 */
typedef struct _GFXCache
{
	_GFXContext* context;

	GFXMap immutable; // Stores _GFXHashKey : _GFXCacheElem.
	GFXMap mutable;   // Stores _GFXHashKey : _GFXCacheElem.

	_GFXMutex lookupLock;
	_GFXMutex createLock;


	// Vulkan fields.
	struct
	{
		VkPipelineCache cache;

	} vk;

} _GFXCache;


// TODO: Add functions to load/store/merge the Vulkan pipeline cache.

/**
 * Initializes a cache.
 * @param cache  Cannot be NULL.
 * @param device Cannot be NULL.
 * @return Non-zero on success.
 *
 * _gfx_device_init_context must have returned successfully at least once
 * for the given device.
 */
int _gfx_cache_init(_GFXCache* cache, _GFXDevice* device);

/**
 * Clears a cache, destroying all objects.
 * @param cache Cannot be NULL.
 */
void _gfx_cache_clear(_GFXCache* cache);

/**
 * Flushes all elements in the mutable cache to the immutable cache.
 * @param cache Cannot be NULL.
 * @return Non-zero on success.
 *
 * Not thread-safe at all.
 */
int _gfx_cache_flush(_GFXCache* cache);

/**
 * Warms up the immutable cache (i.e. inserts an element in it).
 * Input is a Vk*CreateInfo struct with replace handles for non-hashable fields.
 * @param cache      Cannot be NULL.
 * @param createInfo A pointer to a Vk*CreateInfo struct, cannot be NULL.
 * @param handles    Must match the non-hashable field count of createInfo.
 * @return Non-zero on success.
 *
 * This function is reentrant!
 * However, cannot run concurrently with _gfx_cache_get (or other calls).
 *
 * The following Vk*CreateInfo structs can be passed,
 * fields ignored by Vulkan must still be set to 'empty' for proper caching!
 * Listed is the required number of handles to be passed, in order:
 *
 *  VkDescriptorSetLayoutCreateInfo:
 *   1 for each immutable sampler.
 *
 *  VkPipelineLayoutCreateInfo:
 *   1 for each descriptor set layout.
 *
 *  VkSamplerCreateInfo:
 *   None.
 *
 *  VkRenderPassCreateInfo:
 *   None.
 *
 *  VkGraphicsPipelineCreateInfo:
 *   1 for each shader module.
 *   1 for the pipeline layout.
 *   1 for the compatible render pass (compatibility is not resolved!).
 *
 *  VkComputePipelineCreateInfo:
 *   1 for the shader module.
 *   1 for the pipeline layout.
 */
int _gfx_cache_warmup(_GFXCache* cache,
                      const VkStructureType* createInfo,
                      const void** handles);

/**
 * Retrieves an element from the cache.
 * If none found, inserts a new element in the mutable cache.
 * @see _gfx_cache_warmup.
 * @return NULL on failure.
 *
 * This function is reentrant!
 * However, cannot run concurrently with _gfx_cache_warmup (or other calls).
 */
_GFXCacheElem* _gfx_cache_get(_GFXCache* cache,
                              const VkStructureType* createInfo,
                              const void** handles);


/****************************
 * Vulkan descriptor management.
 ****************************/

/**
 * Pool descriptor block (i.e. Vulkan descriptor pool).
 */
typedef struct _GFXPoolBlock
{
	GFXListNode list; // Base-type.
	uint32_t    sets; // #in-use sets.


	// Vulkan fields.
	struct
	{
		VkDescriptorPool pool;

	} vk;

} _GFXPoolBlock;


/**
 * Pooled element (i.e. Vulkan descriptor set).
 */
typedef struct _GFXPoolElem
{
	_GFXPoolBlock* block;
	unsigned int   unused; // #flushes unused.


	// Vulkan fields.
	struct
	{
		VkDescriptorSet set;

	} vk;

} _GFXPoolElem;


/**
 * Pool subordinate (i.e. thread handle).
 */
typedef struct _GFXPoolSub
{
	GFXListNode    list;    // Base-type.
	GFXMap         mutable; // Stores _GFXHashKey : _GFXPoolElem.
	_GFXPoolBlock* block;   // Currently used descriptor block.

} _GFXPoolSub;


/**
 * Vulkan descriptor allocator definition.
 */
typedef struct _GFXPool
{
	_GFXContext* context;

	GFXList free;   // References _GFXPoolBlock.
	GFXList allocd; // References _GFXPoolBlock.
	GFXList subs;   // References _GFXPoolSub.

	GFXMap immutable; // Stores _GFXHashKey : _GFXPoolElem.
	GFXMap recycled;  // Stores _GFXHashKey : _GFXPoolElem.

	_GFXMutex subLock; // For claiming blocks.
	_GFXMutex recLock; // For recycling.

} _GFXPool;


/**
 * Initializes a pool.
 * @param pool   Cannot be NULL.
 * @param device Cannot be NULL.
 * @return Non-zero on success.
 *
 * _gfx_device_init_context must have returned successfully at least once
 * for the given device.
 */
int _gfx_pool_init(_GFXPool* pool, _GFXDevice* device);

/**
 * Clears a pool, freeing all blocks and descriptors.
 * @param pool Cannot be NULL.
 */
void _gfx_pool_clear(_GFXPool* pool);


#endif
