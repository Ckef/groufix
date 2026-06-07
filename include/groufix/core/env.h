/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_ENV_H
#define GFX_CORE_ENV_H

#include "groufix/def.h"


/**
 * Environment variable name to set the default log level.
 * Overrides the GFX_LOG_DEFAULT value at init.
 * Value can be NONE|FATAL|ERROR|WARN|INFO|DEBUG|VERBOSE|ALL, case insensitive.
 */
#define GFX_ENV_DEFAULT_LOG_LEVEL "GROUFIX_DEFAULT_LOG_LEVEL"


/**
 * Environment variable name to influence the primary device selection.
 * Prioritize matching physical Vulkan devices.
 * A device matches if the set value is a substring of its name, case insensitive.
 */
#define GFX_ENV_PRIMARY_VK_DEVICE "GROUFIX_PRIMARY_VK_DEVICE"


/**
 * Environment variable name to turn off the Vulkan Validation Layers.
 * Ignored if not compiled with debug options enabled.
 * Value can be FALSE|OFF|NO|f|n|0 to turn off, case insensitive.
 */
#define GFX_ENV_USE_VK_VALIDATION_LAYERS "GROUFIX_USE_VK_VALIDATION_LAYERS"


#endif
