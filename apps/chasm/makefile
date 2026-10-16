#=======================================================================================
# chasm - a top-down colony sim on a Townscaper-style irregular grid. See docs/README.md.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/chasm_nophysics.exe --minimized --mcp-port 8769 2>stderr.log &
#
# _nophysics because of USE_PHYSICS := 0 below - a non-default flag names its own exe (engine.mk).
#
# NO SEPARATE RULES BUILD, unlike bomber and archer. The checks are built into the app and run
# as a command in it (the panel's Run checks, or the chasm_check MCP tool), in DEBUG builds only,
# so a failure can be looked at where it is - docs/README.md, "Checks live in the app".
#=======================================================================================

ROOT    := ../..
PROJECT := chasm

APP_SRCS += main.cpp
APP_SRCS += ApplicationChasm.cpp
APP_SRCS += ApplicationChasmSelect.cpp
APP_SRCS += ChasmHud.cpp
APP_SRCS += ApplicationChasmIssues.cpp
APP_SRCS += ApplicationChasmBridge.cpp
APP_SRCS += ApplicationChasmWater.cpp
APP_SRCS += ApplicationChasmSeasons.cpp
APP_SRCS += ApplicationChasmHearth.cpp
APP_SRCS += ApplicationChasmTitle.cpp
APP_SRCS += ApplicationChasmMenu.cpp
APP_SRCS += ApplicationChasmRecord.cpp
APP_SRCS += ApplicationChasmGoals.cpp
APP_SRCS += ChasmVersion.cpp
APP_SRCS += ApplicationChasmStats.cpp
APP_SRCS += ChasmStats.cpp
APP_SRCS += Grid.cpp
APP_SRCS += ChasmLayout.cpp
APP_SRCS += GridPick.cpp
APP_SRCS += Terrain.cpp
APP_SRCS += TerrainMesh.cpp
APP_SRCS += Zones.cpp
APP_SRCS += ZoneMesh.cpp
APP_SRCS += ChasmSave.cpp
APP_SRCS += Forest.cpp
APP_SRCS += Deposits.cpp
APP_SRCS += CropMesh.cpp
APP_SRCS += BuildingMesh.cpp
APP_SRCS += CastleMesh.cpp
APP_SRCS += BoundaryMesh.cpp
APP_SRCS += Mist.cpp
APP_SRCS += WaterMesh.cpp
APP_SRCS += ApplicationChasmRoads.cpp
APP_SRCS += RoadMesh.cpp
APP_SRCS += Walkers.cpp
APP_SRCS += Calendar.cpp
APP_SRCS += ApplicationChasmTime.cpp
APP_SRCS += ApplicationChasmSnow.cpp
APP_SRCS += Economy.cpp
APP_SRCS += ApplicationChasmEconomy.cpp
APP_SRCS += ApplicationChasmWorkersView.cpp
APP_SRCS += ApplicationChasmCrowdBench.cpp
APP_SRCS += ApplicationChasmPiles.cpp
APP_SRCS += ApplicationChasmCrates.cpp
APP_SRCS += ApplicationChasmMines.cpp
APP_SRCS += Zombies.cpp
APP_SRCS += Spacing.cpp
APP_SRCS += Shooting.cpp
APP_SRCS += ApplicationChasmZombies.cpp
APP_SRCS += ApplicationChasmRange.cpp
APP_SRCS += ApplicationChasmRopewalk.cpp
APP_SRCS += ApplicationChasmBorders.cpp
APP_SRCS += Exploration.cpp
APP_SRCS += Areas.cpp
APP_SRCS += ApplicationChasmExplore.cpp
APP_SRCS += ApplicationChasmGhost.cpp
APP_SRCS += ApplicationChasmSound.cpp
APP_SRCS += ApplicationChasmMusic.cpp

#NO PHYSICS: the grid, the terrain and the colony are this app's own rules, and nothing here
#creates a PhysicsWorld. See the USE_PHYSICS block in engine.mk.
USE_PHYSICS := 0

#SOUND (docs/sound_plan.md): assets/cues/chasm.json says what is heard; ApplicationChasmSound.cpp reports what
#happens. The app compiles and runs silent with it at 0, the cues still deciding - see the USE_SOUND block in engine.mk.
USE_SOUND := 1

#The same two roots main.cpp declares, in the same order: this app's own (the palette) first.
ASSET_ROOTS := assets $(ROOT)/shared_assets

ASSET_PACK_FLAGS += --exclude "fonts/CascadiaMono.ttf"
ASSET_PACK_FLAGS += --exclude "fonts/mono_sdf.png"
#The shared sounds this app does not play. Not sound/click.wav: an exclude matches the NAME, and chasm's own
#click (assets/sound/click.wav, which stands in front of the shared one) would go with it.
ASSET_PACK_FLAGS += --exclude "sound/bleep.wav"
ASSET_PACK_FLAGS += --exclude "sound/floop.wav"
ASSET_PACK_FLAGS += --exclude "sound/hax.wav"
ASSET_PACK_FLAGS += --exclude "meshes/glyphs_unispace.glb"


include $(ROOT)/engine.mk

#THE VERSION (ChasmVersion.h): the commit, "+" with changes not yet committed, and the day - passed to
#ChasmVersion.cpp alone. The stamp file is written only when they differ from the last build's, and the object
#depends on it, so the file is compiled again on a new commit or a new day and otherwise not: a build with
#nothing changed still has nothing to do.
CHASM_GIT := $(shell git describe --always --dirty=+ --abbrev=7 2>/dev/null || echo unknown)
CHASM_DAY := $(shell date +%Y-%m-%d)
CHASM_STAMP := $(OBJ_DIR)/version.stamp
$(shell mkdir -p $(OBJ_DIR); [ "$$(cat $(CHASM_STAMP) 2>/dev/null)" = "$(CHASM_GIT) $(CHASM_DAY)" ] || echo "$(CHASM_GIT) $(CHASM_DAY)" > $(CHASM_STAMP))
$(OBJ_DIR)/ChasmVersion.o: $(CHASM_STAMP)
$(OBJ_DIR)/ChasmVersion.o: CFLAGS += -DCHASM_GIT=\"$(CHASM_GIT)\" -DCHASM_BUILD_DATE=\"$(CHASM_DAY)\"
