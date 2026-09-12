#=======================================================================================
# Breakout - a paddle game with a signed-distance-field shield effect.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/breakout.exe 2>stderr.log &
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := breakout

APP_SRCS += main.cpp
APP_SRCS += ApplicationBreakout.cpp
APP_SRCS += Field.cpp

#The ball, the bricks, the shield and the paddle all make noise - see core/SoundSystem.h.
#This pulls core/SoundSystem.cpp and core/WaveFile.cpp back into the build and links
#OpenAL; see the USE_SOUND block in engine.mk for why it is opt-in.
USE_SOUND := 1

include $(ROOT)/engine.mk
