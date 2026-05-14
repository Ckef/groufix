/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_MEM_H_
#define GFX_CORE_MEM_H_

#include "groufix/containers/io.h"
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
typedef struct GFXHashKey_
{
	size_t len;
	char bytes[];

} GFXHashKey_;


/**
 * Hashable key builder.
 */
typedef struct GFXHashBuilder_
{
	GFXVec out;

} GFXHashBuilder_;


/**
 * Returns the total size (including key header) of a hash key in bytes.
 */
static inline size_t gfx_hash_size_(const GFXHashKey_* key)
{
	return sizeof(GFXHashKey_) + sizeof(char) * key->len;
}

/**
 * Pushes data on top of a hash key builder, extending its key.
 * @return A pointer to the pushed data, NULL on failure.
 */
static inline void* gfx_hash_builder_push_(GFXHashBuilder_* b, size_t s, const void* d)
{
	return !gfx_vec_push(&b->out, s, d) ? NULL : gfx_vec_at(&b->out, b->out.size - s);
}

/**
 * GFXMap key comparison function,
 * l and r are of type GFXHashKey_*, assumes packed data.
 */
int gfx_hash_cmp_(const void* l, const void* r);

/**
 * MurmurHash3 (32 bits) implementation as GFXMap hash function,
 * key is of type GFXHashKey_*.
 */
uint64_t gfx_hash_murmur3_(const void* key);

/**
 * Initializes a hash key builder.
 * Needs to eventually be 'cleared' with a call to gfx_hash_builder_get_().
 * @param builder Cannot be NULL.
 * @return Non-zero on success.
 */
bool gfx_hash_builder_(GFXHashBuilder_* builder);

/**
 * Claims ownership over the memory allocated by a hash key builder.
 * The hash key builder itself is invalidated.
 * @param builder Cannot be NULL.
 * @return Allocated key data, must call free().
 */
GFXHashKey_* gfx_hash_builder_get_(GFXHashBuilder_* builder);


/****************************
 * Vulkan memory management.
 ****************************/

/**
 * Memory block (i.e. Vulkan memory object to be subdivided).
 */
typedef struct GFXMemBlock_
{
	GFXListNode  list; // Base-type.
	uint32_t     type; // Vulkan memory type index.
	VkDeviceSize size;


	// Related memory nodes.
	struct
	{
		GFXTree free; // Stores { VkDeviceSize, VkDeviceSize } : GFXMemNode_.
		GFXList list; // References GFXMemNode_ | GFXMemAlloc_.

	} nodes;


	// Mapped memory pointer.
	struct
	{
		uintmax_t refs;
		void*     ptr; // NULL if not mapped.
		GFXMutex_ lock;

	} map;


	// Vulkan fields.
	struct
	{
		VkDeviceMemory memory;

	} vk;

} GFXMemBlock_;


/**
 * Memory node, linked to neighbours in actual memory.
 */
typedef struct GFXMemNode_
{
	GFXListNode list; // Base-type.

	bool free; // isa GFXMemAlloc_ if zero, isa search tree node if non-zero.

} GFXMemNode_;


/**
 * Allocated memory node (contains everything necessary for use).
 */
typedef struct GFXMemAlloc_
{
	GFXMemNode_   node; // Base-type.
	GFXMemBlock_* block;

	VkDeviceSize  size;
	VkDeviceSize  offset;

	VkMemoryPropertyFlags flags; // Actual used flags.

	// For granularity constraints.
	bool linear;


	// Vulkan fields.
	struct
	{
		VkDeviceMemory memory; // Redundant for locality.

	} vk;

} GFXMemAlloc_;


/**
 * Vulkan memory allocator definition.
 */
typedef struct GFXAllocator_
{
	GFXDevice_*  device; // For memory property queries.
	GFXContext_* context;

	GFXList free; // References GFXMemBlock_.
	GFXList full; // References GFXMemBlock_.

	// Constant, queried once.
	VkDeviceSize granularity;

} GFXAllocator_;


/**
 * Locks Vulkan memory so we can bind a buffer/image to it.
 */
static inline void gfx_mem_lock_(GFXMemAlloc_* mem)
{
	gfx_mutex_lock_(&mem->block->map.lock);
}

/**
 * Unlocks Vulkan memory locked by gfx_mem_lock_.
 * MUST be called before this thread tries to map any memory!
 */
static inline void gfx_mem_unlock_(GFXMemAlloc_* mem)
{
	gfx_mutex_unlock_(&mem->block->map.lock);
}

/**
 * Initializes an allocator.
 * @param alloc  Cannot be NULL.
 * @param device Cannot be NULL.
 *
 * gfx_device_init_context_ must have returned successfully at least once
 * for the given device.
 */
void gfx_allocator_init_(GFXAllocator_* alloc, GFXDevice_* device);

/**
 * Clears an allocator, freeing all allocations.
 * @param alloc Cannot be NULL.
 */
void gfx_allocator_clear_(GFXAllocator_* alloc);

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
bool gfx_alloc_(GFXAllocator_* alloc, GFXMemAlloc_* mem, bool linear,
                VkMemoryPropertyFlags required, VkMemoryPropertyFlags optimal,
                VkMemoryRequirements reqs);

/**
 * Allocate some 'dedicated' Vulkan memory,
 * meaning it will not be sub-allocated from a larger memory block.
 * @see gfx_alloc_.
 *
 * To allocate Vulkan dedicated (for a Vulkan buffer or image) memory,
 * either a buffer _OR_ image can be passed.
 */
bool gfx_allocd_(GFXAllocator_* alloc, GFXMemAlloc_* mem,
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
void gfx_free_(GFXAllocator_* alloc, GFXMemAlloc_* mem);

/**
 * Maps some Vulkan memory to a host virtual address pointer, this can be
 * called multiple times, the actual memory object is reference counted.
 * @param alloc Cannot be NULL.
 * @param mem   Cannot be NULL, must be allocated from alloc.
 * @return NULL on failure.
 *
 * This function is reentrant!
 * However, cannot bind buffer/image during this call,
 * MUST use gfx_mem_lock_ and gfx_mem_unlock_!
 *
 * The given object must be allocated with VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT.
 */
void* gfx_map_(GFXAllocator_* alloc, GFXMemAlloc_* mem);

/**
 * Unmaps Vulkan memory, invalidating a mapped pointer.
 * Must be called exactly once for every successful call to gfx_map_.
 * @param alloc Cannot be NULL.
 * @param mem   Cannot be NULL, must be allocated from alloc.
 *
 * @see gfx_map_ for concurrency rules!
 */
void gfx_unmap_(GFXAllocator_* alloc, GFXMemAlloc_* mem);


/****************************
 * Vulkan object cache.
 ****************************/

/**
 * Cached element (i.e. cachable Vulkan object).
 */
typedef struct GFXCacheElem_
{
	// Input structure type.
	VkStructureType type;


	// Vulkan fields.
	struct
	{
		VkDescriptorUpdateTemplate template;

		union {
			VkDescriptorSetLayout setLayout;
			VkPipelineLayout      layout;
			VkSampler             sampler;
			VkRenderPass          pass;
			VkPipeline            pipeline;
		};

	} vk;

} GFXCacheElem_;


/**
 * Vulkan object cache definition.
 */
typedef struct GFXCache_
{
	GFXContext_* context;

	GFXMap simple;    // Stores GFXHashKey_ : GFXCacheElem_.
	GFXMap immutable; // Stores GFXHashKey_ : GFXCacheElem_.
	GFXMap mutable;   // Stores GFXHashKey_ : GFXCacheElem_.

	GFXMutex_  simpleLock;
	GFXRWLock_ lookupLock;
	GFXMutex_  createLock;

	size_t templateStride;


	// Vulkan fields.
	struct
	{
		VkPhysicalDevice device; // For pipeline cache validation.
		VkPipelineCache  cache;

	} vk;

} GFXCache_;


/**
 * Initializes a cache.
 * @param cache          Cannot be NULL.
 * @param device         Cannot be NULL.
 * @param templateStride Must be > 0.
 * @return Non-zero on success.
 *
 * gfx_device_init_context_ must have returned successfully at least once
 * for the given device.
 */
bool gfx_cache_init_(GFXCache_* cache, GFXDevice_* device, size_t templateStride);

/**
 * Clears a cache, destroying all objects.
 * @param cache Cannot be NULL.
 */
void gfx_cache_clear_(GFXCache_* cache);

/**
 * Flushes all elements in the mutable cache to the immutable cache.
 * @param cache Cannot be NULL.
 * @return Non-zero on success.
 *
 * Not thread-safe at all.
 * @see gfx_cache_get_ for the only exception.
 */
bool gfx_cache_flush_(GFXCache_* cache);

/**
 * Retrieves an element from the cache.
 * Input is a Vk*CreateInfo struct with replace handles for non-hashable fields.
 * @param cache      Cannot be NULL.
 * @param createInfo A pointer to a Vk*CreateInfo struct, cannot be NULL.
 * @param handles    Must match the non-hashable field count of createInfo.
 * @return NULL on failure.
 *
 * This function is reentrant,
 * However, cannot run concurrently with other calls.
 *
 * Except when anything other than a Vk*PipelineCreateInfo struct is given,
 * then it can run concurrently with gfx_cache_flush_ and gfx_cache_warmup_.
 *
 * The following handles must be passed for each info struct,
 * fields ignored by Vulkan must still be set to 'empty' for proper caching!
 * Listed handles are given in order:
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
GFXCacheElem_* gfx_cache_get_(GFXCache_* cache,
                              const VkStructureType* createInfo,
                              const void** handles);

/**
 * Warms up the immutable cache (i.e. inserts a pipeline in it). Input is a
 * Vk*PipelineCreateInfo struct with replace handles for non-hashable fields.
 * @param cache      Cannot be NULL.
 * @param createInfo A pointer to a Vk*PipelineCreateInfo struct, cannot be NULL.
 * @param handles    Must match the non-hashable field count of createInfo.
 * @return Non-zero on success.
 *
 * This function is reentrant,
 * However, cannot run concurrently with other calls.
 * @see gfx_cache_get_ for the only exception.
 * @see gfx_cache_get_ for the handles that must be passed.
 */
bool gfx_cache_warmup_(GFXCache_* cache,
                       const VkStructureType* createInfo,
                       const void** handles);

/**
 * Loads groufix pipeline cache data, merging it into the current cache.
 * @param cache Cannot be NULL.
 * @param src   Source stream, cannot be NULL.
 * @return Non-zero on success.
 *
 * Not thread-safe at all.
 */
bool gfx_cache_load_(GFXCache_* cache, const GFXReader* src);

/**
 * Stores the current groufix pipeline cache data.
 * @param cache Cannot be NULL.
 * @param dst   Destination stream, cannot be NULL.
 * @return Non-zero on success.
 *
 * Not thread-safe at all.
 */
bool gfx_cache_store_(GFXCache_* cache, const GFXWriter* dst);


/****************************
 * Vulkan descriptor management.
 ****************************/

/**
 * Pool descriptor block (i.e. Vulkan descriptor pool).
 */
typedef struct GFXPoolBlock_
{
	GFXListNode list;  // Base-type, undefined if claimed by subordinate.
	GFXList     elems; // References GFXPoolElem_.
	bool        full;

	// #in-use descriptor sets (i.e. not-recycled).
	atomic_uint_fast32_t sets;


	// Vulkan fields.
	struct
	{
		VkDescriptorPool pool;

	} vk;

} GFXPoolBlock_;


/**
 * Pooled element (i.e. Vulkan descriptor set).
 */
typedef struct GFXPoolElem_
{
	GFXListNode    list; // Base-type.
	GFXPoolBlock_* block;

	// #flushes left to recycle.
	atomic_uint flushes;


	// Vulkan fields.
	struct
	{
		VkDescriptorSet set;

	} vk;

} GFXPoolElem_;


/**
 * Pool subordinate (i.e. thread handle).
 */
typedef struct GFXPoolSub_
{
	GFXListNode    list;    // Base-type.
	GFXMap         mutable; // Stores GFXHashKey_ : GFXPoolElem_.
	GFXPoolBlock_* block;   // Currently claimed for new allocations.

} GFXPoolSub_;


/**
 * Vulkan descriptor allocator definition.
 */
typedef struct GFXPool_
{
	GFXContext_* context;

	GFXList free; // References GFXPoolBlock_.
	GFXList full; // References GFXPoolBlock_.
	GFXList subs; // References GFXPoolSub_.

	GFXMap immutable; // Stores GFXHashKey_ : GFXPoolElem_.
	GFXMap stale;     // Stores GFXHashKey_ : GFXPoolElem_.
	GFXMap recycled;  // Stores GFXHashKey_ : GFXPoolElem_.

	GFXMutex_ subLock; // For claiming blocks.
	GFXMutex_ recLock; // For recycling.

	unsigned int flushes;

} GFXPool_;


/**
 * Initializes a pool.
 * @param pool    Cannot be NULL.
 * @param device  Cannot be NULL.
 * @param flushes Number of flushes after which a descriptor set is recycled.
 * @return Non-zero on success.
 *
 * gfx_device_init_context_ must have returned successfully at least once
 * for the given device.
 */
bool gfx_pool_init_(GFXPool_* pool, GFXDevice_* device, unsigned int flushes);

/**
 * Clears a pool, also clearing all subordinates.
 * @param pool Cannot be NULL.
 */
void gfx_pool_clear_(GFXPool_* pool);

/**
 * Flushes all subordinate descriptor caches to the immutable pool cache,
 * making them visible to all other subordinates.
 * Then recycles descriptor sets that were last retrieved #flushes ago.
 * @param pool Cannot be NULL.
 * @return Non-zero on success, can be partially flushed on failure.
 *
 * Not thread-safe at all.
 */
bool gfx_pool_flush_(GFXPool_* pool);

/**
 * Resets all descriptor pools, freeing all descriptor sets,
 * WITHOUT releasing pool memory!
 * Useful for when used keys for descriptor sets have been invalidated,
 * or many descriptor sets are recycled but never used again.
 * @param pool Cannot be NULL.
 *
 * Not thread-safe at all.
 */
void gfx_pool_reset_(GFXPool_* pool);

/**
 * Initializes a new subordinate of the pool.
 * The object pointed to by sub cannot be moved or copied!
 * @param pool Cannot be NULL.
 * @param sub  Cannot be NULL.
 *
 * Not thread-safe at all.
 */
void gfx_pool_sub_(GFXPool_* pool, GFXPoolSub_* sub);

/**
 * Clears ('undos') a subordinate.
 * @param pool Cannot be NULL.
 * @param sub  Cannot be NULL, must be initialized from pool.
 *
 * Not thread-safe at all.
 */
void gfx_pool_unsub_(GFXPool_* pool, GFXPoolSub_* sub);

/**
 * Forces recycling of all matching Vulkan descriptor sets.
 * Useful for when specific keys have been invalidated.
 * @param pool Cannot be NULL.
 * @param key  Matched against the keys passed in gfx_pool_get_.
 * @param flushes Number of flushes after which the descriptor set is recycled.
 *
 * Not thread-safe at all, unlike gfx_pool_get_!
 * Note: when a set is recycled, its associated block might be freed if empty!
 */
void gfx_pool_recycle_(GFXPool_* pool,
                       const GFXHashKey_* key, unsigned int flushes);

/**
 * Retrieves, allocates or recycles a Vulkan descriptor set from the pool.
 * @param pool      Cannot be NULL.
 * @param sub       Cannot be NULL.
 * @param setLayout Must be a descriptor set layout returned by gfx_cache_get_.
 * @param key       Must uniquely identify the given layout + descriptors.
 * @param update    Template-formatted data to update the descriptors with.
 * @return NULL on failure.
 *
 * Thread-safe with respect to other subordinates.
 * However, can never run concurrently with other pool functions.
 *
 * The first bytes of key must be setLayout, pushed as a GFXCacheElem_*.
 * Naturally key must at least be of size sizeof(GFXCacheElem_*), the total
 * size must be fixed for a given descriptor set layout.
 *
 * update must point to the first VkDescriptorImageInfo, VkDescriptorBufferInfo
 * or VkBufferView structure, with `templateStride` bytes inbetween consecutive
 * structures, as defined by the GFXCache_ that setLayout was allocated from.
 */
GFXPoolElem_* gfx_pool_get_(GFXPool_* pool, GFXPoolSub_* sub,
                            const GFXCacheElem_* setLayout,
                            const GFXHashKey_* key, const void* update);


#endif
