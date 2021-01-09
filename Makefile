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
DEBUG = ON

BIN   = bin
BUILD = build
OUT   = obj
SUB   = /.


# Flags for all binaries
ifeq ($(DEBUG),ON)
	DFLAGS = -g -Og
else
	DFLAGS = -DNDEBUG -O3
endif

MFLAGS = --no-print-directory
CFLAGS = -std=c11 -Wall -Wconversion -Wsign-compare -Wshadow -pedantic -Iinclude $(DFLAGS)


# Flags for library files only
OFLAGS_INCLUDE = \
 -Isrc \
 -Ideps/glfw/include \
 -Ideps/Vulkan-Headers/include \
 -Ideps/shaderc/libshaderc/include

OFLAGS      = $(CFLAGS) -c -s -DGFX_BUILD_LIB $(OFLAGS_INCLUDE)
OFLAGS_UNIX = $(OFLAGS) -fPIC
OFLAGS_WIN  = $(OFLAGS)


# Linker flags
LFLAGS      = -shared
LFLAGS_UNIX = $(LFLAGS) -pthread -ldl -lm
LFLAGS_WIN  = $(LFLAGS) -lgdi32 -static-libgcc


# Dependency flags
GLFW_CONF = \
 -DBUILD_SHARED_LIBS=OFF \
 -DGLFW_BUILD_EXAMPLES=OFF \
 -DGLFW_BUILD_TESTS=OFF \
 -DGLFW_BUILD_DOCS=OFF

SHADERC_MINGW_TOOLCHAIN = \
 -DCMAKE_TOOLCHAIN_FILE=cmake/linux-mingw-toolchain.cmake \
 -Dgtest_disable_pthreads=ON

SHADERC_CONF      = -Wno-dev -DCMAKE_BUILD_TYPE=Release
SHADERC_CONF_UNIX = $(SHADERC_CONF) -G "Unix Makefiles"
SHADERC_CONF_WIN  = $(SHADERC_CONF) -G "MinGw Makefiles"

ifeq ($(CC),i686-w64-mingw32-gcc)
	GLFW_FLAGS    = $(GLFW_CONF) -DCMAKE_TOOLCHAIN_FILE=CMake/i686-w64-mingw32.cmake
	SHADERC_FLAGS = $(SHADERC_CONF_UNIX) $(SHADERC_MINGW_TOOLCHAIN)
else ifeq ($(CC),x86_64-w64-mingw32-gcc)
	GLFW_FLAGS    = $(GLFW_CONF) -DCMAKE_TOOLCHAIN_FILE=CMake/x86_64-w64-mingw32.cmake
	SHADERC_FLAGS = $(SHADERC_CONF_UNIX) $(SHADERC_MINGW_TOOLCHAIN)
else ifeq ($(OS),Windows_NT)
	GLFW_FLAGS    = $(GLFW_CONF) -DCMAKE_C_COMPILER=gcc -G "MinGW Makefiles"
	SHADERC_FLAGS = $(SHADERC_CONF_WIN)
else
	GLFW_FLAGS    = $(GLFW_CONF)
	SHADERC_FLAGS = $(SHADERC_CONF_UNIX)
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

clean-deps:
ifeq ($(OS),Windows_NT)
	$(eval BUILD_W = $(subst /,\,$(BUILD)))
	@if exist $(BUILD_W)\nul rmdir /s /q $(BUILD_W)
else
	@rm -Rf $(BUILD)
endif

clean-all: clean clean-deps
ifeq ($(OS),Windows_NT)
	$(eval BIN_W = $(subst /,\,$(BIN)))
	@if exist $(BIN_W)\nul rmdir /s /q $(BIN_W)
else
	@rm -Rf $(BIN)
endif


##############################
# Dependency files for all builds

HEADERS = \
 include/groufix/containers/vec.h \
 include/groufix/core/device.h \
 include/groufix/core/keys.h \
 include/groufix/core/log.h \
 include/groufix/core/renderer.h \
 include/groufix/core/window.h \
 include/groufix/def.h \
 include/groufix.h \
 src/groufix/core/objects.h \
 src/groufix/core/threads.h \
 src/groufix/core.h


OBJS = \
 $(OUT)$(SUB)/groufix/containers/vec.o \
 $(OUT)$(SUB)/groufix/core/device.o \
 $(OUT)$(SUB)/groufix/core/log.o \
 $(OUT)$(SUB)/groufix/core/monitor.o \
 $(OUT)$(SUB)/groufix/core/pass.o \
 $(OUT)$(SUB)/groufix/core/renderer.o \
 $(OUT)$(SUB)/groufix/core/state.o \
 $(OUT)$(SUB)/groufix/core/swap.o \
 $(OUT)$(SUB)/groufix/core/vulkan.o \
 $(OUT)$(SUB)/groufix/core/window.o \
 $(OUT)$(SUB)/groufix.o


LIBS_WA = \
 $(BUILD)$(SUB)/glfw/src/libglfw3.a
LIBS_NWA = \
 $(BUILD)$(SUB)/shaderc/libshaderc/libshaderc_combined.a

LIBS = $(LIBS_WA) $(LIBS_NWA)
LIBS_FLAGS = -Wl,--whole-archive $(LIBS_WA) -Wl,--no-whole-archive $(LIBS_NWA)


##############################
# All available builds

$(BUILD)$(SUB)/glfw/src/libglfw3.a: | $(BUILD)$(SUB)
	@cd $(BUILD)$(SUB)/glfw && cmake $(GLFW_FLAGS) $(CURDIR)/deps/glfw && $(MAKE)

$(BUILD)$(SUB)/shaderc/libshaderc/libshaderc_combined.a: | $(BUILD)$(SUB)
	@cd $(BUILD)$(SUB)/shaderc && cmake $(SHADERC_FLAGS) $(CURDIR)/deps/shaderc && $(MAKE)


# Unix builds
$(OUT)/unix/%.o: src/%.c $(HEADERS) | $(OUT)/unix
	$(CC) $(OFLAGS_UNIX) $< -o $@

$(BIN)/unix/libgroufix.so: $(LIBS) $(OBJS) | $(BIN)/unix
	$(CC) $(LIBS_FLAGS) $(OBJS) -o $@ $(LFLAGS_UNIX)

$(BIN)/unix/%: tests/%.c $(BIN)/unix/libgroufix.so
	$(CC) $(CFLAGS) $< -o $@ -L$(BIN)/unix -Wl,-rpath='$$ORIGIN' -lgroufix

unix:
	@$(MAKE) $(MFLAGS) $(BIN)/unix/libgroufix.so SUB=/unix
unix-tests:
	@$(MAKE) $(MFLAGS) $(BIN)/unix/minimal SUB=/unix


# Windows builds
$(OUT)/win/%.o: src/%.c $(HEADERS) | $(OUT)/win
	$(CC) $(OFLAGS_WIN) $< -o $@

$(BIN)/win/libgroufix.dll: $(LIBS) $(OBJS) | $(BIN)/win
	$(CC) $(LIBS_FLAGS) $(OBJS) -o $@ $(LFLAGS_WIN)

$(BIN)/win/%.exe: tests/%.c $(BIN)/win/libgroufix.dll
	$(CC) $(CFLAGS) $< -o $@ -L$(BIN)/win -Wl,-rpath='$$ORIGIN' -lgroufix

win:
	@$(MAKE) $(MFLAGS) $(BIN)/win/libgroufix.dll SUB=/win
win-tests:
	@$(MAKE) $(MFLAGS) $(BIN)/win/minimal.exe SUB=/win
