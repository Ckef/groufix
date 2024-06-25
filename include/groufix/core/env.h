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
 * Prioritize matching physical Vulkan devices.
 * A device matches if the set value is a substring of its name, case insensitive.
 */
#define GFX_ENV_PRIMARY_VK_DEVICE "GROUFIX_PRIMARY_VK_DEVICE"


#endif
