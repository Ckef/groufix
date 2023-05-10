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


# Python program command to use
ifeq ($(OS),Windows_NT)
 PYTHON = python
else
 PYTHON = python3
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
OFLAGS = \
 $(CFLAGS) -c -MP -MMD -DGFX_BUILD_LIB -Isrc \
 -Ideps/glfw/include \
 -Ideps/Vulkan-Headers/include \
 -Ideps/shaderc/libshaderc/include \
 -Ideps/SPIRV-Cross \
 -isystem deps/cgltf \
 -isystem deps/stb

ifneq ($(OS),Windows_NT)
 OFLAGS += -fPIC
endif


# Linker flags
LFLAGS_ALL  = -shared -pthread
LFLAGS_WIN  = $(LFLAGS_ALL) -lgdi32 -static-libstdc++ -static-libgcc
LFLAGS_UNIX = $(LFLAGS_ALL) -ldl

ifeq ($(USE_WAYLAND),ON)
 LFLAGS_UNIX += -lwayland-client
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

CMAKE_TOOLCHAIN = \
 -DCMAKE_C_COMPILER=$(CC) \
 -DCMAKE_CXX_COMPILER=$(CXX)

GLFW_MINGW_TOOLCHAIN = \
 -DCMAKE_TOOLCHAIN_FILE=CMake/$(CC_PREFIX).cmake

SHADERC_MINGW_TOOLCHAIN = \
 -DCMAKE_TOOLCHAIN_FILE=cmake/linux-mingw-toolchain.cmake \
 -DMINGW_COMPILER_PREFIX=$(CC_PREFIX) \
 -Dgtest_disable_pthreads=ON

SPIRV_CROSS_MINGW_TOOLCHAIN = \
 $(CMAKE_TOOLCHAIN) \
 -DCMAKE_SYSTEM_NAME=Windows \
 -DCMAKE_RC_COMPILER=$(CC_PREFIX)-windres \
 -DCMAKE_FIND_ROOT_PATH=/usr/$(CC_PREFIX) \
 -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=Never \
 -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=Only \
 -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=Only

ifneq ($(CC_PREFIX),None) # Cross-compile
 GLFW_FLAGS        = $(GLFW_FLAGS_ALL) $(GLFW_MINGW_TOOLCHAIN)
 SHADERC_FLAGS     = $(SHADERC_FLAGS_ALL) $(SHADERC_MINGW_TOOLCHAIN) -G "Unix Makefiles"
 SPIRV_CROSS_FLAGS = $(SPIRV_CROSS_FLAGS_ALL) $(SPIRV_CROSS_MINGW_TOOLCHAIN)
else ifeq ($(OS),Windows_NT)
 GLFW_FLAGS        = $(GLFW_FLAGS_ALL) $(CMAKE_TOOLCHAIN) -G "MinGW Makefiles"
 SHADERC_FLAGS     = $(SHADERC_FLAGS_ALL) $(CMAKE_TOOLCHAIN) -G "MinGW Makefiles"
 SPIRV_CROSS_FLAGS = $(SPIRV_CROSS_FLAGS_ALL) $(CMAKE_TOOLCHAIN) -G "MinGW Makefiles"
else
 GLFW_FLAGS        = $(GLFW_FLAGS_UNIX) $(CMAKE_TOOLCHAIN)
 SHADERC_FLAGS     = $(SHADERC_FLAGS_ALL) $(CMAKE_TOOLCHAIN) -G "Unix Makefiles"
 SPIRV_CROSS_FLAGS = $(SPIRV_CROSS_FLAGS_ALL) $(CMAKE_TOOLCHAIN) -DSPIRV_CROSS_FORCE_PIC=ON
endif


##############################
# Directory management

.PHONY: .makedir
.makedir:
ifeq ($(OS),Windows_NT)
	$(eval MAKEDIR_W = $(subst /,\,$(MAKEDIR)))
	@if not exist $(MAKEDIR_W)\nul mkdir $(MAKEDIR_W)
else
	@mkdir -p $(MAKEDIR)
endif


# Cleaning directories
.PHONY: clean-temp
clean-temp:
ifeq ($(OS),Windows_NT)
	$(eval OUT_W = $(subst /,\,$(OUT)))
	@if exist $(OUT_W)\nul rmdir /s /q $(OUT_W)
else
	@rm -Rf $(OUT)
endif

.PHONY: clean-bin
clean-bin:
ifeq ($(OS),Windows_NT)
	$(eval BIN_W = $(subst /,\,$(BIN)))
	@if exist $(BIN_W)\nul rmdir /s /q $(BIN_W)
else
	@rm -Rf $(BIN)
endif

.PHONY: clean-deps
clean-deps:
ifeq ($(OS),Windows_NT)
	$(eval BUILD_W = $(subst /,\,$(BUILD)))
	@if exist $(BUILD_W)\nul rmdir /s /q $(BUILD_W)
	@del /q .shaderc-deps.stamp
else
	@rm -Rf $(BUILD)
	@rm -f .shaderc-deps.stamp
endif


# Nuke build files
.PHONY: clean
clean: clean-temp clean-bin


# Nuke everything
.PHONY: clean-all
clean-all: clean-temp clean-bin clean-deps


##############################
# Dependency and build files

LIBS = \
 $(BUILD)$(SUB)/glfw/src/libglfw3.a \
 $(BUILD)$(SUB)/shaderc/libshaderc/libshaderc_combined.a \
 $(BUILD)$(SUB)/SPIRV-Cross/libspirv-cross-c.a \
 $(BUILD)$(SUB)/SPIRV-Cross/libspirv-cross-core.a


# Auto expansion of files in a directory
getfiles = $(foreach d,$(wildcard $1/*),$(call getfiles,$d,$2) $(filter $2,$d))

SRCS = $(call getfiles,src,%.c)
OBJS = $(SRCS:src/%.c=$(OUT)$(SUB)/%.o)

TESTSRCS = $(call getfiles,tests,%.c)
TESTS    = $(TESTSRCS:tests/%.c=$(BIN)$(SUB)/%)


# Generated dependency files
-include $(OBJS:.o=.d)


##############################
# All available builds

.shaderc-deps.stamp:
	$(PYTHON) ./deps/shaderc/utils/git-sync-deps
ifeq ($(OS),Windows_NT)
	@type nul >> $@ & copy /b $@ +,,
else
	@touch $@
endif

$(BUILD)$(SUB)/glfw/src/libglfw3.a:
	@$(MAKE) $(MFLAGS_ALL) MAKEDIR=$(BUILD)$(SUB)/glfw .makedir
	@cd $(BUILD)$(SUB)/glfw && cmake $(GLFW_FLAGS) $(CURDIR)/deps/glfw && $(MAKE)

$(BUILD)$(SUB)/shaderc/libshaderc/libshaderc_combined.a: .shaderc-deps.stamp
	@$(MAKE) $(MFLAGS_ALL) MAKEDIR=$(BUILD)$(SUB)/shaderc .makedir
	@cd $(BUILD)$(SUB)/shaderc && cmake $(SHADERC_FLAGS) $(CURDIR)/deps/shaderc && $(MAKE)

$(BUILD)$(SUB)/SPIRV-Cross/libspirv-cross-c.a:
$(BUILD)$(SUB)/SPIRV-Cross/libspirv-cross-core.a:
	@$(MAKE) $(MFLAGS_ALL) MAKEDIR=$(BUILD)$(SUB)/SPIRV-Cross .makedir
	@cd $(BUILD)$(SUB)/SPIRV-Cross && cmake $(SPIRV_CROSS_FLAGS) $(CURDIR)/deps/SPIRV-Cross && $(MAKE)


# Object files
$(OUT)$(SUB)/%.o: src/%.c
	@$(MAKE) $(MFLAGS_ALL) MAKEDIR=$(@D) .makedir
	$(CC) $(OFLAGS) $< -o $@

# Library file
$(BIN)$(SUB)/libgroufix$(LIBEXT): $(LIBS) $(OBJS)
	@$(MAKE) $(MFLAGS_ALL) MAKEDIR=$(@D) .makedir
	$(CXX) $(OBJS) -o $@ $(LIBS) $(LFLAGS)

# Test programs
$(BIN)$(SUB)/$(TESTPAT): tests/%.c tests/test.h $(BIN)$(SUB)/libgroufix$(LIBEXT)
	$(CC) -Itests $< -o $@ $(TFLAGS) -L$(BIN)$(SUB) -Wl,-rpath,'$$ORIGIN' -lgroufix


# Platform builds
MFLAGS_ALL  = --no-print-directory
MFLAGS_UNIX = $(MFLAGS_ALL) SUB=/unix LIBEXT=.so TESTPAT=%
MFLAGS_WIN  = $(MFLAGS_ALL) SUB=/win LIBEXT=.dll TESTPAT=%.exe

.PHONY: .build .build-tests
.build: $(BIN)$(SUB)/libgroufix$(LIBEXT)
.build-tests: $(TESTS:%=$(TESTPAT))


.PHONY: unix unix-tests
unix:
	@$(MAKE) $(MFLAGS_UNIX) .build
unix-tests:
	@$(MAKE) $(MFLAGS_UNIX) .build-tests

.PHONY: win win-tests
win:
	@$(MAKE) $(MFLAGS_WIN) .build
win-tests:
	@$(MAKE) $(MFLAGS_WIN) .build-tests
