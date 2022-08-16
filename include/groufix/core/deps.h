/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_DEPS_H
#define GFX_CORE_DEPS_H

#include "groufix/core/device.h"
#include "groufix/core/refs.h"
#include "groufix/core/shader.h"
#include "groufix/def.h"


/**
 * Dependency access mask.
 */
typedef enum GFXAccessMask
{
	GFX_ACCESS_VERTEX_READ   = 0x000001,
	GFX_ACCESS_INDEX_READ    = 0x000002,
	GFX_ACCESS_UNIFORM_READ  = 0x000004,
	GFX_ACCESS_INDIRECT_READ = 0x000008,
	GFX_ACCESS_SAMPLED_READ  = 0x000010,

	GFX_ACCESS_STORAGE_READ     = 0x000020,
	GFX_ACCESS_STORAGE_WRITE    = 0x000040,
	GFX_ACCESS_ATTACHMENT_INPUT = 0x000080,
	GFX_ACCESS_ATTACHMENT_READ  = 0x000100, // Necessary for blending/depth/stencil.
	GFX_ACCESS_ATTACHMENT_WRITE = 0x000200,
	GFX_ACCESS_ATTACHMENT_BLEND = 0x000300, // Both read/write.
	GFX_ACCESS_ATTACHMENT_TEST  = 0x000300, // Both read/write (depth/stencil testing).
	GFX_ACCESS_TRANSFER_READ    = 0x000400,
	GFX_ACCESS_TRANSFER_WRITE   = 0x000800,
	// TODO: How are we even going to handle host access?
	GFX_ACCESS_HOST_READ        = 0x001000,
	GFX_ACCESS_HOST_WRITE       = 0x002000,

	// Modifiers, meaningless without other flags.
	GFX_ACCESS_COMPUTE_ASYNC  = 0x004000,
	GFX_ACCESS_TRANSFER_ASYNC = 0x008000,
	GFX_ACCESS_DISCARD        = 0x010000 // Resulting contents may be discarded.
	// TODO: Add a modifier for framebuffer local regions?

} GFXAccessMask;

GFX_BIT_FIELD(GFXAccessMask)


/**
 * Dependency object definition.
 * Stores transition & synchronization metadata.
 */
typedef struct GFXDependency GFXDependency;


/**
 * Creates a dependency object.
 * @param device   NULL is equivalent to gfx_get_primary_device().
 * @param capacity Wait capacity of the dependency object.
 * @return NULL on failure.
 *
 * @capacity:
 * When a dependency is formed between operations that do not operate on the
 * same underlying Vulkan queue, internal semaphores are created.
 * (this can happen between async and non-async operations).
 *
 * These internal semaphores are recycled after N (capacity) subsequent
 * wait commands, at which point the original operations MUST have completed.
 * In other words: the dependency object can hold `capacity` concurrent
 * wait commands of which the operation that the commands are inserted in
 * are not yet completed.
 * Once an operation that waited on this dependency has finished,
 * we are allowed to insert another wait command into this dependency object.
 *
 * capacity can be 0 (infinite) to _never_ recycle any internal semaphores,
 * their memory will be stale until the dependency object is destroyed!
 */
GFX_API GFXDependency* gfx_create_dep(GFXDevice* device, unsigned int capacity);

/**
 * Destroys a dependency object.
 * Undefined behaviour if destroyed when it holds metadata
 * about pairs of GPU operations that have not yet completed!
 */
GFX_API void gfx_destroy_dep(GFXDependency* dep);

/**
 * Returns the device the dependency object was created for.
 * Can be called from any thread.
 */
GFX_API GFXDevice* gfx_dep_get_device(GFXDependency* dep);


/****************************
 * Dependency (transition/synchronization) injection.
 ****************************/

/**
 * Dependency injection type.
 */
typedef enum GFXInjectType
{
	GFX_DEP_SIGNAL,
	GFX_DEP_SIGNAL_RANGE,
	GFX_DEP_WAIT,
	GFX_DEP_WAIT_RANGE

} GFXInjectType;


/**
 * Dependency injection command.
 */
typedef struct GFXInject
{
	GFXInjectType type;

	// Object to inject a dependency in.
	GFXDependency* dep;

	// To-be synchronized resource (may be GFX_REF_NULL).
	GFXReference ref;

	// Affected resource range.
	GFXRange range;

	// Access scope that will be signaled.
	GFXAccessMask mask;

	// Shader stages that will have access.
	GFXShaderStage stage;

} GFXInject;


// TODO: Someday in the future change it so we only have gfx_dep_wait,
// and cannot wait for specific resources, only for entire queue destinations.
// This makes it so we can pool semaphores and not use one for every resource.
// Makes heap pooling much smoother also.

/**
 * Injection macros. Dependency objects can be signaled or waited upon
 * with respect to (a set of) resources on the GPU, the CPU is never blocked!
 *
 * In order for resources to transition between different operations performed
 * on them, a dependency must be injected inbetween the two operations.
 * If this is ignored, caches might not be flushed or invalidated, or worse,
 * the contents may be discarded by the engine and/or GPU when they see fit.
 *
 * A dependency is formed by a matching pair of signal/wait commands, where a
 * signal command can only match with one wait command, but a wait command can
 * match with any number of signal commands.
 * Wait and signal commands match iff they reference the same underlying
 * resource with an overlapping range (unspecified range = entire resource) AND
 * the access mask of the signal command matches the waiting operation.
 *
 * To force the dependency on a specific resource, use
 *  `gfx_dep_sigr` and `gfx_dep_waitr`
 *
 * To limit the dependency to a range (area) of a resource, use
 *  `gfx_dep_siga` and `gfx_dep_waita`
 *
 * To apply both of the above simultaneously, use
 *  `gfx_dep_sigra` and `gfx_dep_waitra`
 *
 * Resources are considered referenced by the dependency object as long as it
 * has not formed a valid signal/wait pair, meaning the resources in question
 * cannot be freed until its dependencies are waited upon.
 *
 * Injections that reference attachments are _NOT_ thread-safe with respect
 * to the renderer it belongs to, not even if referenced implicitly.
 * TODO: Explain that attachments must be operated on before the next acquire
 * and catched by the next submit-like-func?
 *
 * Functions that take injections as an argument are _always_ thread-safe with
 * respect to the dependency objects being referenced!
 */
#define gfx_dep_sig(dep_, mask_, stage_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_DEP_SIGNAL, \
		.dep = dep_, \
		.ref = GFX_REF_NULL, \
		.mask = mask_, \
		.stage = stage_ \
	}

#define gfx_dep_sigr(dep_, mask_, stage_, ref_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_DEP_SIGNAL, \
		.dep = dep_, \
		.ref = ref_, \
		.mask = mask_, \
		.stage = stage_ \
	}

#define gfx_dep_siga(dep_, mask_, stage_, range_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_DEP_SIGNAL_RANGE, \
		.dep = dep_, \
		.ref = GFX_REF_NULL, \
		.range = range_, \
		.mask = mask_, \
		.stage = stage_ \
	}

#define gfx_dep_sigra(dep_, mask_, stage_, ref_, range_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_DEP_SIGNAL_RANGE, \
		.dep = dep_, \
		.ref = ref_, \
		.range = range_, \
		.mask = mask_, \
		.stage = stage_ \
	}

#define gfx_dep_wait(dep_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_DEP_WAIT, \
		.dep = dep_, \
		.ref = GFX_REF_NULL \
	}

// TODO: Remove.
#define gfx_dep_waitr(dep_, ref_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_DEP_WAIT, \
		.dep = dep_, \
		.ref = ref_ \
	}

// TODO: Remove.
#define gfx_dep_waita(dep_, range_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_DEP_WAIT_RANGE, \
		.dep = dep_, \
		.ref = GFX_REF_NULL, \
		.range = range_ \
	}

// TODO: Remove.
#define gfx_dep_waitra(dep_, ref_, range_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_DEP_WAIT_RANGE, \
		.dep = dep_, \
		.ref = ref_, \
		.range = range_ \
	}


#endif
