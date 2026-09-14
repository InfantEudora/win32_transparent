#=======================================================================================
# fontbake - bakes a monospace TrueType font into the signed-distance atlas the 2D
# overlay draws text from. See docs/ui_overlay_plan.md section 2, and fontbake.cpp.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"     # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                          # mingw32-make, not /usr/bin/make
#     ./build/fontbake.exe                          # bakes the default font with default settings
#     ./build/fontbake.exe --help
#
# This links NOTHING from core/ - it only includes core/UIFont.h, which is header-only
# and is the single definition of the format the tool writes and the engine reads. The
# TrueType parser is 3rdparty/stb_truetype/, which exists so that no app has to contain
# one; see that folder's README.
#=======================================================================================

ROOT    := ../..
PROJECT := fontbake

TOOL_SRCS += fontbake.cpp

include $(ROOT)/tools/tools.mk
