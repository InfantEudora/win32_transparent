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
#The floors' edges, spans and wall feet (vine_plan.md section 15) - Stage's own methods, a file of their own.
APP_SRCS += StageEdges.cpp
APP_SRCS += Puppet.cpp
#The blockout, as terrain. The mesher itself is core/MarchingCubes.cpp and is picked up by the
#core wildcard; this is the half that knows what a StageBlock is. See terrain_plan.md.
APP_SRCS += Terrain.cpp
#Its field on its own, engine-free - what the mesher samples, and what a vine grows against.
APP_SRCS += TerrainField.cpp
#Equipping the bow and the arrow to the hand bones, and bending the bow. See bow_plan.md.
APP_SRCS += Bow.cpp
#Where the ferns and flowers grow on the blockout. Engine-free, so `rules` tests it too.
APP_SRCS += Foliage.cpp
#The bank behind the terrain - see Backdrop.h.
APP_SRCS += Backdrop.cpp
#Where the rocks lie - see Boulders.h.
APP_SRCS += Boulders.cpp
#The vines: a trunk bent along a core/Spline, leaves along it. Engine-free apart from core's maths
#and the two spline files, which `rules` compiles in beside it. See vine_plan.md.
APP_SRCS += Vine.cpp
#The rope as one skinned mesh over its physics links. Engine-free, so `rules` tests it too.
APP_SRCS += RopeMesh.cpp
#Text on props - a signpost's boards - placed by empties authored on the model. See Sign.h.
APP_SRCS += Sign.cpp
#The wind over the level, derived from the blockout - engine-free, so `rules` tests it too - and
#its debug view. See wind_plan.md.
APP_SRCS += Wind.cpp
APP_SRCS += WindView.cpp
#Leaves blown through it - engine-free too, with its own test.
APP_SRCS += Leaves.cpp
#And the streaks that show it moving.
APP_SRCS += Streaks.cpp
#Fireflies living in the plants, and their group of lights.
APP_SRCS += Fireflies.cpp
#The waterfall: where its pieces go and its foam, engine-free (water_test), and its objects.
APP_SRCS += Water.cpp
APP_SRCS += ApplicationArcherWater.cpp

#SOUND, for the bow: the string's creak from the nock, the loose, and the arrow striking
#something - assets/sound/*.wav, played from ApplicationArcher::UpdateSound. The jump, the
#landing and the kick have events waiting for sounds of their own. See the USE_SOUND block in
#engine.mk for why it is opt-in per app; the app compiles and runs silent with it at 0.
USE_SOUND := 1

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

#Nothing in this app selects a font or plays a sound, so the shared tree's copies of those
#would be baked for nobody. EVERY ONE OF THESE WAS CHECKED RATHER THAN ASSUMED - an exclusion
#that is wrong does not fail the build, it produces an exe that dies on its first LoadFile on
#a machine with no loose copy to fall back on.
#
#meshes/glyphs_unispace.glb is NOT excluded any more: since 2026-09-23 the target hit counter
#builds its numbers from it (ApplicationArcher::BuildHitPopups). It used to be on this list.
ASSET_PACK_FLAGS += --exclude "fonts/CascadiaMono.ttf"
ASSET_PACK_FLAGS += --exclude "fonts/mono_sdf.png"
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
	$(CXX) -std=c++17 -Wall -g -I$(ROOT)/core -I$(ROOT)/core/types -o build/stage_test.exe \
		stage_test.cpp Stage.cpp StageEdges.cpp TerrainField.cpp Puppet.cpp Foliage.cpp Backdrop.cpp Water.cpp Boulders.cpp Vine.cpp RopeMesh.cpp RouteCheck.cpp \
		$(ROOT)/core/Spline.cpp $(ROOT)/core/SplineDeform.cpp $(ROOT)/core/types/type_helpers.cpp
	@./build/stage_test.exe
#The wind's checks, as their own exe so they need nothing from stage_test.cpp. -O2 because the
#main level's flow solve takes seconds unoptimised, and its two timings are printed.
	$(CXX) -std=c++17 -Wall -O2 -I$(ROOT)/core -I$(ROOT)/core/types -o build/wind_test.exe \
		wind_test.cpp Wind.cpp Stage.cpp StageEdges.cpp
	@./build/wind_test.exe
#The leaves, the same way.
	$(CXX) -std=c++17 -Wall -O2 -I$(ROOT)/core -I$(ROOT)/core/types -o build/leaves_test.exe \
		leaves_test.cpp Leaves.cpp Wind.cpp Stage.cpp StageEdges.cpp
	@./build/leaves_test.exe
#And the streaks.
	$(CXX) -std=c++17 -Wall -O2 -I$(ROOT)/core -I$(ROOT)/core/types -o build/streaks_test.exe \
		streaks_test.cpp Streaks.cpp Wind.cpp Stage.cpp StageEdges.cpp
	@./build/streaks_test.exe
#And the fireflies.
	$(CXX) -std=c++17 -Wall -O2 -I$(ROOT)/core -I$(ROOT)/core/types -o build/fireflies_test.exe \
		fireflies_test.cpp Fireflies.cpp Wind.cpp Stage.cpp StageEdges.cpp
	@./build/fireflies_test.exe
#And the waterfall - its layout against the real bay, the notch it cuts in the bank, the foam.
	$(CXX) -std=c++17 -Wall -O2 -I$(ROOT)/core -I$(ROOT)/core/types -o build/water_test.exe \
		water_test.cpp Water.cpp Backdrop.cpp Stage.cpp StageEdges.cpp
	@./build/water_test.exe
