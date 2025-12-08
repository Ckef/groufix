##
# This file is part of groufix.
# Copyright (c) Stef Velzel. All rights reserved.
#
# groufix : graphics engine produced by Stef Velzel.
# www     : <www.vuzzel.nl>
##


##############################
# Helper manual, no target provided

.PHONY: help
help:
	@echo " Clean"
	@echo "  $(MAKE) clean         - Clean temporary and output build files."
	@echo "  $(MAKE) clean-temp    - Clean temporary build files only."
	@echo "  $(MAKE) clean-bin     - Clean output build files only."
	@echo "  $(MAKE) clean-deps    - Clean dependency builds."
	@echo "  $(MAKE) clean-all     - Clean all files make produced."
	@echo ""
	@echo " Build"
	@echo "  $(MAKE) <TAR>         - Build the groufix library only."
	@echo "  $(MAKE) <TAR>-grouviz - Build grouviz."
	@echo "  $(MAKE) <TAR>-tests   - Build all tests."
	@echo "  $(MAKE) <TAR>-all     - Build everything."
	@echo ""
	@echo "  Choose target platform with <TAR>:"
	@echo "   - unix (Unix + macOS)"
	@echo "   - win  (Windows)"
	@echo ""
	@echo "  e.g. '$(MAKE) unix-grouviz'"
	@echo ""


##############################
# Build environment

CC    = gcc
CXX   = $(subst gcc,g,$(CC))++
DEBUG = ON

BIN   = bin
BUILD = build
TEMP  = obj

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

ifeq ($(DEBUG),ON)
 ifdef SANITIZE
  DFLAGS += -fsanitize=$(SANITIZE)
 endif
endif

WFLAGS = -Wall -Wconversion -Wsign-compare -Wshadow -pedantic
CFLAGS = $(DFLAGS) $(WFLAGS) -std=c11 -Iinclude
OFLAGS = $(CFLAGS) -c -MP -MMD
TFLAGS = $(CFLAGS) -pthread -lm

ifneq ($(OS),Windows_NT)
 OFLAGS += -fPIC
endif


# Flags for library files only
LIB_FLAGS = \
 $(OFLAGS) -DGFX_BUILD_LIB -Ilib \
 -Ideps/glfw/include \
 -Ideps/Vulkan-Headers/include \
 -Ideps/shaderc/libshaderc/include \
 -Ideps/SPIRV-Cross \
 -Ideps/cimgui \
 -isystem deps/cgltf \
 -isystem deps/stb

ifeq ($(CC_PREFIX),None)
 ifneq ($(OS),Windows_NT)
  LIB_FLAGS += -D_POSIX_C_SOURCE=199506L
 endif
endif


# Library linker flags
LIB_LDFLAGS_ALL  = -shared -pthread
LIB_LDFLAGS_WIN  = $(LIB_LDFLAGS_ALL) -lgdi32 -static-libstdc++ -static-libgcc
LIB_LDFLAGS_UNIX = $(LIB_LDFLAGS_ALL) -ldl

ifeq ($(USE_WAYLAND),ON)
 LIB_LDFLAGS_UNIX += -lwayland-client
endif

ifeq ($(MACOS),ON)
 LIB_LDFLAGS_UNIX += \
  -framework CoreFoundation \
  -framework CoreGraphics \
  -framework Cocoa \
  -framework IOKit
endif

ifneq ($(CC_PREFIX),None) # Cross-compile
 LIB_LDFLAGS = $(LIB_LDFLAGS_WIN)
else ifeq ($(OS),Windows_NT)
 LIB_LDFLAGS = $(LIB_LDFLAGS_WIN)
else
 LIB_LDFLAGS = $(LIB_LDFLAGS_UNIX)
endif


# Dependency flags
GLFW_FLAGS_ALL = \
 -DBUILD_SHARED_LIBS=OFF \
 -DGLFW_BUILD_EXAMPLES=OFF \
 -DGLFW_BUILD_TESTS=OFF \
 -DGLFW_BUILD_DOCS=OFF

ifeq ($(USE_WAYLAND),ON)
 GLFW_FLAGS_UNIX = $(GLFW_FLAGS_ALL) -DGLFW_BUILD_X11=OFF -DGLFW_BUILD_WAYLAND=ON
else
 GLFW_FLAGS_UNIX = $(GLFW_FLAGS_ALL) -DGLFW_BUILD_X11=ON -DGLFW_BUILD_WAYLAND=OFF
endif

SHADERC_FLAGS_ALL = \
 -Wno-dev \
 -DCMAKE_BUILD_TYPE=Release \
 -DSHADERC_SKIP_EXAMPLES=ON \
 -DSHADERC_SKIP_TESTS=ON \
 -DENABLE_GLSLANG_BINARIES=OFF \
 -DSPIRV_HEADERS_SKIP_EXAMPLES=ON \
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

CIMGUI_FLAGS_ALL = \
 -DIMGUI_STATIC=ON

CMAKE_TOOLCHAIN = \
 -DCMAKE_C_COMPILER=$(CC) \
 -DCMAKE_CXX_COMPILER=$(CXX)

CMAKE_MINGW_TOOLCHAIN = \
 $(CMAKE_TOOLCHAIN) \
 -DCMAKE_SYSTEM_NAME=Windows \
 -DCMAKE_RC_COMPILER=$(CC_PREFIX)-windres \
 -DCMAKE_FIND_ROOT_PATH=/usr/$(CC_PREFIX) \
 -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=Never \
 -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=Only \
 -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=Only

GLFW_MINGW_TOOLCHAIN = \
 -DCMAKE_TOOLCHAIN_FILE=CMake/$(CC_PREFIX).cmake

SHADERC_MINGW_TOOLCHAIN = \
 -DCMAKE_TOOLCHAIN_FILE=cmake/linux-mingw-toolchain.cmake \
 -DMINGW_COMPILER_PREFIX=$(CC_PREFIX) \
 -Dgtest_disable_pthreads=ON

ifneq ($(CC_PREFIX),None) # Cross-compile
 GLFW_FLAGS        = $(GLFW_FLAGS_ALL) $(GLFW_MINGW_TOOLCHAIN)
 SHADERC_FLAGS     = $(SHADERC_FLAGS_ALL) $(SHADERC_MINGW_TOOLCHAIN) -G "Unix Makefiles"
 SPIRV_CROSS_FLAGS = $(SPIRV_CROSS_FLAGS_ALL) $(CMAKE_MINGW_TOOLCHAIN)
 CIMGUI_FLAGS      = $(CIMGUI_FLAGS_ALL) $(CMAKE_MINGW_TOOLCHAIN)
else ifeq ($(OS),Windows_NT)
 GLFW_FLAGS        = $(GLFW_FLAGS_ALL) $(CMAKE_TOOLCHAIN) -G "MinGW Makefiles"
 SHADERC_FLAGS     = $(SHADERC_FLAGS_ALL) $(CMAKE_TOOLCHAIN) -G "MinGW Makefiles"
 SPIRV_CROSS_FLAGS = $(SPIRV_CROSS_FLAGS_ALL) $(CMAKE_TOOLCHAIN) -G "MinGW Makefiles"
 CIMGUI_FLAGS      = $(CIMGUI_FLAGS_ALL) $(CMAKE_TOOLCHAIN) -G "MinGW Makefiles"
else
 GLFW_FLAGS        = $(GLFW_FLAGS_UNIX) $(CMAKE_TOOLCHAIN)
 SHADERC_FLAGS     = $(SHADERC_FLAGS_ALL) $(CMAKE_TOOLCHAIN) -G "Unix Makefiles"
 SPIRV_CROSS_FLAGS = $(SPIRV_CROSS_FLAGS_ALL) $(CMAKE_TOOLCHAIN) -DSPIRV_CROSS_FORCE_PIC=ON
 CIMGUI_FLAGS      = $(CIMGUI_FLAGS_ALL) $(CMAKE_TOOLCHAIN) -DCMAKE_POSITION_INDEPENDENT_CODE=ON
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
	$(eval TEMP_W = $(subst /,\,$(TEMP)))
	@if exist $(TEMP_W)\nul rmdir /s /q $(TEMP_W)
else
	@rm -Rf $(TEMP)
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

DEPS = \
 $(BUILD)$(SUB)/glfw/src/libglfw3.a \
 $(BUILD)$(SUB)/shaderc/libshaderc/libshaderc_combined.a \
 $(BUILD)$(SUB)/SPIRV-Cross/libspirv-cross-c.a \
 $(BUILD)$(SUB)/SPIRV-Cross/libspirv-cross-core.a

DEPS_EXPORT = \
 $(BUILD)$(SUB)/cimgui/cimgui.a


# Auto expansion of files in a directory
getfiles = $(foreach d,$(wildcard $1/*),$(call getfiles,$d,$2) $(filter $2,$d))

LIB_SRCS = $(call getfiles,lib,%.c)
LIB_OBJS = $(LIB_SRCS:lib/%.c=$(TEMP)$(SUB)/lib/%.o)

VIZ_SRCS = $(call getfiles,viz,%.c)
VIZ_OBJS = $(VIZ_SRCS:viz/%.c=$(TEMP)$(SUB)/viz/%.o)

TESTS = $(patsubst tests/%.c,$(BIN)$(SUB)/%,$(call getfiles,tests,%.c))


# Generated dependency files
-include $(LIB_OBJS:.o=.d)
-include $(VIZ_OBJS:.o=.d)


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

$(BUILD)$(SUB)/cimgui/cimgui.a:
	@$(MAKE) $(MFLAGS_ALL) MAKEDIR=$(BUILD)$(SUB)/cimgui .makedir
	@cd $(BUILD)$(SUB)/cimgui && cmake $(CIMGUI_FLAGS) $(CURDIR)/deps/cimgui && $(MAKE)


# Object files
$(TEMP)$(SUB)/lib/%.o: lib/%.c
	@$(MAKE) $(MFLAGS_ALL) MAKEDIR=$(@D) .makedir
	$(CC) $(LIB_FLAGS) $< -o $@

$(TEMP)$(SUB)/viz/%.o: viz/%.c
	@$(MAKE) $(MFLAGS_ALL) MAKEDIR=$(@D) .makedir
	$(CC) $(OFLAGS) -Iviz $< -o $@

# Library file
$(BIN)$(SUB)/libgroufix$(LIBEXT): $(DEPS) $(DEPS_EXPORT) $(LIB_OBJS)
	@$(MAKE) $(MFLAGS_ALL) MAKEDIR=$(@D) .makedir
	$(CXX) $(LIB_OBJS) -o $@ $(DEPS) -Wl,--push-state,--whole-archive $(DEPS_EXPORT) -Wl,--pop-state $(LIB_LDFLAGS)

# Program files
$(BIN)$(SUB)/grouviz$(BINEXT): $(VIZ_OBJS) $(BIN)$(SUB)/libgroufix$(LIBEXT)
	$(CC) $(VIZ_OBJS) -o $@ $(CFLAGS) -L$(BIN)$(SUB) -Wl,-rpath,'$$ORIGIN' -lgroufix

$(BIN)$(SUB)/%$(BINEXT): tests/%.c tests/test.h $(BIN)$(SUB)/libgroufix$(LIBEXT)
	$(CC) -Itests -Ideps/cimgui $< -o $@ $(TFLAGS) -L$(BIN)$(SUB) -Wl,-rpath,'$$ORIGIN' -lgroufix


# Platform builds
MFLAGS_ALL  = --no-print-directory
MFLAGS_UNIX = $(MFLAGS_ALL) SUB=/unix LIBEXT=.so
MFLAGS_WIN  = $(MFLAGS_ALL) SUB=/win LIBEXT=.dll BINEXT=.exe

.PHONY: .build .build-grouviz .build-tests
.build: $(BIN)$(SUB)/libgroufix$(LIBEXT)
.build-grouviz: $(BIN)$(SUB)/grouviz$(BINEXT)
.build-tests: $(TESTS:%=%$(BINEXT))


.PHONY: unix unix-grouviz unix-tests unix-all
unix:
	@$(MAKE) $(MFLAGS_UNIX) .build
unix-grouviz:
	@$(MAKE) $(MFLAGS_UNIX) .build-grouviz
unix-tests:
	@$(MAKE) $(MFLAGS_UNIX) .build-tests
unix-all:
	@$(MAKE) $(MFLAGS_UNIX) .build-grouviz .build-tests

.PHONY: win win-grouviz win-tests win-all
win:
	@$(MAKE) $(MFLAGS_WIN) .build
win-grouviz:
	@$(MAKE) $(MFLAGS_WIN) .build-grouviz
win-tests:
	@$(MAKE) $(MFLAGS_WIN) .build-tests
win-all:
	@$(MAKE) $(MFLAGS_WIN) .build-grouviz .build-tests
