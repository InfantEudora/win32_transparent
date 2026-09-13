#=======================================================================================
# The prebuilt static libraries: libs/libthirdparty.a and libs/libimgui.a.
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
CC_C      := gcc
BUILD_DIR := build
OUT       := ../libs/libthirdparty.a

CFLAGS := -std=c++17 -Os -fno-exceptions -DJSON_NOEXCEPTION -DTINYGLTF_NO_FS -DTINYGLTF_NO_WRITER -D_WIN32
IPATHS := -I. -Itinygltf -Istb_image -Iminiz -Iminiaudio

#---------------------------------------------------------------------------------------
# miniaudio
#
# It is C, not C++, so it gets gcc and its own flags - do not fold it into CFLAGS above,
# which is -std=c++17. 3rdparty/miniaudio is a git submodule (see .gitmodules); after a
# fresh clone it is empty until `git submodule update --init`.
#
# NOTE THE ABSENCE OF ANY -DMA_* HERE, WHICH IS DELIBERATE. The feature set lives in
# 3rdparty/miniaudio_config.h, which miniaudio_impl.c includes before miniaudio.h, because
# some of those defines change struct layout - a consumer compiled without them would
# disagree with this library about where ma_engine's fields are, silently. That file
# explains it and carries the measurements; there is nothing to keep in step here.
#---------------------------------------------------------------------------------------
MA_CFLAGS := -Os -D_WIN32

#---------------------------------------------------------------------------------------
# imgui
#
# Same story as libthirdparty.a: libs/libimgui.a had no build rule anywhere in the repo and
# was a hand-built blob. Probing it the same way - compiling imgui.cpp at each level and
# comparing against the shipped object - matched -Og to within 0.8% (347,392 against
# 344,492), so that one was a DEBUG build too. -Os measures 293,500.
#
# UNLIKE the libraries above, this one IS in the frame loop: the debug panels are rebuilt
# and re-rendered every frame. -Os is still the right default because the thing it replaces
# is -Og, which is slower as well as bigger, so this is not a size-for-speed trade. But if
# ImGui ever shows up in a profile, -O2 is the knob, and that is a real decision here in a
# way it is not for image decode or glTF parse.
#
# imgui_demo.cpp is built even though it is 251 KB of sample code, because
# apps/isoanimation calls ImGui::ShowDemoWindow() for real. It costs nothing in the apps
# that do not: an archive member is only pulled in when something references it.
#
# NO -DIMGUI_* ANYWHERE, and that is checked rather than assumed. 3rdparty/imgui/imconfig.h
# is stock, and engine.mk defines nothing either. It matters because ImGui's structs cross
# this boundary - ImVec2, ImDrawData, ImGuiIO - so a define that changed a layout on one
# side only would corrupt rather than fail to link. If a config is ever added, it has to be
# added to BOTH, the same trap 3rdparty/miniaudio_config.h exists to remove.
#
# AND THE SUBMODULE CARRIES A LOCAL PATCH. backends/imgui_impl_win32.cpp has
# ImGui_ImplWin32_EnableAlphaCompositing() short-circuited with an early return: it calls
# DwmEnableBlurBehindWindow, which fights the layered-window alpha this whole project is
# built around. That patch is UNCOMMITTED inside the submodule, so a `git submodule update`
# would revert it silently and the symptom would be a visual regression, not a build error.
# Check `git -C imgui diff` is non-empty before rebuilding this library.
#---------------------------------------------------------------------------------------
IMGUI_OUT    := ../libs/libimgui.a
IMGUI_CFLAGS := -std=c++17 -Os -fno-exceptions -D_WIN32
IMGUI_IPATHS := -Iimgui -Iimgui/backends

#Object names are flat inside the archive, matching what the hand-built library held.
SRCS := stb_image/stb_image.cpp \
        stb_image/stb_image_write.cpp \
        tinygltf/tiny_gltf.cpp \
        miniz/miniz_tdef.cpp \
        miniz/miniz_tinfl.cpp

C_SRCS := miniaudio_impl.c

IMGUI_SRCS := imgui/imgui.cpp \
              imgui/imgui_draw.cpp \
              imgui/imgui_widgets.cpp \
              imgui/imgui_tables.cpp \
              imgui/imgui_demo.cpp \
              imgui/backends/imgui_impl_win32.cpp \
              imgui/backends/imgui_impl_opengl3.cpp

IMGUI_OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(IMGUI_SRCS:.cpp=.o)))

OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(SRCS:.cpp=.o)))
OBJS += $(addprefix $(BUILD_DIR)/,$(notdir $(C_SRCS:.c=.o)))

#Header dependencies, the same -MMD -MP arrangement the app builds use: editing
#tiny_gltf.h or json.hpp has to rebuild tiny_gltf.o.
DEPFLAGS := -MMD -MP
DEPS     := $(OBJS:.o=.d) $(IMGUI_OBJS:.o=.d)

VPATH := stb_image:tinygltf:miniz:miniaudio:imgui:imgui/backends

.DEFAULT_GOAL := default
default: $(OUT) $(IMGUI_OUT)

$(OUT): $(OBJS)
	@echo [AR] $@
	ar rcs $@ $^

$(IMGUI_OUT): $(IMGUI_OBJS)
	@echo [AR] $@
	ar rcs $@ $^

#imgui's objects are built by the same pattern rule below, with its flags swapped out for
#theirs. Target-specific variables rather than a second rule, so there is one recipe to keep
#right.
$(IMGUI_OBJS): CFLAGS := $(IMGUI_CFLAGS)
$(IMGUI_OBJS): IPATHS := $(IMGUI_IPATHS)
$(IMGUI_OBJS): | imgui/imgui.h

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	@echo [CC] $<
	$(CC) -c $(DEPFLAGS) $(CFLAGS) $(IPATHS) $< -o $@

#miniaudio only. Its own compiler and its own flags - see the block above.
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR) miniaudio/miniaudio.h
	@echo [CC] $<
	$(CC_C) -c $(DEPFLAGS) $(MA_CFLAGS) $(IPATHS) $< -o $@

#A fresh clone has an empty 3rdparty/miniaudio until the submodule is checked out, and the
#error gcc gives for that is a missing include, which reads like a broken build rather than
#a missing checkout. Say what it actually is.
miniaudio/miniaudio.h:
	$(error 3rdparty/miniaudio is empty - run: git submodule update --init 3rdparty/miniaudio)

imgui/imgui.h:
	$(error 3rdparty/imgui is empty - run: git submodule update --init 3rdparty/imgui)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(OUT) $(IMGUI_OUT)

-include $(DEPS)

.PHONY: default clean
