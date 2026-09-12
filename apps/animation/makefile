#=======================================================================================
# Animation - a rigged character used to work on the skeleton, clip blending and
# swing-twist root motion. See "ApplicationAnimation Todo.md" here and
# docs/animation_root_motion.md.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/animation.exe 2>stderr.log &
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := animation

APP_SRCS += main.cpp
APP_SRCS += ApplicationAnimation.cpp

include $(ROOT)/engine.mk
