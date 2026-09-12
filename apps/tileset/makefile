#=======================================================================================
# Tileset - tiling and pathfinding on an isometric terrain. Cars find their way along the
# roads you lay down.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/tileset.exe 2>stderr.log &
#
# isoterrain/ at the repo root is a SHARED library - Grid and IsoAnimation build it too, so
# it stays where it is rather than moving in here. isocity/ was used by this app alone and
# has moved in (IsoCar/IsoHouse/IsoPath). See docs/asset_layout_plan.md section 2.1.
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := tileset

APP_SRCS += main.cpp
APP_SRCS += ApplicationTileset.cpp
APP_SRCS += IsoCar.cpp
APP_SRCS += IsoHouse.cpp
APP_SRCS += IsoPath.cpp

#The shared isometric terrain library - Grid and IsoAnimation build it too, so it goes in
#LIB_DIRS and compiles once into the shared object tree. See engine.mk.
LIB_DIRS += $(ROOT)/isoterrain
IPATHS   += -I$(ROOT)/isoterrain

#A car horn - see core/SoundSystem.h.
USE_SOUND := 1

include $(ROOT)/engine.mk
