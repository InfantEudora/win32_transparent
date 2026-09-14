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
#     export PATH="/c/msys64/mingw64/bin:$PATH"     # the toolchain is NOT on the default PATH
#     cd tools/fontbake && mingw32-make.exe -j8     # mingw32-make, not /usr/bin/make
#     ./build/fontbake.exe --help
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

BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj

#ONE CONFIGURATION, unlike engine.mk's debug/release split. A tool is a developer utility
#that runs on a workstation for a second or two; there is nothing here worth two object
#trees and two exe names to choose between. -O2 -g is fast enough to be unnoticeable and
#still gives a usable stack trace when one asserts.
CFLAGS += -std=c++17 -O2 -g -D_WIN32

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
# build/obj/core/File.o and cannot collide with a tool source of the same name.
#---------------------------------------------------------------------------------------
TOOL_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(TOOL_SRCS))
CORE_OBJS := $(patsubst $(ROOT)/%.cpp,$(OBJ_DIR)/%.o,$(CORE_SRCS))

#Header dependency tracking, for the same reason engine.mk has it: without it a stale
#object can link old code against a changed header and the mismatch shows up at run time.
DEPFLAGS = -MMD -MP
DEPS = $(TOOL_OBJS:.o=.d) $(CORE_OBJS:.o=.d)

#---------------------------------------------------------------------------------------
# Rules
#---------------------------------------------------------------------------------------
#The goal is the exe BY ITS REAL PATH - naming it $(PROJECT).exe while writing it into
#build/ would mean make never finds what it just built and relinks every time.
EXE := $(BUILD_DIR)/$(PROJECT).exe

default: $(EXE)

$(EXE): $(TOOL_OBJS) $(CORE_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $^ -o $@ $(LFLAGS) $(LIBS)
	@echo "Built $@"

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPFLAGS) $(CFLAGS) $(IPATHS) $< -o $@

$(OBJ_DIR)/%.o: $(ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPFLAGS) $(CFLAGS) $(IPATHS) $< -o $@

-include $(DEPS)

clean:
	-rm -rf $(BUILD_DIR)

.PHONY: default clean
