#=======================================================================================
# Ship - a character aboard a ship, asteroids, pickups and doors, under raymarched
# volumetric clouds that light, shadow and occlude the scene.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/ship.exe 2>stderr.log &
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := ship

APP_SRCS += main.cpp
APP_SRCS += ApplicationShip.cpp
APP_SRCS += ShipCharacter.cpp
APP_SRCS += Asteroid.cpp
APP_SRCS += AsteroidExplosion.cpp
APP_SRCS += HingedDoor.cpp
APP_SRCS += Pickup.cpp

#No SoundSystem here, so OpenAL is not linked - see the USE_SOUND block in engine.mk.

include $(ROOT)/engine.mk
