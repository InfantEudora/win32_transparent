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

include $(ROOT)/engine.mk
