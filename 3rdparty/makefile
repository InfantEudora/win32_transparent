#=======================================================================================
# libs/libthirdparty.a - stb_image, miniz and tinygltf.
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"
#     cd 3rdparty && mingw32-make.exe -j4
#
# This library used to have no build rule at all. It was compiled by hand once and lived
# only as libs/libthirdparty.a, which .gitignore excludes (*.a) - so a fresh clone could
# not produce it and a change to tiny_gltf.cpp could not be landed without knowing, or
# guessing, the original flags. That is what this file fixes. It is NOT wired into
# engine.mk: the library changes about once a year and rebuilding it on every app build
# would cost more than it saves. Run it by hand after editing anything under 3rdparty/.
#
# THE FLAGS BELOW ARE NOT FREE CHOICES. Everything here is also compiled into the apps
# through headers, so the two compiles have to agree:
#
#   -std=c++17          ABI. libstdc++ types cross this boundary (std::string in
#                       tinygltf::Model, std::function in its callbacks).
#   -fno-exceptions     ABI. engine.mk sets it; the objects in the shipped library were
#                       built with it too (no __cxa_throw references in them).
#   -DJSON_NOEXCEPTION  json.hpp aborts instead of throwing - required by -fno-exceptions.
#   -DTINYGLTF_NO_FS    switches the in-class initialiser of TinyGLTF::fs. See the note
#                       beside the matching flag in engine.mk; a mismatch here is an
#                       undefined reference to tinygltf::FileExists at app link time.
#   -DTINYGLTF_NO_WRITER compiles the glTF serializer out. Nothing here calls it, and it was
#                       the last referrer to std::endl and operator<<(ostream&, string) -
#                       so the last thing pulling ~700 KB of libstdc++ stream machinery in.
#   -D_WIN32            what the rest of the build assumes.
#
# No -g. The hand-built objects carried full debug info, which is most of why the archive
# was 8.5 MB; release builds strip it anyway and nobody single-steps through stb_image.
#
# -Os, and that is the whole reason this file paid for itself. The hand-built objects
# turn out to have been compiled with -Og: rebuilding stb_image.cpp at each level and
# comparing against the shipped object matched -Og to within 56 bytes (47,376 against
# 47,432), while -O2 gave 62,240 and -O3 gave 112,768. So every app has been linking a
# DEBUG build of this library. -Os is smaller than -Og and faster than it, which makes
# it a strict improvement rather than the usual size-for-speed trade; measured on
# tiny_gltf.o it is 182,616 bytes against the shipped 281,252, and it took 102 KB off
# tetris_release.exe.
#
# No -ffunction-sections/-fdata-sections either, and this one WAS tried: the hope was that
# splitting tiny_gltf.o would let -Wl,--gc-sections drop the glTF writer, which nothing in
# this engine calls. It changed ocpp_release.exe by zero bytes. The writer is still in the
# binary afterwards - WriteGltfSceneToStream and every SerializeGltf* with it - because on
# PE/COFF each function's .pdata unwind entry references it and keeps it alive. That writer
# is the last thing referencing std::endl and operator<<(ostream&, string), and so the last
# thing pulling libstdc++'s locale machinery (~700 KB) into every app. Dropping it needs the
# serializer compiled out, not garbage-collected out; see docs/engine_backlog.md.
#
# Do not "fix" this to -O2 or -O3 on the assumption that a release library wants them.
# Nothing here is in a frame loop - it is image decode, zip inflate and glTF parse, all
# of which happen while something is loading - so code size is the axis that matters.
#=======================================================================================

CC        := g++
BUILD_DIR := build
OUT       := ../libs/libthirdparty.a

CFLAGS := -std=c++17 -Os -fno-exceptions -DJSON_NOEXCEPTION -DTINYGLTF_NO_FS -DTINYGLTF_NO_WRITER -D_WIN32
IPATHS := -I. -Itinygltf -Istb_image -Iminiz

#Object names are flat inside the archive, matching what the hand-built library held.
SRCS := stb_image/stb_image.cpp \
        stb_image/stb_image_write.cpp \
        tinygltf/tiny_gltf.cpp \
        miniz/miniz_tdef.cpp \
        miniz/miniz_tinfl.cpp

OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(SRCS:.cpp=.o)))

#Header dependencies, the same -MMD -MP arrangement the app builds use: editing
#tiny_gltf.h or json.hpp has to rebuild tiny_gltf.o.
DEPFLAGS := -MMD -MP
DEPS     := $(OBJS:.o=.d)

VPATH := stb_image:tinygltf:miniz

.DEFAULT_GOAL := default
default: $(OUT)

$(OUT): $(OBJS)
	@echo [AR] $@
	ar rcs $@ $^

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	@echo [CC] $<
	$(CC) -c $(DEPFLAGS) $(CFLAGS) $(IPATHS) $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(OUT)

-include $(DEPS)

.PHONY: default clean
