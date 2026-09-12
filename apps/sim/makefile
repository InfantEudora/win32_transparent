#=======================================================================================
# Sim - a space/galaxy simulation: stellar bodies, resources and an ImGui-driven UI.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/sim.exe 2>stderr.log &
#
# galaxy/ used to be a folder at the repo root; nothing else built it, so it moved in here
# (PerlinNoise/WorleyNoise/StellarBody). See docs/asset_layout_plan.md section 2.1.
#
# NOTE: icon_source/ next to this file is NOT build input - the layered .psd sources for the
# four resource icons in assets/icons. It wants an art repo, not this one.
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := sim

APP_SRCS += main.cpp
APP_SRCS += ApplicationSim.cpp
APP_SRCS += StellarBody.cpp
APP_SRCS += PerlinNoise.cpp
APP_SRCS += WorleyNoise.cpp

#A small immediate-mode helper this app uses for its own widgets.
APP_SRCS += imgooey.cpp

#Uses SoundSystem -> needs OpenAL linked in.
USE_SOUND := 1

include $(ROOT)/engine.mk
