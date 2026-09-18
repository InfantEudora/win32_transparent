#=======================================================================================
# Tank - a tracked vehicle, a buggy and a joint-based crane test rig on a heightmap.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/tank.exe 2>stderr.log &
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app and are left alone; `make cleancore` is the one
# that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := tank

#This app's own sources. Everything else it needs is the engine, which engine.mk adds.
#main.cpp is this app's own - it names ApplicationTank directly, and declares where the
#app's assets are. See the comment in it.
APP_SRCS += main.cpp
APP_SRCS += ApplicationTank.cpp
APP_SRCS += TankCharacter.cpp
APP_SRCS += BuggyCharacter.cpp
APP_SRCS += Heightmap.cpp

#A joint-based test rig, unrelated to the tank and the buggy - see CraneCharacter's own
#header comment for what it is testing. Nothing else in the repo uses it.
APP_SRCS += CraneCharacter.cpp

#No SoundSystem here, so OpenAL is not linked - see the USE_SOUND block in engine.mk.

#---------------------------------------------------------------------------------------
# What `make ship` bakes into the exe.
#
# THE SAME TWO ROOTS main.cpp DECLARES, IN THE SAME ORDER - this app's own first so a file
# in apps/tank/assets wins over a shared one of the same name, then the shared tree. The
# order matters for the same reason it matters at run time, and nothing checks that these
# two lists agree, so they are worth reading together.
#
# Only read when BAKE_ASSETS is 1, so this costs an ordinary build nothing. See the BAKED
# ASSETS block in engine.mk.
#---------------------------------------------------------------------------------------
ASSET_ROOTS := assets $(ROOT)/shared_assets

#NOTHING IS EXCLUDED YET, and that is the safe state rather than the finished one. This app
#bakes about 12 MB raw where bomber bakes 7, and some of the shared tree is certainly dead
#weight here - but an exclusion that is wrong does not fail the build, it produces an exe
#that dies on its first LoadFile on a machine with no loose copy to fall back on. Each one
#has to be checked rather than assumed, which is a job of its own; see the worked example
#in apps/bomber/makefile for what that looks like when it is done.
#ASSET_PACK_FLAGS += --exclude "..."

include $(ROOT)/engine.mk
