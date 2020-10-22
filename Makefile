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
	@echo ""
	@echo "Available commands:"
	@echo "~~~~~~~~~~~~~~~~~~~"
	@echo "$(MAKE) clean         Clean temporary files."
	@echo "$(MAKE) clean-all     Clean all files make produced."
	@echo "~~~~~~~~~~~~~~~~~~~"
	@echo "$(MAKE) unix          Build the groufix Unix target."
	@echo "$(MAKE) win           Build the groufix Windows target."
	@echo ""


##############################
# Build environment

CC    = gcc
BIN   = bin
BUILD = build
OUT   = obj
SUB   = /.

DEBUG = OFF


# Flags for all binaries
ifeq ($(DEBUG),ON)
	DFLAGS = -g -Og
else
	DFLAGS = -DNDEBUG -O3
endif

CFLAGS = -std=c11 -Wall -Wconversion -Wsign-compare -pedantic -Iinclude $(DFLAGS)


# Flags for library files only
OFLAGS      = $(CFLAGS) -c -s -Ideps/glfw/include -DGFX_BUILD_LIB
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

ifeq ($(CC),i686-w64-mingw32-gcc)
	GLFW_FLAGS = $(GLFW_CONF) -DCMAKE_TOOLCHAIN_FILE=CMake/i686-w64-mingw32.cmake
else ifeq ($(CC),x86_64-w64-mingw32-gcc)
	GLFW_FLAGS = $(GLFW_CONF) -DCMAKE_TOOLCHAIN_FILE=CMake/x86_64-w64-mingw32.cmake
else
	GLFW_FLAGS = $(GLFW_CONF)
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
else
	@mkdir -p $(BUILD)$(SUB)/glfw
endif

$(OUT)$(SUB):
ifeq ($(OS),Windows_NT)
	$(eval OUTSUB_W = $(subst /,\,$(OUT)$(SUB)))
	@if not exist $(OUTSUB_W)\nul mkdir $(OUTSUB_W)
else
	@mkdir -p $(OUT)$(SUB)
endif


# Cleaning directories
clean:
ifeq ($(OS),Windows_NT)
	$(eval BUILD_W = $(subst /,\,$(BUILD)))
	$(eval OUT_W = $(subst /,\,$(OUT)))
	@if exist $(BUILD_W)\nul rmdir /s /q $(BUILD_W)
	@if exist $(OUT_W)\nul rmdir /s /q $(OUT_W)
else
	@rm -Rf $(BUILD)
	@rm -Rf $(OUT)
endif

clean-all: clean
ifeq ($(OS),Windows_NT)
	$(eval BIN_W = $(subst /,\,$(BIN)))
	@if exist $(BIN_W)\nul rmdir /s /q $(BIN_W)
else
	@rm -Rf $(BIN)
endif


##############################
# Dependency files for all builds

HEADERS = \
 include/groufix/utils.h \
 include/groufix.h


OBJS = \
 $(OUT)$(SUB)/groufix.o


LIBS = \
 $(BUILD)$(SUB)/glfw/src/libglfw3.a


##############################
# All available builds

$(BUILD)$(SUB)/glfw/src/libglfw3.a: | $(BUILD)$(SUB)
	@cd $(BUILD)$(SUB)/glfw && cmake $(GLFW_FLAGS) $(CURDIR)/deps/glfw && $(MAKE)


# Unix builds
$(OUT)/unix/%.o: src/%.c $(HEADERS) | $(OUT)/unix
	$(CC) $(OFLAGS_UNIX) $< -o $@

$(BIN)/unix/libgroufix.so: $(LIBS) $(OBJS) | $(BIN)/unix
	$(CC) -Wl,--whole-archive $(LIBS) -Wl,--no-whole-archive $(OBJS) -o $@ $(LFLAGS_UNIX)

unix:
	@$(MAKE) --no-print-directory $(BIN)/unix/libgroufix.so SUB=/unix


# Windows builds
$(OUT)/win/%.o: src/%.c $(HEADERS) | $(OUT)/win
	$(CC) $(OFLAGS_WIN) $< -o $@

$(BIN)/win/libgroufix.dll: $(LIBS) $(OBJS) | $(BIN)/win
	$(CC) -Wl,--whole-archive $(LIBS) -Wl,--no-whole-archive $(OBJS) -o $@ $(LFLAGS_WIN)

win:
	@$(MAKE) --no-print-directory $(BIN)/win/libgroufix.dll SUB=/win
