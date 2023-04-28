##
# This file is part of groufix.
# Copyright (c) Stef Velzel. All rights reserved.
#
# groufix : graphics engine produced by Stef Velzel.
# www     : <www.vuzzel.nl>
##


##############################
# Helper manual, no target provided

help:
	@echo " Clean"
	@echo "  $(MAKE) clean      - Clean build files."
	@echo "  $(MAKE) clean-temp - Clean temporary build files only."
	@echo "  $(MAKE) clean-bin  - Clean output build files only."
	@echo "  $(MAKE) clean-deps - Clean dependency builds."
	@echo "  $(MAKE) clean-all  - Clean all files make produced."
	@echo ""
	@echo " Build"
	@echo "  $(MAKE) unix       - Build the Unix target."
	@echo "  $(MAKE) unix-tests - Build all tests for the Unix target."
	@echo "  $(MAKE) win        - Build the Windows target."
	@echo "  $(MAKE) win-tests  - Build all tests for the Windows target."
	@echo ""


##############################
# Build environment

CC    = gcc
CXX   = $(subst gcc,g,$(CC))++
DEBUG = ON

BIN   = bin
BUILD = build
OUT   = obj

USE_WAYLAND = OFF


# Is this macOS?
MACOS = OFF
ifneq ($(OS),Windows_NT)
 ifeq ($(shell uname -s),Darwin)
  MACOS = ON
 endif
endif


# Compiler prefix (None if not a cross-compile)
ifeq ($(CC),i686-w64-mingw32-gcc)
 CC_PREFIX = i686-w64-mingw32
else ifeq ($(CC),x86_64-w64-mingw32-gcc)
 CC_PREFIX = x86_64-w64-mingw32
else
 CC_PREFIX = None
endif


# Flags for all binaries
ifeq ($(DEBUG),ON)
 DFLAGS = -g -Og
else
 DFLAGS = -DNDEBUG -O3
endif

WFLAGS = -Wall -Wconversion -Wsign-compare -Wshadow -pedantic
CFLAGS = $(DFLAGS) $(WFLAGS) -std=c11 -Iinclude
TFLAGS = $(CFLAGS) -pthread -lm


# Flags for library files only
OFLAGS_ALL = \
 $(CFLAGS) -c -MP -MMD -DGFX_BUILD_LIB -Isrc \
 -Ideps/glfw/include \
 -Ideps/Vulkan-Headers/include \
 -Ideps/shaderc/libshaderc/include \
 -Ideps/SPIRV-Cross \
 -isystem deps/cgltf \
 -isystem deps/stb

ifeq ($(OS),Windows_NT)
 OFLAGS = $(OFLAGS_ALL)
else
 OFLAGS = $(OFLAGS_ALL) -fPIC
endif


# Linker flags
LFLAGS_ALL = -shared -pthread
LFLAGS_WIN = $(LFLAGS_ALL) -lgdi32 -static-libstdc++ -static-libgcc

ifeq ($(USE_WAYLAND),ON)
 LFLAGS_UNIX = $(LFLAGS_ALL) -ldl -lwayland-client
else
 LFLAGS_UNIX = $(LFLAGS_ALL) -ldl
endif

ifeq ($(MACOS),ON)
 LFLAGS_UNIX += \
  -framework CoreFoundation \
  -framework CoreGraphics \
  -framework Cocoa \
  -framework IOKit
endif

ifneq ($(CC_PREFIX),None) # Cross-compile
 LFLAGS = $(LFLAGS_WIN)
else ifeq ($(OS),Windows_NT)
 LFLAGS = $(LFLAGS_WIN)
else
 LFLAGS = $(LFLAGS_UNIX)
endif


# Dependency flags
CMAKE_CC_FLAGS = \
 -DCMAKE_C_COMPILER=$(CC) \
 -DCMAKE_CXX_COMPILER=$(CXX)

GLFW_FLAGS_ALL = \
 -DBUILD_SHARED_LIBS=OFF \
 -DGLFW_BUILD_EXAMPLES=OFF \
 -DGLFW_BUILD_TESTS=OFF \
 -DGLFW_BUILD_DOCS=OFF

ifeq ($(USE_WAYLAND),ON)
 GLFW_FLAGS_UNIX = $(GLFW_FLAGS_ALL) -DGLFW_USE_WAYLAND
else
 GLFW_FLAGS_UNIX = $(GLFW_FLAGS_ALL)
endif

SHADERC_FLAGS_ALL = \
 -Wno-dev \
 -DCMAKE_BUILD_TYPE=Release \
 -DSHADERC_SKIP_EXAMPLES=ON \
 -DSHADERC_SKIP_TESTS=ON \
 -DSPIRV_SKIP_EXECUTABLES=ON \
 -DSPIRV_SKIP_TESTS=ON

SHADERC_MINGW_TOOLCHAIN = \
 -DCMAKE_TOOLCHAIN_FILE=cmake/linux-mingw-toolchain.cmake \
 -DMINGW_COMPILER_PREFIX=$(CC_PREFIX) \
 -Dgtest_disable_pthreads=ON

SPIRV_CROSS_FLAGS_ALL = \
 -DSPIRV_CROSS_STATIC=ON \
 -DSPIRV_CROSS_SHARED=OFF \
 -DSPIRV_CROSS_CLI=OFF \
 -DSPIRV_CROSS_ENABLE_TESTS=OFF \
 -DSPIRV_CROSS_ENABLE_GLSL=OFF \
 -DSPIRV_CROSS_ENABLE_HLSL=OFF \
 -DSPIRV_CROSS_ENABLE_MSL=OFF \
 -DSPIRV_CROSS_ENABLE_CPP=OFF \
 -DSPIRV_CROSS_ENABLE_REFLECT=OFF \
 -DSPIRV_CROSS_ENABLE_UTIL=OFF

SPIRV_CROSS_MINGW_TOOLCHAIN = \
 -DCMAKE_SYSTEM_NAME=Windows \
 -DCMAKE_C_COMPILER=$(CC_PREFIX)-gcc \
 -DCMAKE_CXX_COMPILER=$(CC_PREFIX)-g++ \
 -DCMAKE_RC_COMPILER=$(CC_PREFIX)-windres \
 -DCMAKE_FIND_ROOT_PATH=/usr/$(CC_PREFIX) \
 -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=Never \
 -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=Only \
 -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=Only

ifneq ($(CC_PREFIX),None) # Cross-compile
 GLFW_FLAGS        = $(GLFW_FLAGS_ALL) -DCMAKE_TOOLCHAIN_FILE=CMake/$(CC_PREFIX).cmake
 SHADERC_FLAGS     = $(SHADERC_FLAGS_ALL) $(SHADERC_MINGW_TOOLCHAIN) -G "Unix Makefiles"
 SPIRV_CROSS_FLAGS = $(SPIRV_CROSS_FLAGS_ALL) $(SPIRV_CROSS_MINGW_TOOLCHAIN)
else ifeq ($(OS),Windows_NT)
 GLFW_FLAGS        = $(GLFW_FLAGS_ALL) $(CMAKE_CC_FLAGS) -G "MinGW Makefiles"
 SHADERC_FLAGS     = $(SHADERC_FLAGS_ALL) $(CMAKE_CC_FLAGS) -G "MinGW Makefiles"
 SPIRV_CROSS_FLAGS = $(SPIRV_CROSS_FLAGS_ALL) $(CMAKE_CC_FLAGS) -G "MinGW Makefiles"
else
 GLFW_FLAGS        = $(GLFW_FLAGS_UNIX) $(CMAKE_CC_FLAGS)
 SHADERC_FLAGS     = $(SHADERC_FLAGS_ALL) $(CMAKE_CC_FLAGS) -G "Unix Makefiles"
 SPIRV_CROSS_FLAGS = $(SPIRV_CROSS_FLAGS_ALL) $(CMAKE_CC_FLAGS) -DSPIRV_CROSS_FORCE_PIC=ON
endif


##############################
# Directory management

$(BIN)$(SUB):
ifeq ($(OS),Windows_NT)
	$(eval BINSUB_W = $(subst /,\,$(BIN)$(SUB)))
	@if not exist $(BINSUB_W)\nul mkdir $(BINSUB_W)
else
	@mkdir -p $(BIN)$(SUB)
endif

$(BUILD)$(SUB):
ifeq ($(OS),Windows_NT)
	$(eval BUILDSUB_W = $(subst /,\,$(BUILD)$(SUB)))
	@if not exist $(BUILDSUB_W)\glfw\nul mkdir $(BUILDSUB_W)\glfw
	@if not exist $(BUILDSUB_W)\shaderc\nul mkdir $(BUILDSUB_W)\shaderc
	@if not exist $(BUILDSUB_W)\SPIRV-Cross\nul mkdir $(BUILDSUB_W)\SPIRV-Cross
else
	@mkdir -p $(BUILD)$(SUB)/glfw
	@mkdir -p $(BUILD)$(SUB)/shaderc
	@mkdir -p $(BUILD)$(SUB)/SPIRV-Cross
endif

$(OUT)$(SUB):
ifeq ($(OS),Windows_NT)
	$(eval OUTSUB_W = $(subst /,\,$(OUT)$(SUB)))
	@if not exist $(OUTSUB_W)\groufix\assets\nul mkdir $(OUTSUB_W)\groufix\assets
	@if not exist $(OUTSUB_W)\groufix\containers\nul mkdir $(OUTSUB_W)\groufix\containers
	@if not exist $(OUTSUB_W)\groufix\core\mem\nul mkdir $(OUTSUB_W)\groufix\core\mem
else
	@mkdir -p $(OUT)$(SUB)/groufix/assets
	@mkdir -p $(OUT)$(SUB)/groufix/containers
	@mkdir -p $(OUT)$(SUB)/groufix/core/mem
endif


# Cleaning directories
clean-temp:
ifeq ($(OS),Windows_NT)
	$(eval OUT_W = $(subst /,\,$(OUT)))
	@if exist $(OUT_W)\nul rmdir /s /q $(OUT_W)
else
	@rm -Rf $(OUT)
endif

clean-bin:
ifeq ($(OS),Windows_NT)
	$(eval BIN_W = $(subst /,\,$(BIN)))
	@if exist $(BIN_W)\nul rmdir /s /q $(BIN_W)
else
	@rm -Rf $(BIN)
endif

clean-deps:
ifeq ($(OS),Windows_NT)
	$(eval BUILD_W = $(subst /,\,$(BUILD)))
	@if exist $(BUILD_W)\nul rmdir /s /q $(BUILD_W)
else
	@rm -Rf $(BUILD)
endif


# Nuke build files
clean: clean-temp clean-bin


# Nuke everything
clean-all: clean-temp clean-bin clean-deps


##############################
# Dependency and build files

LIBS = \
 $(BUILD)$(SUB)/glfw/src/libglfw3.a \
 $(BUILD)$(SUB)/shaderc/libshaderc/libshaderc_combined.a \
 $(BUILD)$(SUB)/SPIRV-Cross/libspirv-cross-c.a \
 $(BUILD)$(SUB)/SPIRV-Cross/libspirv-cross-core.a


# Auto expansion of a directory
recurse = $(foreach d,$(wildcard $1/*),$(call recurse,$d,$2) $(filter $2,$d))

SRCS = $(call recurse,src,%.c)
OBJS = $(SRCS:src/%.c=$(OUT)$(SUB)/%.o)

TESTSRCS = $(call recurse,tests,%.c)
TESTS    = $(TESTSRCS:tests/%.c=$(BIN)$(SUB)/%)


# Generated dependency files
-include $(OBJS:.o=.d)


##############################
# All available builds

$(BUILD)$(SUB)/glfw/src/libglfw3.a: | $(BUILD)$(SUB)
	@cd $(BUILD)$(SUB)/glfw && cmake $(GLFW_FLAGS) $(CURDIR)/deps/glfw && $(MAKE)

$(BUILD)$(SUB)/shaderc/libshaderc/libshaderc_combined.a: | $(BUILD)$(SUB)
	@cd $(BUILD)$(SUB)/shaderc && cmake $(SHADERC_FLAGS) $(CURDIR)/deps/shaderc && $(MAKE)

$(BUILD)$(SUB)/SPIRV-Cross/libspirv-cross-c.a:
$(BUILD)$(SUB)/SPIRV-Cross/libspirv-cross-core.a: | $(BUILD)$(SUB)
	@cd $(BUILD)$(SUB)/SPIRV-Cross && cmake $(SPIRV_CROSS_FLAGS) $(CURDIR)/deps/SPIRV-Cross && $(MAKE)


# Object files
$(OUT)$(SUB)/%.o: src/%.c | $(OUT)$(SUB)
	$(CC) $(OFLAGS) $< -o $@

# Library file
$(BIN)$(SUB)/libgroufix$(LIBEXT): $(LIBS) $(OBJS) | $(BIN)$(SUB)
	$(CXX) $(OBJS) -o $@ $(LIBS) $(LFLAGS)

# Test programs
$(BIN)$(SUB)/$(TESTPAT): tests/%.c tests/test.h $(BIN)$(SUB)/libgroufix$(LIBEXT)
	$(CC) -Itests $< -o $@ $(TFLAGS) -L$(BIN)$(SUB) -Wl,-rpath,'$$ORIGIN' -lgroufix


# Platform builds
MFLAGS_ALL  = --no-print-directory
MFLAGS_UNIX = $(MFLAGS_ALL) SUB=/unix LIBEXT=.so TESTPAT=%
MFLAGS_WIN  = $(MFLAGS_ALL) SUB=/win LIBEXT=.dll TESTPAT=%.exe

.build: $(BIN)$(SUB)/libgroufix$(LIBEXT)
.build-tests: $(TESTS:%=$(TESTPAT))


unix:
	@$(MAKE) $(MFLAGS_UNIX) .build
unix-tests:
	@$(MAKE) $(MFLAGS_UNIX) .build-tests

win:
	@$(MAKE) $(MFLAGS_WIN) .build
win-tests:
	@$(MAKE) $(MFLAGS_WIN) .build-tests
