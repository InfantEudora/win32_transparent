#=======================================================================================
# archer - a side-view platformer prototype about an archer, in 3D assets.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/archer.exe 2>stderr.log &
#
#     A/D or arrows  run            Space  jump (hold it for height)
#     S              drop through   J      hold to draw the bow, release to loose
#     Up/Down        tilt the aim   R      restart        F1  the engine panels
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#
#     mingw32-make.exe rules                         # -> build/stage_test.exe, then runs it
#
# `rules` builds stage_test.cpp against Stage.cpp and Puppet.cpp ALONE - no core, no window,
# no GPU - and checks the game's rules: that a held jump peaks where v^2/2g minus the Euler half-step
# says, that a tapped one is markedly shorter, that coyote time and the jump buffer are
# windows rather than free passes, that a running jump clears the five-unit gap, that the
# one-way platform works from both sides, that a full-draw arrow cannot tunnel through a
# wall thinner than one tick of its flight, and - the important one - that the aim arc
# drawn on screen IS the arrow's flight, tick for tick.
#
# It is possible at all because Stage.h names no engine type, which is the whole reason for
# that split; it takes about a second and is the cheapest way to find out whether a change
# to the feel constants broke one of them. It is NOT part of the normal build and nothing
# depends on it.
#=======================================================================================

ROOT    := ../..
PROJECT := archer

APP_SRCS += main.cpp
APP_SRCS += ApplicationArcher.cpp
APP_SRCS += Stage.cpp
APP_SRCS += Puppet.cpp
#The blockout, as terrain. The mesher itself is core/MarchingCubes.cpp and is picked up by the
#core wildcard; this is the half that knows what a StageBlock is. See terrain_plan.md.
APP_SRCS += Terrain.cpp

#NO SOUND YET. Nothing here calls SoundSystem - the jump, the draw, the loose and the thud
#are all places a sound obviously goes, and every one of them already has an event in
#StageEvents waiting for it (see HandleEvents). Flip this to 1 and fill them in; see the
#USE_SOUND block in engine.mk for why it is opt-in per app.
USE_SOUND := 0

#PHYSICS, and genuinely: the crates, the targets and - later - the brick debris and the rope
#are reactphysics3d's. The ARCHER is not, and neither is an arrow; both are integrated by
#hand in Stage.cpp, for the reasons set out at the top of Stage.h. USE_PHYSICS defaults to 1
#so this is not set here, but it is worth knowing that dropping it would cost this app its
#props and its whole kick-and-break slice, not just a few bodies.

#---------------------------------------------------------------------------------------
# What `make ship` would bake into the exe.
#
# THE SAME TWO ROOTS main.cpp declares, IN THE SAME ORDER - this app's own first, then the
# shared tree for default.vert, deferred.frag and the overlay's font. The order matters for
# the same reason it matters at run time, and nothing checks that these two lists agree, so
# they are worth reading together.
#
# apps/archer/assets now holds meshes/archer.glb - the character, her rig and every clip.
# The level itself is still built from core/Primitives.h, so that one file is the whole of
# what resolves out of this root today; the bow, the arrow and the props follow.
#
# Only read when BAKE_ASSETS is 1, so this costs an ordinary build nothing.
#---------------------------------------------------------------------------------------
ASSET_ROOTS := assets $(ROOT)/shared_assets

#Nothing in this app selects a font, builds text meshes or plays a sound, so the shared
#tree's copies of those would be baked for nobody. EVERY ONE OF THESE WAS CHECKED RATHER
#THAN ASSUMED - an exclusion that is wrong does not fail the build, it produces an exe that
#dies on its first LoadFile on a machine with no loose copy to fall back on.
ASSET_PACK_FLAGS += --exclude "fonts/CascadiaMono.ttf"
ASSET_PACK_FLAGS += --exclude "fonts/mono_sdf.png"
ASSET_PACK_FLAGS += --exclude "meshes/glyphs_unispace.glb"
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
	$(CXX) -std=c++17 -Wall -g -o build/stage_test.exe stage_test.cpp Stage.cpp Puppet.cpp
	@./build/stage_test.exe
