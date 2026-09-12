#=======================================================================================
# Dozer - a bulldozer with an articulated arm, buttons and a lot of engine audio.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/dozer.exe 2>stderr.log &
#
# NOTE: audio_source/ next to this file is NOT build input and NOT loaded - it is the raw
# multitrack the nine clips in assets/sound were cut from, plus the .pk/.asd files the
# editor left behind. 42 MB of it. It wants an audio project repo, not this one - see
# docs/asset_layout_plan.md section 6.1.
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := dozer

APP_SRCS += main.cpp
APP_SRCS += ApplicationDozer.cpp
APP_SRCS += DozerCharacter.cpp
APP_SRCS += DozerButton.cpp

#The engine, the hydraulics, the door and the steel beam all make noise - see core/SoundSystem.h.
USE_SOUND := 1

include $(ROOT)/engine.mk
