# groufix

_groufix_ is a cross platform, hardware accelerated graphics engine built in C. The library is primarily focused on the Vulkan API. The main repository is hosted on [GitHub](https://github.com/Ckef/groufix).

The engine currently supports the following targets:

* __Unix__-like ([GCC](https://gcc.gnu.org/))
* __Windows__ ([Mingw-w64](http://mingw-w64.org/doku.php))

## Building

The project is shipped with a Makefile, run `make` without a target to view all possible build targets. Each supported operating system has an explicit target. It is also possible to cross-compile _groufix_ to Windows using the `mingw-w64` package.

The Makefile takes the following flags:

* `DEBUG=xxx` tells the Makefile whether or not to compile _groufix_ with debug options enabled. `xxx` can be either `ON` or `OFF` and defaults to `OFF`. If not compiling with debug options, optimization settings will be applied.
* `CC=xxx` tells the Makefile to use a given compiler collection. `xxx` defaults to `gcc`, however this can be set to `i686-w64-mingw32-gcc` or `x86_64-w64_mingw32-gcc` to cross-compile to Windows.

### Dependencies

Major dependencies, such as [GLFW](https://www.glfw.org/), are included as submodules in the repository. They are automatically built and linked by the included Makefile. To build any target, the [VulkanSDK](https://vulkan.lunarg.com/sdk/home) needs to be installed on your system. When building on Linux, the `cmake` and either the `xorg-dev` or `libwayland-dev` packages need to be installed. When building on Windows, [CMake](https://cmake.org/) and [Mingw-w64](http://mingw-w64.org/doku.php) need to be installed, which should include the `cmake.exe` and `mingw32-make.exe` binaries. To use these to build _groufix_, add the `bin` directory of both to your `PATH` variable. Lastly, when cross-compiling to Windows, the `mingw-w64` package needs to be installed and the `CC` flag needs to be set as described above.
