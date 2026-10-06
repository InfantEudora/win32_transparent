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
APP_SRCS += ApplicationChasmWater.cpp
APP_SRCS += Grid.cpp
APP_SRCS += ChasmLayout.cpp
APP_SRCS += GridPick.cpp
APP_SRCS += Terrain.cpp
APP_SRCS += TerrainMesh.cpp
APP_SRCS += Zones.cpp
APP_SRCS += ZoneMesh.cpp
APP_SRCS += ChasmSave.cpp
APP_SRCS += Forest.cpp
APP_SRCS += CropMesh.cpp
APP_SRCS += BuildingMesh.cpp
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
APP_SRCS += ApplicationChasmPiles.cpp
APP_SRCS += Exploration.cpp
APP_SRCS += ApplicationChasmExplore.cpp

#NO PHYSICS: the grid, the terrain and the colony are this app's own rules, and nothing here
#creates a PhysicsWorld. See the USE_PHYSICS block in engine.mk.
USE_PHYSICS := 0

#The same two roots main.cpp declares, in the same order: this app's own (the palette) first.
ASSET_ROOTS := assets $(ROOT)/shared_assets

include $(ROOT)/engine.mk
