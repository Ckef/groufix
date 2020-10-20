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
	@echo ""


##############################
# Build environment

BIN = bin
OUT = obj
CC  = gcc

DEBUG = OFF


# Flags for all binaries
ifeq ($(DEBUG),ON)
	DFLAGS = -g -Og
else
	DFLAGS = -O3
endif

CFLAGS = -std=c11 -Wall -Wconversion -Wsign-compare -pedantic $(DFLAGS)


# Flags for library files only
OFLAGS      = $(CFLAGS) -c -s
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
	$(eval BIN_W = $(subst /,\,$(BIN)))
	@if not exist $(BIN_W)\nul mkdir $(BIN_W)
else
	@mkdir -p $(BIN)
endif

$(OUT):
ifeq ($(OS),Windows_NT)
	$(eval OUT_W = $(subst /,\,$(OUT)))
	@if not exist $(OUT_W)\nul mkdir $(OUT_W)
else
	@mkdir -p $(OUT)
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
