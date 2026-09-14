#=======================================================================================
# Tetris - guideline-ish falling blocks, with a signed-distance field glow on the well.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/tetris.exe 2>stderr.log &
#
# There is no assets/ folder here - every file Tetris loads is shared. See main.cpp.
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := tetris

APP_SRCS += main.cpp
APP_SRCS += ApplicationTetris.cpp
APP_SRCS += Playfield.cpp
APP_SRCS += Tetromino.cpp

#Line clears, lock-downs and rotations play sounds - see core/SoundSystem.h.
USE_SOUND := 1

#---------------------------------------------------------------------------------------
# Baking, DELIBERATELY OFF BY DEFAULT here - unlike apps/ui, which is the proving ground.
#
# Tetris is the app closest to shipping (docs/engine_backlog.md items 79-82), so what it is
# worth is the MEASUREMENT of baked against loose, and that is only worth anything if the
# ordinary build stays exactly what it was. Ask for it explicitly:
#
#     mingw32-make.exe CONFIG=release BAKE_ASSETS=1 -j8   -> build/tetris_baked_release.exe
#
# The roots below are only read when BAKE_ASSETS is 1, so declaring them costs a loose build
# nothing. See the BAKED ASSETS block in engine.mk and tools/assetpack_plan.md.
#---------------------------------------------------------------------------------------
ASSET_ROOTS := $(ROOT)/shared_assets

#Neither is loaded by anything at run time - CascadiaMono is a second font nothing selects, and
#mono_sdf.png is written by tools/fontbake only so the atlas can be eyeballed, as its own header
#says. 444 KB of the 2.3 MB shared tree. Measured 2026-09-14.
ASSET_PACK_FLAGS += --exclude "fonts/CascadiaMono.ttf"
ASSET_PACK_FLAGS += --exclude "fonts/mono_sdf.png"

include $(ROOT)/engine.mk
