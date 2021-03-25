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
	@echo "  $(MAKE) clean      - Clean temporary files."
	@echo "  $(MAKE) clean-bin  - Clean build files."
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
CCPP  = $(subst gcc,g++,$(CC))
DEBUG = ON

BIN   = bin
BUILD = build
OUT   = obj


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
TFLAGS = $(CFLAGS) -pthread


# Flags for library files only
OFLAGS_ALL = \
 $(CFLAGS) -c -s -MP -MMD -DGFX_BUILD_LIB -Isrc \
 -Ideps/glfw/include \
 -Ideps/Vulkan-Headers/include \
 -Ideps/shaderc/libshaderc/include

ifeq ($(OS),Windows_NT)
 OFLAGS = $(OFLAGS_ALL)
else
 OFLAGS = $(OFLAGS_ALL) -fPIC
endif


# Linker flags
LFLAGS_ALL  = -shared -pthread
LFLAGS_UNIX = $(LFLAGS_ALL) -ldl
LFLAGS_WIN  = $(LFLAGS_ALL) -lgdi32 -static-libstdc++ -static-libgcc

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

SHADERC_FLAGS_ALL = \
 -Wno-dev \
 -DCMAKE_BUILD_TYPE=Release \
 -DSHADERC_SKIP_TESTS=ON \
 -DSPIRV_SKIP_TESTS=ON

SHADERC_MINGW_TOOLCHAIN = \
 -DCMAKE_TOOLCHAIN_FILE=cmake/linux-mingw-toolchain.cmake \
 -DMINGW_COMPILER_PREFIX=$(CC_PREFIX) \
 -Dgtest_disable_pthreads=ON

SHADERC_FLAGS_UNIX = $(SHADERC_FLAGS_ALL) -G "Unix Makefiles"
SHADERC_FLAGS_WIN  = $(SHADERC_FLAGS_ALL) -G "MinGW Makefiles"

ifneq ($(CC_PREFIX),None) # Cross-compile
 GLFW_FLAGS    = $(GLFW_FLAGS_ALL) -DCMAKE_TOOLCHAIN_FILE=CMake/$(CC_PREFIX).cmake
 SHADERC_FLAGS = $(SHADERC_FLAGS_UNIX) $(SHADERC_MINGW_TOOLCHAIN)
else ifeq ($(OS),Windows_NT)
 GLFW_FLAGS    = $(GLFW_FLAGS_ALL) -DCMAKE_C_COMPILER=$(CC) -G "MinGW Makefiles"
 SHADERC_FLAGS = $(SHADERC_FLAGS_WIN)
else
 GLFW_FLAGS    = $(GLFW_FLAGS_ALL)
 SHADERC_FLAGS = $(SHADERC_FLAGS_UNIX)
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
else
	@mkdir -p $(BUILD)$(SUB)/glfw
	@mkdir -p $(BUILD)$(SUB)/shaderc
endif

$(OUT)$(SUB):
ifeq ($(OS),Windows_NT)
	$(eval OUTSUB_W = $(subst /,\,$(OUT)$(SUB)))
	@if not exist $(OUTSUB_W)\groufix\containers\nul mkdir $(OUTSUB_W)\groufix\containers
	@if not exist $(OUTSUB_W)\groufix\core\nul mkdir $(OUTSUB_W)\groufix\core
else
	@mkdir -p $(OUT)$(SUB)/groufix/containers
	@mkdir -p $(OUT)$(SUB)/groufix/core
endif


# Cleaning directories
clean:
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


# Nuke everything
clean-all: clean clean-bin clean-deps


##############################
# Dependency files for all builds

OBJS = \
 $(OUT)$(SUB)/groufix/containers/list.o \
 $(OUT)$(SUB)/groufix/containers/tree.o \
 $(OUT)$(SUB)/groufix/containers/vec.o \
 $(OUT)$(SUB)/groufix/core/alloc.o \
 $(OUT)$(SUB)/groufix/core/device.o \
 $(OUT)$(SUB)/groufix/core/init.o \
 $(OUT)$(SUB)/groufix/core/log.o \
 $(OUT)$(SUB)/groufix/core/monitor.o \
 $(OUT)$(SUB)/groufix/core/pass.o \
 $(OUT)$(SUB)/groufix/core/renderer.o \
 $(OUT)$(SUB)/groufix/core/shader.o \
 $(OUT)$(SUB)/groufix/core/swap.o \
 $(OUT)$(SUB)/groufix/core/vulkan.o \
 $(OUT)$(SUB)/groufix/core/window.o \
 $(OUT)$(SUB)/groufix.o

LIBS = \
 $(BUILD)$(SUB)/glfw/src/libglfw3.a \
 $(BUILD)$(SUB)/shaderc/libshaderc/libshaderc_combined.a


# Generated dependency files
-include $(OBJS:.o=.d)


##############################
# All available builds

$(BUILD)$(SUB)/glfw/src/libglfw3.a: | $(BUILD)$(SUB)
	@cd $(BUILD)$(SUB)/glfw && cmake $(GLFW_FLAGS) $(CURDIR)/deps/glfw && $(MAKE)

$(BUILD)$(SUB)/shaderc/libshaderc/libshaderc_combined.a: | $(BUILD)$(SUB)
	@cd $(BUILD)$(SUB)/shaderc && cmake $(SHADERC_FLAGS) $(CURDIR)/deps/shaderc && $(MAKE)


# Object files
$(OUT)$(SUB)/%.o: src/%.c | $(OUT)$(SUB)
	$(CC) $(OFLAGS) $< -o $@

# Library file
$(BIN)$(SUB)/libgroufix$(EXT): $(LIBS) $(OBJS) | $(BIN)$(SUB)
	$(CCPP) $(OBJS) -o $@ $(LIBS) $(LFLAGS)

# Test programs
$(BIN)$(SUB)/$(PTEST): tests/%.c tests/test.h $(BIN)$(SUB)/libgroufix$(EXT)
	$(CC) $(TFLAGS) -Itests $< -o $@ -L$(BIN)$(SUB) -Wl,-rpath='$$ORIGIN' -lgroufix


# Platform builds
MFLAGS_ALL  = --no-print-directory
MFLAGS_UNIX = $(MFLAGS_ALL) SUB=/unix EXT=.so PTEST=%
MFLAGS_WIN  = $(MFLAGS_ALL) SUB=/win EXT=.dll PTEST=%.exe

unix:
	@$(MAKE) $(MFLAGS_UNIX) $(BIN)/unix/libgroufix.so
unix-tests:
	@$(MAKE) $(MFLAGS_UNIX) $(BIN)/unix/fps
	@$(MAKE) $(MFLAGS_UNIX) $(BIN)/unix/minimal
	@$(MAKE) $(MFLAGS_UNIX) $(BIN)/unix/threaded

win:
	@$(MAKE) $(MFLAGS_WIN) $(BIN)/win/libgroufix.dll
win-tests:
	@$(MAKE) $(MFLAGS_WIN) $(BIN)/win/fps.exe
	@$(MAKE) $(MFLAGS_WIN) $(BIN)/win/minimal.exe
	@$(MAKE) $(MFLAGS_WIN) $(BIN)/win/threaded.exe
