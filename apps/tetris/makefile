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
# THAT LINE IS THE MEASUREMENT, NOT THE SHIPPING BUILD - it bakes the assets and changes
# nothing else, which is what makes it comparable against the loose release. A build meant
# for a player also drops the MCP server, the sockets and the ImGui panels, and that whole
# set is `make ship` (see the ship block in engine.mk). Use this line to answer "what does
# baking cost?"; use `make ship` to produce something to hand over.
#
# The roots below are only read when BAKE_ASSETS is 1, so declaring them costs a loose build
# nothing. See the BAKED ASSETS block in engine.mk and tools/assetpack_plan.md.
#---------------------------------------------------------------------------------------
ASSET_ROOTS := $(ROOT)/shared_assets

#---------------------------------------------------------------------------------------
# WHAT NOT TO PACK, IN THREE GROUPS - and the grouping is the point, because these are three
# different KINDS of claim and only two of them are safe to make unconditionally.
#
# All of it measured 2026-09-14, by baking everything and reading what the app asked for at
# shutdown (BinaryAsset::ListBinaryAssets, called from main.cpp for exactly this).
#---------------------------------------------------------------------------------------

#NOTHING IN THE REPO LOADS THESE - facts about the asset TREE, true for every app and every
#build. CascadiaMono is a second font nothing selects; mono_sdf.png is written by tools/fontbake
#only so the atlas can be eyeballed, as its own header says; shaders/texture.comp has no loader
#anywhere, its only trace being a comment in apps/ship's noise3d.comp saying the voronoi was
#lifted out of it. 444 KB of the 2.3 MB shared tree.
ASSET_PACK_FLAGS += --exclude "fonts/CascadiaMono.ttf"
ASSET_PACK_FLAGS += --exclude "fonts/mono_sdf.png"
ASSET_PACK_FLAGS += --exclude "shaders/texture.comp"

#TETRIS DOES NOT DRAW A SKYBOX - a fact about this APP rather than the tree. apps/grid,
#apps/isoanimation and apps/pinball all load these, so this is correct only because the exclude
#list is per-app. Still unconditional: no flag gives Tetris a skybox.
ASSET_PACK_FLAGS += --exclude "shaders/skybox.frag"
ASSET_PACK_FLAGS += --exclude "shaders/skybox.vert"

#IMGUI'S FONT, AND THE ONE THAT CANNOT BE UNCONDITIONAL. This is a fact about a BUILD of Tetris,
#not about Tetris: core/WindowImGui.cpp:64 loads it, and USE_IMGUI=0 is what drops that
#translation unit. Excluded outright it breaks every baked build that still HAS ImGui - including
#the CONFIG=release BAKE_ASSETS=1 measurement build documented above, which died during startup
#with "LoadFile failed to load [fonts/consola.ttf]" and, ASSETS_BAKED having removed the search
#root, no disk copy to fall back on.
#
#USE_IMGUI IS NOT SET YET AT THIS POINT. engine.mk is included at the BOTTOM of this file, and
#that is where the flag family gets its defaults - so a bare `ifneq ($(USE_IMGUI),1)` here reads
#an EMPTY value and excludes in every build that did not name the flag on the command line, which
#is precisely the bug being fixed. Mirror the default first, then test it the way engine.mk does.
#A command-line USE_IMGUI=0 still wins: ?= does not overwrite one.
USE_IMGUI ?= 1
ifneq ($(USE_IMGUI),1)
ASSET_PACK_FLAGS += --exclude "fonts/consola.ttf"
endif

#The general form of that last one: an exclusion is a claim about what the CODE loads, and only
#the code knows that. Backlog items 90 and 91; docs/asset_declaration_plan.md is the agreed
#replacement for this whole block - ASSET_NEEDS next to the LoadFile call, and no list here.

include $(ROOT)/engine.mk
