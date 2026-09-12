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

include $(ROOT)/engine.mk
