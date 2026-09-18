#=======================================================================================
# Offline tools, as something a tool's makefile includes.
#
# The same idea as apps/: every tool under tools/<name>/ has its own folder, its own
# makefile and its own build/<name>.exe, and gets here by setting three things:
#
#     ROOT      := ../..           where the repo root is, seen from the tool folder
#     PROJECT   := fontbake        the exe name
#     TOOL_SRCS := fontbake.cpp    the tool's own sources, relative to the tool folder
#     include $(ROOT)/tools/tools.mk
#
# and optionally:
#
#     CORE_SRCS += $(ROOT)/core/File.cpp    engine sources to compile INTO this tool
#     LIBS      += -lws2_32                 extra link flags
#
# Build and run from the tool's folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"          # the toolchain is NOT on the default PATH
#     cd tools/fontbake && mingw32-make.exe -j8          # mingw32-make, not /usr/bin/make
#     ./build/fontbake.exe --help
#
#     mingw32-make.exe CONFIG=release -j8                # -> build/fontbake_release.exe
#
#=======================================================================================
# TOOLS ARE WIN32-ONLY, AND THAT IS A DECISION RATHER THAN A LIMITATION.
#
# Assets for the Android port are produced on a PC and shipped as assets; nothing here
# ever runs on a device. So a tool may use anything in core/ and anything in 3rdparty/
# without a thought for GLES, and it never needs the portability care that
# docs/ui_overlay_plan.md spends on the runtime side.
#=======================================================================================
# WHY CORE SOURCES ARE COMPILED IN, RATHER THAN LINKED FROM $(ROOT)/build/core
#
# engine.mk shares one set of core objects between all fourteen apps, and the rule that
# makes that safe is that they are compiled with CORE_CFLAGS and nothing else - because
# make compares objects by timestamp and cannot see the flags they were built with. If a
# tool compiled core into that shared tree with its own flags, whichever built first would
# win and every app would silently link objects built for someone else. Nothing would
# rebuild and nothing would warn.
#
# Linking the shared objects read-only would avoid that but trades it for a worse problem:
# the tool's own translation units would be compiled with different flags than the objects
# they link against (-DDEBUG in particular changes what headers declare), which is an ODR
# mismatch that shows up as a crash rather than a link error.
#
# So a tool NAMES the core sources it wants and they are compiled into the tool's own
# build folder. It costs a recompile per tool, which for a handful of files on a dev
# utility is free, and it makes the shared tree untouchable from here by construction.
#=======================================================================================

CC = g++

ifndef ROOT
$(error ROOT is not set - a tool makefile must set ROOT to the repo root before including tools.mk)
endif
ifndef PROJECT
$(error PROJECT is not set - a tool makefile must set PROJECT to its exe name before including tools.mk)
endif
ifndef TOOL_SRCS
$(error TOOL_SRCS is not set - a tool makefile must list its own sources before including tools.mk)
endif

.DEFAULT_GOAL := default

#---------------------------------------------------------------------------------------
# BUILD CONFIGURATION
#
# The same two configurations engine.mk has, spelled the same way, for the same reasons -
# see its own CONFIG block for the long version. In short: each configuration gets its own
# object tree so nothing collides and switching costs one link rather than a rebuild, and
# the EXE IS NAMED PER CONFIGURATION because otherwise building release and switching back
# to debug leaves make comparing the release exe against older debug objects, reporting
# "nothing to be done", and you keep running the binary you thought you had replaced.
#
# This file used to argue the opposite - that a tool runs for a second on a workstation and
# is not worth two object trees. That was true of fontbake and assetpack, which run and
# exit. It stopped being true when tools/lockd arrived: a broker that sits running for days
# is a program you have a reason to build small and optimised, and the argument for the
# apps' split applies to it unchanged.
#
# What does NOT move is the folder the exe sits in, exactly as in engine.mk. lockd finds
# the repo root by counting three levels up from its own path (DefaultRepoRoot), so a
# build/<config>/ exe would silently resolve every claimed path against the wrong root.
#
# NO CLOSED LIST OF SETTINGS HERE, unlike engine.mk, and that is deliberate rather than an
# omission. engine.mk rejects a command-line variable that is not one of its settings, to
# catch `make CONIFG=release` building debug quietly. The same guard cannot live here:
# engine.mk RECURSES into tools/assetpack for BAKE_ASSETS builds, and a command-line
# assignment propagates to a sub-make through MAKEFLAGS, so `make ship` would arrive with
# BAKE_ASSETS=1 USE_MCP=0 USE_NET=0 USE_IMGUI=0 in hand and a closed list would refuse a
# build that is perfectly correct. CONFIG still validates its own value below, which is the
# half of the guard that fits.
#---------------------------------------------------------------------------------------
CONFIG ?= debug

ifeq ($(CONFIG),debug)
CONFIG_SUFFIX :=
else ifeq ($(CONFIG),release)
CONFIG_SUFFIX := _release
else
$(error CONFIG must be 'debug' or 'release', not '$(CONFIG)')
endif

BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj/$(CONFIG)

CFLAGS += -std=c++17 -D_WIN32

#-O2 rather than engine.mk's -Og for the debug build: a tool has no frame budget to protect
#and no hot loop worth stepping through, so the faster default costs nothing and the -g is
#what actually matters when one asserts. Release matches engine.mk exactly.
DFLAGS = -DDEBUG -O2 -g
RFLAGS = -DRELEASE -O3 -s #O3 highest optimisation #-s to strip symbols
CFLAGS += $(if $(CONFIG_SUFFIX),$(RFLAGS),$(DFLAGS))

#Matching engine.mk, so a core source compiled in here behaves the way it does in an app.
#Neither is inert: core/ is written without exceptions, and building it with them enabled
#would change what the compiler emits around every allocation in it.
CFLAGS += -fno-exceptions -DJSON_NOEXCEPTION

#Same reasoning as engine.mk's copy - tinygltf's built-in file I/O and its serializer are
#both dead weight, and both macros must be seen by every translation unit that constructs
#a TinyGLTF or they disagree about the object's layout. Harmless in a tool that never
#touches glTF.
CFLAGS += -DTINYGLTF_NO_FS
CFLAGS += -DTINYGLTF_NO_WRITER

CFLAGS += -Wall

#Static, so a tool exe can be run from anywhere without the MSYS runtime on PATH. Tools
#get copied around and invoked from scripts more than apps do.
LFLAGS += -static-libstdc++ -static-libgcc -static

IPATHS += -I$(ROOT)/core/
IPATHS += -I$(ROOT)/core/physics
IPATHS += -I$(ROOT)/core/skeleton
IPATHS += -I$(ROOT)/3rdparty/
IPATHS += -I$(ROOT)/3rdparty/imgui/
IPATHS += -I$(ROOT)/3rdparty/stb_image/
IPATHS += -I$(ROOT)/3rdparty/stb_truetype/
IPATHS += -I$(ROOT)/3rdparty/miniz/
IPATHS += -I$(ROOT)/3rdparty/reactphysics3d/
#The tool's own folder, so its headers find each other by plain name.
IPATHS += -I.

#---------------------------------------------------------------------------------------
# Objects
#
# The tool's own sources keep their own names; core sources named by the tool are mirrored
# under the same build folder by their path below the repo root, so core/File.cpp becomes
# build/obj/<config>/core/File.o and cannot collide with a tool source of the same name.
#---------------------------------------------------------------------------------------
TOOL_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(TOOL_SRCS))

#CORE_SRCS is filtered by suffix rather than substituted blind, because a .c in the list
#would otherwise survive patsubst unchanged and be handed to the linker as a source file -
#which works, silently, with none of the flags or dependency tracking below. The only .c
#sources a tool names today are miniz's; see tools/assetpack/makefile.
CORE_OBJS := $(patsubst $(ROOT)/%.cpp,$(OBJ_DIR)/%.o,$(filter %.cpp,$(CORE_SRCS)))
CORE_OBJS += $(patsubst $(ROOT)/%.c,$(OBJ_DIR)/%.o,$(filter %.c,$(CORE_SRCS)))

#Header dependency tracking, for the same reason engine.mk has it: without it a stale
#object can link old code against a changed header and the mismatch shows up at run time.
DEPFLAGS = -MMD -MP
DEPS = $(TOOL_OBJS:.o=.d) $(CORE_OBJS:.o=.d)

#---------------------------------------------------------------------------------------
# Rules
#---------------------------------------------------------------------------------------
#The goal is the exe BY ITS REAL PATH - naming it $(PROJECT).exe while writing it into
#build/ would mean make never finds what it just built and relinks every time.
EXE := $(BUILD_DIR)/$(PROJECT)$(CONFIG_SUFFIX).exe

default: $(EXE)

$(EXE): $(TOOL_OBJS) $(CORE_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $^ -o $@ $(LFLAGS) $(LIBS)
	@echo "Built $@ ($(CONFIG))"

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPFLAGS) $(CFLAGS) $(IPATHS) $< -o $@

$(OBJ_DIR)/%.o: $(ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPFLAGS) $(CFLAGS) $(IPATHS) $< -o $@

#$(CC) and $(CFLAGS), the C++ ones, on a C source. That is the same choice 3rdparty/makefile
#makes for miniz and for the same reason: these files were .cpp in this repo until miniz
#became a submodule, so compiling them any other way would be a change of compiler arriving
#under cover of a change of file extension. A tool naming a genuinely C-only library here
#would need its own compiler, not this rule.
$(OBJ_DIR)/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPFLAGS) $(CFLAGS) $(IPATHS) $< -o $@

-include $(DEPS)

clean:
	-rm -rf $(BUILD_DIR)

.PHONY: default clean
