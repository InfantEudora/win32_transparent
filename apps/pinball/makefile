#=======================================================================================
# Orbit Outpost - a pinball machine. See pinball_design.md for the table and the build
# plan; this is stage 0, the static layout.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/pinball.exe 2>stderr.log &
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those -
# and because they are shared, build ONE app at a time.
#=======================================================================================

ROOT    := ../..
PROJECT := pinball

APP_SRCS += main.cpp
APP_SRCS += ApplicationPinball.cpp
APP_SRCS += Table.cpp
APP_SRCS += TableBuilder.cpp
APP_SRCS += Mechanisms.cpp

# Not yet. A pinball machine is mostly short overlapping one-shots and wants about
# fourteen of them (pinball_design.md 3.4), which is well inside SoundSystem's 32 buffers
# and 16 voices - but nothing on the table makes a noise until stage 1 has something that
# moves, and linking OpenAL to play silence only slows the build down. Turn this on with
# the flippers.
#USE_SOUND := 1

include $(ROOT)/engine.mk
