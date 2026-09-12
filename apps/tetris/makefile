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

include $(ROOT)/engine.mk
