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
#
#     mingw32-make.exe rules                         # -> build/maze_test.exe, then runs it
#
# `rules` builds maze_test.cpp against Maze.cpp and Hallway.cpp ALONE - no core, no window, no
# GPU - and checks the game's rules: a blast clearing a hedge and stopping at it, an enemy
# cutting through one, a pickup staying buried until the block above it goes, the bridge
# refusing a step across its rope side, and 200 seeds all laying out a playable board. It
# is possible at all because Maze.h names no engine type, which is the whole reason for
# that split; it takes about a second and is the cheapest way to find out whether a change
# to the rules broke one of them. It is NOT part of the normal build and nothing depends
# on it.
#=======================================================================================

ROOT    := ../..
PROJECT := bomber

APP_SRCS += main.cpp
APP_SRCS += ApplicationBomber.cpp
APP_SRCS += Maze.cpp
APP_SRCS += Hallway.cpp

#No SoundSystem yet, so OpenAL is not linked - see the USE_SOUND block in engine.mk. A bomb
#that makes no noise is a gap this app will want closing, but it is a property of the app
#rather than of the build, so it turns on here when there is a wav to play.

include $(ROOT)/engine.mk

#The rules test. Its own two-line rule rather than anything in engine.mk, because it links
#no core object and must not be able to drift into depending on one - the moment it needs
#the engine it has stopped testing what it is for. See the note at the top.
.PHONY: rules
rules:
	@mkdir -p build
	$(CXX) -std=c++17 -Wall -g -o build/maze_test.exe maze_test.cpp Maze.cpp Hallway.cpp
	@./build/maze_test.exe
