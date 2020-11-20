# groufix

_groufix_ is a cross platform, hardware accelerated graphics engine built in C. The library is primarily focused on the Vulkan API. The main repository is hosted on [GitHub](https://github.com/Ckef/groufix).

The engine currently supports the following targets:

* __Unix__-like ([GCC](https://gcc.gnu.org/))
* __Windows__ (XP+) ([Mingw-w64](http://mingw-w64.org/doku.php))

## Building

The project is shipped with a Makefile, run `make` without a target to view all possible build targets. Each supported operating system has an explicit target. It is also possible to cross-compile _groufix_ to Windows using the `mingw-w64` package.

The Makefile takes the following flags:

* `DEBUG=xxx` tells the Makefile whether or not to compile _groufix_ with debug options enabled. `xxx` can be either `ON` or `OFF` and defaults to `ON`. If not compiling with debug options, optimization settings will be applied.
* `CC=xxx` tells the Makefile to use a given compiler collection. `xxx` defaults to `gcc`, however this can be set to `i686-w64-mingw32-gcc` or `x86_64-w64_mingw32-gcc` to cross-compile to Windows.

### Dependencies

Major dependencies, such as [GLFW](https://www.glfw.org/), are included as submodules in the repository. They are automatically built and linked by the included Makefile. To build and run with debug options enabled, the [VulkanSDK](https://vulkan.lunarg.com/sdk/home) needs to be installed on your system. When building on Linux, the `gcc`, `make`, `cmake` and either the `xorg-dev` or `libwayland-dev` packages need to be installed. When building on Windows, [CMake](https://cmake.org/) and [Mingw-w64](http://mingw-w64.org/doku.php) need to be installed, which should include the `cmake.exe` and `mingw32-make.exe` binaries. To use these to build _groufix_, add the `bin` directory of both to your `PATH` variable. Lastly, when cross-compiling to Windows, the `mingw-w64` package needs to be installed and the `CC` flag needs to be set as described above.

## Usage

Once _groufix_ is built, it can be used in your code with `#include <groufix.h>`. All core functionality will be available through this file. The engine should be initialized with a call to `gfx_init`. The thread that initializes the engine is considered the _main thread_. Any other function of _groufix_ cannot be called before `gfx_init` has returned succesfully, the only exceptions being `gfx_terminate`, `gfx_attach`, `gfx_detach` and the `gfx_log_*` function family. When the engine will not be used anymore, it must be terminated by the main thread with a call to `gfx_terminate`. Once the engine is terminated, it behaves exactly the same as before initialization.

_groufix will not implicitly free resources_. This means that any object you create or initialize should be destroyed or cleared by you as well. In practice this means any call to a `gfx_create_*` function should be followed up by a call to the appropriate `gfx_destroy_*` function and every call to a `gfx_*_init` function should be followed up by a call to the appropriate `gfx_*_clear` function. Any `gfx_destroy_*` function can take `NULL` as argument and the call becomes a no-op.

All names starting with `gfx`, `_gfx`, `GFX` or `_GFX` are reserved by _groufix_, using any such name in conjunction with the engine might result in redefinitions.

### Threading

Similarly to initializing the engine, any thread that wants to make any _groufix_ calls needs to _attach_ itself to the engine with a call to `gfx_attach`, the only exception being the main thread. Technically _groufix_ can be used without attaching the calling thread, however not all functionality is guaranteed. Most notably, the `gfx_log_*` function family will not be able to distinguish between threads without attaching them. Before an attached thread exits, it must be detached with a call to `gfx_detach`, the only exception being the main thread again.

_All functions related to the window manager of the host platform are thread affine_. These functions can only be called from the main thread. These functions are `gfx_poll_events` and all functions defined in `groufix/core/window.h` (this includes the `gfx_*monitor*` and `gfx_*window*` function families).
