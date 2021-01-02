
groufix
=======

_groufix_ is a cross-platform, thread-friendly and hardware accelerated graphics engine built in C. The library is primarily focused on the Vulkan API. The main repository is hosted on [GitHub](https://github.com/Ckef/groufix).

The engine currently supports the following targets:

* __Unix__-like ([GCC](https://gcc.gnu.org/))
* __Windows__ (XP+) ([Mingw-w64](http://mingw-w64.org/doku.php))


Building
--------

The project is shipped with a Makefile, run `make` or `mingw32-make` without a target to view all possible build targets. Each supported operating system has an explicit target. It is also possible to cross-compile _groufix_ to Windows using the `mingw-w64` package.

The Makefile takes the following flags:

* `DEBUG=xxx` tells the Makefile whether or not to compile _groufix_ with debug options enabled. If not compiling with debug options, optimization settings will be applied. `xxx` can be either `ON` or `OFF` and defaults to `ON`.

* `CC=xxx` tells the Makefile to use a given compiler collection. `xxx` defaults to `gcc`, however this can be set to `i686-w64-mingw32-gcc` or `x86_64-w64_mingw32-gcc` to cross-compile to Windows.

### Dependencies

Major dependencies, such as [GLFW](https://www.glfw.org/), are included as submodules in the repository. They are automatically built and linked by the included Makefile. To build and run with debug options enabled, the [VulkanSDK](https://vulkan.lunarg.com/sdk/home) needs to be installed on your system.

_When building on Linux_, the `gcc`, `make`, `cmake` and either the `xorg-dev` or `libwayland-dev` packages need to be installed.

_When building on Windows_, [CMake](https://cmake.org/) and [Mingw-w64](http://mingw-w64.org/doku.php) need to be installed, which should include the `cmake.exe` and `mingw32-make.exe` binaries. To use these to build _groufix_, add the `bin` directory of both to your `PATH` variable.

_When cross-compiling to Windows_, the `mingw-w64` package needs to be installed and the `CC` flag needs to be set as described under [#Building](#building).


Usage
-----

Once _groufix_ is built, it can be used in your code with `#include <groufix.h>`. All core functionality will be available through this file. The engine must be initialized with a call to `gfx_init`. The thread that initializes the engine is considered the _main thread_. Any other function of _groufix_ cannot be called before `gfx_init` has returned succesfully, the only exceptions being `gfx_terminate`, `gfx_attach`, `gfx_detach` and the `gfx_log_*` function family. When the engine will not be used anymore, it must be terminated by the main thread with a call to `gfx_terminate`. Once the engine is terminated, it behaves exactly the same as before initialization.

_groufix will not implicitly free resources_. This means that any object you create or initialize should be destroyed or cleared by you as well. In practice this means any call to a `gfx_create_*` function should be followed up by a call to the associated `gfx_destroy_*` function and every call to a `gfx_*_init` function should be followed up by a call to the associated `gfx_*_clear` function. Any `gfx_destroy_*` function can take `NULL` as argument and the call becomes a no-op.

All names starting with `gfx` or `GFX` are reserved by _groufix_, using any such name in conjunction with the engine might result in redefinitions.

### Threading

Similarly to initializing the engine, any thread that wants to make any _groufix_ calls needs to _attach_ itself to the engine with a call to `gfx_attach`. Before an attached thread exits, it must be detached with a call to `gfx_detach`. The main thread is the only exception, it does not have to be explicitly attached or detached.

_groufix will not reference count_. This means that whenever you destroy or clear an object with a call to the associated `gfx_destroy_*` or `gfx_*_clear` function, any other object may not reference this object anymore. In other words, _groufix_ will __not__ idle during destruction or clearing until all references to the object are released.

_groufix objects are not reentrant_. Function calls associated with the same object created with a call to `gfx_create_*` or `gfx_*_init` are not synchronized and __cannot__ be called concurrently from different threads. ~~However, internal access to the same object of any type is synchronized, unless explicitly stated otherwise. For example, various concurrently operating objects referencing the same `GFXWindow` object is thread-safe.~~

_All functions directly related to the window manager of the host platform are thread affine_. These functions can __only__ be called from the main thread. These functions are `gfx_poll_events`, `gfx_wait_events` and all functions defined in `groufix/core/window.h` (these are the `gfx_*monitor*` and `gfx_*window*` function families). All other functions can be called from any thread, unless explicitly stated otherwise.
