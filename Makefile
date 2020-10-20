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

BIN = bin
OUT = obj
SUB = /.
CC  = gcc

DEBUG = OFF


# Flags for all binaries
ifeq ($(DEBUG),ON)
	DFLAGS = -g -Og
else
	DFLAGS = -O3
endif

CFLAGS = -std=c11 -Wall -Wconversion -Wsign-compare -pedantic -Iinclude $(DFLAGS)


# Flags for library files only
OFLAGS      = $(CFLAGS) -c -s -DGFX_BUILD_LIB
OFLAGS_UNIX = $(OFLAGS) -fPIC
OFLAGS_WIN  = $(OFLAGS)


# Linker flags
LFLAGS      = -shared
LFLAGS_UNIX = $(LFLAGS)
LFLAGS_WIN  = $(LFLAGS) -static-libgcc


##############################
# Directory management

$(BIN):
ifeq ($(OS),Windows_NT)
	$(eval BINSUB = $(subst /,\,$(BIN)$(SUB)))
	@if not exist $(BINSUB)\nul mkdir $(BINSUB)
else
	@mkdir -p $(BIN)$(SUB)
endif

$(OUT):
ifeq ($(OS),Windows_NT)
	$(eval OUTSUB = $(subst /,\,$(OUT)$(SUB)))
	@if not exist $(OUTSUB)\nul mkdir $(OUTSUB)
else
	@mkdir -p $(OUT)$(SUB)
endif


# Cleaning directories
clean:
ifeq ($(OS),Windows_NT)
	$(eval OUT_W = $(subst /,\,$(OUT)))
	@if exist $(OUT_W)\nul rmdir /s /q $(OUT_W)
else
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
# Shared files for all builds

HEADERS = \
 include/groufix/utils.h \
 include/groufix.h


OBJS = \
 $(OUT)$(SUB)/groufix.o


##############################
# Unix builds

$(OUT)/unix/%.o: src/%.c $(HEADERS) | $(OUT)
	$(CC) $(OFLAGS_UNIX) $< -o $@

$(BIN)/unix/libgroufix.so: $(OBJS) | $(BIN)
	$(CC) $(OBJS) -o $@ $(LFLAGS_UNIX)


unix:
	@$(MAKE) $(BIN)/unix/libgroufix.so SUB=/unix


##############################
# Windows builds

$(OUT)/win/%.o: src/%.c $(HEADERS) | $(OUT)
	$(CC) $(OFLAGS_WIN) $< -o $@

$(BIN)/win/libgroufix.dll: $(OBJS) | $(BIN)
	$(CC) $(OBJS) -o $@ $(LFLAGS_WIN)


win:
	@$(MAKE) $(BIN)/win/libgroufix.dll SUB=/win
