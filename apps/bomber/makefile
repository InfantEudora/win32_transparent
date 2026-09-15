#=======================================================================================
# bomber - a volumetric-effects bench on its way to becoming a bomberman game.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/bomber.exe 2>stderr.log &
#
# The explosion is shaders/bomber_explosion.frag and can be edited while the app runs -
# F5, the Explosion panel's Reload button, or the bomber_reload_shader MCP tool all
# recompile it from disk. So a shader change costs no rebuild and no relaunch.
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := bomber

APP_SRCS += main.cpp
APP_SRCS += ApplicationBomber.cpp
APP_SRCS += Maze.cpp

#No SoundSystem yet, so OpenAL is not linked - see the USE_SOUND block in engine.mk. A bomb
#that makes no noise is a gap this app will want closing, but it is a property of the app
#rather than of the build, so it turns on here when there is a wav to play.

include $(ROOT)/engine.mk
