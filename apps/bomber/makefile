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

#The bomb, the shears, the pickups, the exit and the level boundary all make noise - see the
#sound block in ApplicationBomber.cpp. This pulls core/SoundSystem.cpp and core/WaveFile.cpp
#into the build; see the USE_SOUND block in engine.mk for why it is opt-in per app.
USE_SOUND := 1

#---------------------------------------------------------------------------------------
# What `make ship` bakes into the exe.
#
# THE SAME TWO ROOTS main.cpp declares, IN THE SAME ORDER - this app's own first so its
# shaders/bomber_explosion.frag wins, then the shared tree for default.vert, noise3d.comp
# and the overlay's font. The order matters for the same reason it matters at run time, and
# nothing checks that these two lists agree, so they are worth reading together.
#
# Only read when BAKE_ASSETS is 1, so this costs an ordinary build nothing. See the BAKED
# ASSETS block in engine.mk and tools/assetpack_plan.md.
#---------------------------------------------------------------------------------------
ASSET_ROOTS := assets $(ROOT)/shared_assets

#---------------------------------------------------------------------------------------
# What it does NOT bake: 1.1 MB of the shared tree this app never asks for.
#
# EVERY ONE OF THESE WAS CHECKED RATHER THAN ASSUMED - nothing in core/ or in this app names
# any of them outside a comment. That check is the whole job here: an exclusion that is wrong
# does not fail the build, it produces an exe that dies on its first LoadFile, on a machine
# that has no loose copy to fall back on.
#
# --exclude MATCHES THE ASSET NAME, NOT THE PATH, and the asset name is what a name resolves
# to across ALL roots - so `sound/*` here would silently drop this app's OWN seven wavs along
# with the shared four. The four are named one at a time for exactly that reason.
#---------------------------------------------------------------------------------------
ASSET_PACK_FLAGS += --exclude "fonts/CascadiaMono.ttf"      #371 KB - nothing selects this font
ASSET_PACK_FLAGS += --exclude "fonts/mono_sdf.png"          #73 KB - fontbake writes it to be eyeballed
ASSET_PACK_FLAGS += --exclude "meshes/glyphs_unispace.glb"  #312 KB - TextMesh's; this app uses UIOverlay
#breakout's and the sound tests'. This app ships its own seven under the same category.
ASSET_PACK_FLAGS += --exclude "sound/bleep.wav"
ASSET_PACK_FLAGS += --exclude "sound/click.wav"
ASSET_PACK_FLAGS += --exclude "sound/floop.wav"
ASSET_PACK_FLAGS += --exclude "sound/hax.wav"

include $(ROOT)/engine.mk

#The rules test. Its own two-line rule rather than anything in engine.mk, because it links
#no core object and must not be able to drift into depending on one - the moment it needs
#the engine it has stopped testing what it is for. See the note at the top.
.PHONY: rules
rules:
	@mkdir -p build
	$(CXX) -std=c++17 -Wall -g -o build/maze_test.exe maze_test.cpp Maze.cpp Hallway.cpp
	@./build/maze_test.exe
