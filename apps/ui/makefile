#=======================================================================================
# UI - a bare harness for working on the ImGui debug panels and the engine's own widgets,
# with no gameplay in the way.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/ui.exe 2>stderr.log &
#
# There is no assets/ folder here - it loads only the shared default shaders and font.
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := ui

APP_SRCS += main.cpp
APP_SRCS += ApplicationUI.cpp

#Assets baked into the exe rather than read off the disk beside it. UI is the proving ground
#for this: it owns no assets, so its whole payload is shared_assets and the bake either
#covers it completely or the app does not start. Off by default everywhere else - see the
#BAKED ASSETS block in engine.mk, and tools/assetpack_plan.md.
#
#Pass BAKE_ASSETS=0 on the command line to build this app the ordinary loose way.
BAKE_ASSETS ?= 0
ASSET_ROOTS := $(ROOT)/shared_assets

#Neither is loaded by anything at run time: CascadiaMono is a second font nothing selects, and
#mono_sdf.png is written by tools/fontbake purely so the atlas can be eyeballed - its own header
#comment says so. Together they are 444 KB of the 2.3 MB shared tree, and this app would
#otherwise carry both in its binary. Measured 2026-09-14.
ASSET_PACK_FLAGS += --exclude "fonts/CascadiaMono.ttf"
ASSET_PACK_FLAGS += --exclude "fonts/mono_sdf.png"

include $(ROOT)/engine.mk
