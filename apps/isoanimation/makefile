#=======================================================================================
# IsoAnimation - animated characters on an isometric terrain, lit by an HDR environment.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/isoanimation.exe 2>stderr.log &
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := isoanimation

APP_SRCS += main.cpp
APP_SRCS += ApplicationIsoAnimation.cpp

#The shared isometric terrain library - Grid and Tileset build it too, so it goes in
#LIB_DIRS and compiles once into the shared object tree. See engine.mk.
LIB_DIRS += $(ROOT)/isoterrain
IPATHS   += -I$(ROOT)/isoterrain

include $(ROOT)/engine.mk
