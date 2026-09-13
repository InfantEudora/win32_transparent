#=======================================================================================
# testfx - a fullscreen shader bench. One screen-filling quad, one fragment shader, a
# panel to pick which and to drive its uniforms, and MCP tools so the whole loop runs
# without a keyboard. See docs/testfx_plan.md.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/testfx.exe 2>stderr.log &
#
# Adding an effect is dropping a .frag in assets/shaders/ - no rebuild, and no entry in
# this file. That is the point of the app.
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := testfx

APP_SRCS += main.cpp
APP_SRCS += ApplicationTestFX.cpp

#No SoundSystem here, so OpenAL is not linked - see the USE_SOUND block in engine.mk.

include $(ROOT)/engine.mk
