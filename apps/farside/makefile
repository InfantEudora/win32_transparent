#=======================================================================================
# Farside - the scale bench: a planet sunk through the flight plane, a moon, asteroids,
# hulls and two defence posts, a ship with a continuous altitude, and every size and
# camera number on a slider. See docs/README.md.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/farside_nophysics.exe --minimized 2>stderr.log &
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := farside

APP_SRCS += main.cpp
APP_SRCS += ApplicationFarside.cpp
APP_SRCS += Field.cpp
APP_SRCS += Flight.cpp

#No rigid bodies: the ship flies on its own integrator (Flight.h) and sight is plain geometry
#(Field.h). Hence _nophysics in the exe name. See the USE_PHYSICS block in engine.mk.
USE_PHYSICS := 0

#Not baked and not shippable yet: it reads meshes/ships.glb from apps/ship/assets (main.cpp).

include $(ROOT)/engine.mk
