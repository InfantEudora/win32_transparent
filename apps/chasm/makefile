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
APP_SRCS += TitleTown.cpp
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
APP_SRCS += Weather.cpp
APP_SRCS += ApplicationChasmWeather.cpp
APP_SRCS += Economy.cpp
APP_SRCS += ApplicationChasmEconomy.cpp
APP_SRCS += ApplicationChasmWorkersView.cpp
APP_SRCS += ApplicationChasmCrowdBench.cpp
APP_SRCS += Birds.cpp
APP_SRCS += ApplicationChasmBirds.cpp
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

#The exe's icon and Details tab, on Windows - chasm.rc, and THE VERSION below for what it says.
APP_RC := chasm.rc

#The same two roots main.cpp declares, in the same order: this app's own (the palette) first.
ASSET_ROOTS := assets $(ROOT)/shared_assets

#The icon's picture, which nothing loads: it reaches the exe as chasm.ico, through chasm.rc.
ASSET_PACK_FLAGS += --exclude "icon.png"

ASSET_PACK_FLAGS += --exclude "fonts/CascadiaMono.ttf"
ASSET_PACK_FLAGS += --exclude "fonts/mono_sdf.png"
#The shared sounds this app does not play - click.wav among them since chasm's own click became click.ogg
#(tools/sound_ogg.py): the shared wav no longer stands behind one of its own name, and the cue table plays the ogg.
ASSET_PACK_FLAGS += --exclude "sound/bleep.wav"
ASSET_PACK_FLAGS += --exclude "sound/click.wav"
ASSET_PACK_FLAGS += --exclude "sound/floop.wav"
ASSET_PACK_FLAGS += --exclude "sound/hax.wav"
ASSET_PACK_FLAGS += --exclude "meshes/glyphs_unispace.glb"
#Shared shaders nothing here loads: no skybox is drawn (a flat haze, BuildScene); the field shadows come only from
#Renderer::EnableFieldShadows, which chasm never calls; texture.comp and noise3d.comp no code in core or here names.
#Take a line out before using any of them, or a ship build fails to load it.
ASSET_PACK_FLAGS += --exclude "shaders/skybox.frag"
ASSET_PACK_FLAGS += --exclude "shaders/skybox.vert"
ASSET_PACK_FLAGS += --exclude "shaders/field.frag"
ASSET_PACK_FLAGS += --exclude "shaders/field.vert"
ASSET_PACK_FLAGS += --exclude "shaders/field_jfa.comp"
ASSET_PACK_FLAGS += --exclude "shaders/texture.comp"
ASSET_PACK_FLAGS += --exclude "shaders/noise3d.comp"
#IMGUI'S FONT, in a build without ImGui only (apps/tetris/makefile has the long version): core/WindowImGui.cpp is the
#one thing that loads it, and a baked build WITH ImGui dies at startup without it. USE_IMGUI gets its default from
#engine.mk, included below, so mirror that default before testing it; a command-line USE_IMGUI=0 still wins.
USE_IMGUI ?= 1
ifneq ($(USE_IMGUI),1)
ASSET_PACK_FLAGS += --exclude "fonts/consola.ttf"
endif


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

#AND THE SAME VERSION IN THE EXE'S OWN PROPERTIES (chasm.rc), so a crash report or a player's "which build is
#this" can be answered from the file. The values go in a header written beside the objects, which the script
#includes like any other, rather than as -D's handed on through windres to its preprocessor. The number is read
#out of ChasmVersion.cpp so it is still set in one place - and since Windows also wants it as four numbers, it may
#hold only digits and dots.
ifeq ($(PLATFORM),windows)
CHASM_VERSION := $(shell sed -n 's/^\#define CHASM_VERSION "\(.*\)"$$/\1/p' ChasmVersion.cpp)
ifeq ($(CHASM_VERSION),)
$(error could not read CHASM_VERSION out of ChasmVersion.cpp, which chasm.rc needs)
endif
CHASM_EMPTY :=
CHASM_SPACE := $(CHASM_EMPTY) $(CHASM_EMPTY)
CHASM_COMMA := ,
#"0.0.1" -> 0,0,1,0
CHASM_VERSION_NUM := $(subst $(CHASM_SPACE),$(CHASM_COMMA),$(wordlist 1,4,$(subst ., ,$(CHASM_VERSION)) 0 0 0 0))

CHASM_RC_H := $(OBJ_DIR)/chasm_rc.h
$(CHASM_RC_H): $(CHASM_STAMP) ChasmVersion.cpp makefile
	@mkdir -p $(dir $@)
	@echo '#define CHASM_RC_VERSION "$(CHASM_VERSION)"' > $@
	@echo '#define CHASM_RC_VERSION_NUM $(CHASM_VERSION_NUM)' >> $@
	@echo '#define CHASM_RC_PRODUCT_VERSION "$(CHASM_VERSION) ($(CHASM_GIT), $(CHASM_DAY))"' >> $@
	@echo '#define CHASM_RC_EXE "$(notdir $(EXE))"' >> $@
	@echo '#define CHASM_RC_FILEFLAGS $(if $(CONFIG_SUFFIX),0,VS_FF_DEBUG)' >> $@

#What chasm.rc pulls in, which make cannot see for itself (WINDOWS RESOURCES in engine.mk).
$(OBJ_DIR)/chasm.res.o: chasm.ico $(CHASM_RC_H)
RC_FLAGS += -I$(OBJ_DIR)
endif
