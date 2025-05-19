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

	GFX_ACCESS_STORAGE_READ        = 0x000020,
	GFX_ACCESS_STORAGE_WRITE       = 0x000040,
	GFX_ACCESS_STORAGE_READ_WRITE  = 0x000060,
	GFX_ACCESS_ATTACHMENT_INPUT    = 0x000080,
	GFX_ACCESS_ATTACHMENT_READ     = 0x000100, // Necessary for blending/depth/stencil.
	GFX_ACCESS_ATTACHMENT_WRITE    = 0x000200,
	GFX_ACCESS_ATTACHMENT_TEST     = 0x000300, // Both read/write (depth/stencil testing).
	GFX_ACCESS_ATTACHMENT_BLEND    = 0x000300, // Both read/write.
	GFX_ACCESS_ATTACHMENT_RESOLVE  = 0x000400,
	GFX_ACCESS_TRANSFER_READ       = 0x000800,
	GFX_ACCESS_TRANSFER_WRITE      = 0x001000,
	GFX_ACCESS_TRANSFER_READ_WRITE = 0x001800,
	GFX_ACCESS_HOST_READ           = 0x002000,
	GFX_ACCESS_HOST_WRITE          = 0x004000,
	GFX_ACCESS_HOST_READ_WRITE     = 0x006000,

	// Modifiers, meaningless without other flags.
	GFX_ACCESS_COMPUTE_ASYNC  = 0x008000,
	GFX_ACCESS_TRANSFER_ASYNC = 0x010000,
	GFX_ACCESS_DISCARD        = 0x020000, // Resulting contents may be discarded.
	GFX_ACCESS_MODIFIERS      = 0x038000  // All modifiers.

	// TODO: Add a modifier for framebuffer local regions?

} GFXAccessMask;

GFX_BIT_FIELD(GFXAccessMask)


/**
 * Access read/write checkers.
 */
#define GFX_ACCESS_READS(mask) \
	((mask) & \
		(GFX_ACCESS_VERTEX_READ | GFX_ACCESS_INDEX_READ | \
		GFX_ACCESS_UNIFORM_READ | GFX_ACCESS_INDIRECT_READ | \
		GFX_ACCESS_SAMPLED_READ | GFX_ACCESS_STORAGE_READ | \
		GFX_ACCESS_ATTACHMENT_INPUT | GFX_ACCESS_ATTACHMENT_READ | \
		GFX_ACCESS_TRANSFER_READ | GFX_ACCESS_HOST_READ))

#define GFX_ACCESS_WRITES(mask) \
	((mask) & \
		(GFX_ACCESS_STORAGE_WRITE | \
		GFX_ACCESS_ATTACHMENT_WRITE | GFX_ACCESS_ATTACHMENT_RESOLVE | \
		GFX_ACCESS_TRANSFER_WRITE | GFX_ACCESS_HOST_WRITE))


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
 * wait commands of which the first operation that these wait commands were
 * submitted in is not yet completed.
 * Once the first operation that waited on this dependency has finished,
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
	GFX_INJ_SIGNAL,
	GFX_INJ_SIGNAL_RANGE,
	GFX_INJ_SIGNAL_FROM,
	GFX_INJ_SIGNAL_RANGE_FROM,
	GFX_INJ_WAIT

} GFXInjectType;


/**
 * Dependency injection command.
 */
typedef struct GFXInject
{
	GFXInjectType type;

	// Object to inject a dependency in (may be NULL).
	GFXDependency* dep;

	// To-be synchronized resource (may be GFX_REF_NULL).
	GFXReference ref;

	// Affected resource range.
	GFXRange range;

	// Access scope that will be signaled.
	GFXAccessMask mask;

	// Shader stages that will have access.
	GFXShaderStage stage;

	// Access scope that does the signaling.
	GFXAccessMask maskf;

	// Shader stages that do the signaling.
	GFXShaderStage stagef;

} GFXInject;


/**
 * Injection macros. Dependency objects or passes of a renderer can be
 * signaled or waited upon with respect to (a set of) resources on the GPU,
 * the CPU is never blocked!
 *
 * In order for resources to transition between different operations performed
 * on them, a dependency must be injected inbetween the two operations.
 * If this is ignored, caches might not be flushed or invalidated, or worse,
 * the contents may be discarded by the engine and/or GPU when they see fit.
 *
 * A dependency is formed by a pair of signal/wait commands, where a signal
 * command matches with exactly one wait command, but a wait commad can match
 * with any number of signal commands.
 * Signal commands are accumulated in dependency objects and are made visible
 * by the operation they were injected in. After being made visible,
 * a wait command matches (and waits for) all signal commands that address
 * the same underlying Vulkan queue.
 *
 * There are three queue destinations:
 *  -graphics, -compute, -transfer
 *
 * Operations and the commands injected into them normally address the
 * graphics queue, but they can address the other two with the respective
 * `*_(COMPUTE|TRANSFER)_ASYNC` type, flag and modifiers.
 *
 * To limit the dependency to a range (area) of a resource, use
 *  `gfx_dep_siga`
 *
 * To force the dependency on a specific resource, use
 *  `gfx_dep_sigr`
 *
 * To apply both of the above simultaneously, use
 *  `gfx_dep_sigra`
 *
 * To specify the source access mask and stages of a specific resource, use
 *  `gfx_dep_sigrf` or `gfx_dep_sigraf`
 *
 * When signaling passes of a renderer directly, `f` is always appended:
 *  `gfx_sigf`, `gfx_sigrf`, `gfx_sigraf`
 *
 * Resources are considered referenced by the dependency object as long as it
 * has not formed a valid signal/wait pair, meaning the resources in question
 * cannot be freed until its dependencies are waited upon.
 *
 * Injections that reference attachments are _NOT_ thread-safe with respect
 * to the renderer it belongs to, not even if referenced implicitly.
 * When an attachment is signaled out of its renderer and operated on, it must
 * be immediately waited upon by the next frame of that renderer!
 *
 * When the access mask contains host read/write access, remaining writes are
 * flushed to host visible memory after the operation. The host must not read or
 * write to this memory before the operation is waited upon by the host itself.
 *
 * Functions that take injections as an argument are _always_ thread-safe with
 * respect to the dependency objects being referenced!
 */
#define gfx_dep_sig(dep_, mask_, stage_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_INJ_SIGNAL, \
		.dep = dep_, \
		.ref = GFX_REF_NULL, \
		.mask = mask_, \
		.stage = stage_ \
	}

#define gfx_dep_siga(dep_, mask_, stage_, range_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_INJ_SIGNAL_RANGE, \
		.dep = dep_, \
		.ref = GFX_REF_NULL, \
		.range = range_, \
		.mask = mask_, \
		.stage = stage_ \
	}

#define gfx_dep_sigr(dep_, mask_, stage_, ref_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_INJ_SIGNAL, \
		.dep = dep_, \
		.ref = ref_, \
		.mask = mask_, \
		.stage = stage_ \
	}

#define gfx_dep_sigra(dep_, mask_, stage_, ref_, range_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_INJ_SIGNAL_RANGE, \
		.dep = dep_, \
		.ref = ref_, \
		.range = range_, \
		.mask = mask_, \
		.stage = stage_ \
	}

#define gfx_dep_sigrf(dep_, maskf_, stagef_, mask_, stage_, ref_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_INJ_SIGNAL_FROM, \
		.dep = dep_, \
		.ref = ref_, \
		.mask = mask_, \
		.stage = stage_, \
		.maskf = maskf_, \
		.stagef = stagef_ \
	}

#define gfx_dep_sigraf(dep_, maskf_, stagef_, mask_, stage_, ref_, range_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_INJ_SIGNAL_RANGE_FROM, \
		.dep = dep_, \
		.ref = ref_, \
		.range = range_, \
		.mask = mask_, \
		.stage = stage_, \
		.maskf = maskf_, \
		.stagef = stagef_ \
	}

#define gfx_sigf(maskf_, stagef_, mask_, stage_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_INJ_SIGNAL_FROM, \
		.dep = NULL, \
		.ref = GFX_REF_NULL, \
		.mask = mask_, \
		.stage = stage_, \
		.maskf = maskf_, \
		.stagef = stagef_ \
	}

#define gfx_sigrf(maskf_, stagef_, mask_, stage_, ref_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_INJ_SIGNAL_FROM, \
		.dep = NULL, \
		.ref = ref_, \
		.mask = mask_, \
		.stage = stage_, \
		.maskf = maskf_, \
		.stagef = stagef_ \
	}

#define gfx_sigraf(maskf_, stagef_, mask_, stage_, ref_, range_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_INJ_SIGNAL_RANGE_FROM, \
		.dep = NULL, \
		.ref = ref_, \
		.range = range_, \
		.mask = mask_, \
		.stage = stage_, \
		.maskf = maskf_, \
		.stagef = stagef_ \
	}

#define gfx_dep_wait(dep_) \
	GFX_LITERAL(GFXInject){ \
		.type = GFX_INJ_WAIT, \
		.dep = dep_, \
		.ref = GFX_REF_NULL \
	}


#endif
