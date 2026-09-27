#=======================================================================================
# samplescan - catalogues a folder of audio samples: level, envelope, pitch, key, and a
# first guess at the role each one plays. See samplescan.cpp.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"     # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                          # mingw32-make, not /usr/bin/make
#     ./build/samplescan.exe                        # scans ../../apps/music/samples
#     ./build/samplescan.exe --file some.wav        # one file, every field
#
# Links NOTHING from core/ and not libthirdparty.a either: it compiles its own miniaudio,
# with the decoders the engine's build leaves out. samplescan_ma.h says why that is safe.
#=======================================================================================

ROOT    := ../..
PROJECT := samplescan

TOOL_SRCS += samplescan.cpp
TOOL_SRCS += analyse.cpp
TOOL_SRCS += export.cpp
TOOL_SRCS += samplescan_ma.cpp

include $(ROOT)/tools/tools.mk
