#=======================================================================================
# assetpack - walks an app's asset roots and bakes what it finds into the table the
# engine reads at BinaryAsset::assets[]. See tools/assetpack_plan.md, and assetpack.cpp.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"     # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                          # mingw32-make, not /usr/bin/make
#     ./build/assetpack.exe --list ../../shared_assets
#     ./build/assetpack.exe --help
#
# It links NO core sources, which is a deliberate departure from the Android port's
# tools/pack_assets.cpp. That one compiles in File.cpp, BinaryAsset.cpp and both Debug
# translation units so it can drive LoadFile and then call the engine's own
# DumpBinaryAssets(). That only made sense while the dump lived in the engine, and it does
# not any more - it was deleted on 2026-09-14 and BinaryAsset.cpp is a reader now. A tool
# that owns its own writer needs a directory walk, a deflate and an fprintf.
#
# It DOES include core/BinaryAsset.h, for the contract: the generated table names that
# struct's fields with designated initialisers, so a rename breaks the build loudly rather
# than the pack meaning something else. -I../../core is already in tools.mk's IPATHS.
#=======================================================================================

ROOT    := ../..
PROJECT := assetpack

TOOL_SRCS += assetpack.cpp

#miniz, for the deflate the writer needs (step 3). Named as sources rather than linked from
#libs/libthirdparty.a for the reason tools.mk gives at length: a tool's own translation units
#and the objects it links have to agree on flags, and the library is built with the engine's.
#The inflate half comes with it; the tool does not decompress, but the two files are one unit
#and separating them would save a compile nobody notices.
CORE_SRCS += $(ROOT)/3rdparty/miniz/miniz_tdef.cpp
CORE_SRCS += $(ROOT)/3rdparty/miniz/miniz_tinfl.cpp

include $(ROOT)/tools/tools.mk
