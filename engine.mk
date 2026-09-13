#=======================================================================================
# The engine, as something an app's makefile includes.
#
# Every app under apps/<name>/ has its own makefile, builds its own exe into its own
# build/ folder, and gets here by setting three things and including this file:
#
#     ROOT     := ../..            where the repo root is, seen from the app folder
#     PROJECT  := tank             the exe name
#     APP_SRCS := main.cpp ...     the app's own sources, relative to the app folder
#     include $(ROOT)/engine.mk
#
# and optionally:
#
#     USE_SOUND := 1               link OpenAL (see the block below)
#
# CONFIG picks the build configuration and is passed on the command line, not set by an
# app - it is a property of the build you are doing, not of the app you are doing it to:
#
#     mingw32-make.exe                    ->  build/<project>.exe          (debug)
#     mingw32-make.exe CONFIG=release     ->  build/<project>_release.exe
#
# WHAT MAKES THIS POSSIBLE is that core/ no longer knows which app is building. It used
# to: main.cpp took the app class through -DAPP_HEADER/-DAPP_CLASS, and core/File.cpp
# took the asset roots through -DAPP_ASSET_PATH. Both are gone - each app has its own
# main.cpp naming its class directly, and declares its asset roots by calling
# AddAssetSearchRootFromExe() there. So the core objects are identical whichever app is
# being built, which is what lets them be compiled ONCE into $(ROOT)/build/core and
# shared by all of them.
#
# It also retires the .current_app sentinel the old root makefile needed. That existed
# only to force main.o (and later core/File.o) to rebuild when APP changed while their
# .cpp had not. Nothing app-dependent is compiled into a shared object any more, so
# there is nothing left to go stale.
#
# NOTE ON CONCURRENT BUILDS: $(ROOT)/build/core is shared between apps deliberately, so
# core compiles once rather than twelve times. Two builds running at the same time can
# therefore race on the same object files. Build one app at a time.
#
# The other consequence of sharing them is expected and harmless: when a core source or
# header changes, the NEXT build of every app relinks, because its exe is now older than
# an object it links. That is a link, not a recompile - the core objects themselves are
# rebuilt once, by whichever app builds first.
#=======================================================================================

CC = g++

ifndef ROOT
$(error ROOT is not set - an app makefile must set ROOT to the repo root before including engine.mk)
endif
ifndef PROJECT
$(error PROJECT is not set - an app makefile must set PROJECT to its exe name before including engine.mk)
endif

.DEFAULT_GOAL := default

#---------------------------------------------------------------------------------------
# BUILD CONFIGURATION
#
# Unlike USE_SOUND, this is NOT an app setting. Debug-vs-release changes CORE_CFLAGS, so
# it changes the shared core objects - the one thing the "line between shared and per-app
# flags" below exists to prevent an app from doing.
#
# The way out is not a stamp that wipes objects when the setting changes: with one shared
# core directory that would mean every app rebuilding whenever any app switched. Instead
# each configuration gets its OWN object tree. Nothing can collide, nothing needs wiping,
# and switching back and forth costs one link rather than a rebuild.
#
# The EXE is named per configuration too, and that is load-bearing rather than tidy. If
# both configurations wrote build/<project>.exe, building release and then switching back
# to debug would leave make comparing the release exe against debug objects that are all
# older than it - "nothing to be done", and you keep running the release binary. Distinct
# names make that unrepresentable, and let both exist at once.
#
# What does NOT move is the directory the exe sits in. Every app's main.cpp counts levels
# from there - AddAssetSearchRootFromExe("../../../shared_assets") - and imgui.ini and
# per-app save files are written next to it. A build/<config>/ exe would silently lose
# every asset in all fourteen apps.
#---------------------------------------------------------------------------------------
CONFIG ?= debug

ifeq ($(CONFIG),debug)
CONFIG_SUFFIX :=
else ifeq ($(CONFIG),release)
CONFIG_SUFFIX := _release
else
$(error CONFIG must be 'debug' or 'release', not '$(CONFIG)')
endif

#Where objects go. The app's own objects are private to the app; the core, imgui and
#third-party objects are shared, because they no longer vary by app. Both are split by
#configuration; the exe is not, it is only named by one.
BUILD_DIR       := build
OBJ_DIR         := $(BUILD_DIR)/obj/$(CONFIG)
CORE_BUILD_ROOT := $(ROOT)/build/core
CORE_BUILD_DIR  := $(CORE_BUILD_ROOT)/$(CONFIG)

#No Console on windows, just the window
FNOCONSOLE = -Wl,-subsystem,windows

CFLAGS = -std=c++17 -L$(ROOT)/libs/ -lreactphysics3d -limgui -lsetupapi -lhid -lthirdparty -luser32 -lopengl32 -lgdi32 -lws2_32 -lcrypt32 -Wl,-Bstatic -static-libstdc++ -static-libgcc -static -lstdc++ -Wl,--gc-sections -D_WIN32
CFLAGS += -lXinput9_1_0
#CFLAGS += $(FNOCONSOLE)
CFLAGS += -fno-exceptions -DJSON_NOEXCEPTION
#tinygltf's own file I/O is dead weight here: core/GLTFLoader.cpp hands it bytes that
#core/File.cpp has already resolved and read, so LoadBinaryFromMemory is the only entry
#point the engine uses. Turning the built-in filesystem off drops tinygltf's <fstream>.
#
#This has to be seen by BOTH tiny_gltf.cpp and everything that constructs a TinyGLTF -
#the macro switches the in-class initialiser of TinyGLTF::fs between the default callbacks
#and nullptrs, so a translation unit compiled without it emits references to
#tinygltf::FileExists and friends that the library no longer defines. Hence up here with
#the other app-invariant flags, and repeated in 3rdparty/makefile.
CFLAGS += -DTINYGLTF_NO_FS

#The glTF serializer, which nothing in this engine calls and which was the last thing
#pulling libstdc++'s locale and streambuf machinery - about 700 KB - into every app. Like
#NO_FS above this has to be seen by both sides, though for a weaker reason: the class
#layout is deliberately the same either way, so a translation unit that missed it merely
#fails to link if it calls the writer rather than disagreeing about the object silently.
#Kept here anyway so the two compiles say the same thing. See 3rdparty/tinygltf/tiny_gltf.cpp.
CFLAGS += -DTINYGLTF_NO_WRITER
#Sound is optional - see the USE_SOUND block below.
#NOTHING links winmm now, not even a sound build: that went with OpenAL, and miniaudio's
#WASAPI backend wants only -lole32. core/PrecisionSleeper resolves timeBeginPeriod from
#winmm.dll at run time anyway, and only on pre-Win10-1803 machines where the
#high-resolution waitable timer is unavailable, so nothing needs it at link time.

IPATHS += -I$(ROOT)/core/
IPATHS += -I$(ROOT)/core/physics
IPATHS += -I$(ROOT)/core/skeleton
IPATHS += -I$(ROOT)/3rdparty/imgui/
IPATHS += -I$(ROOT)/3rdparty/
IPATHS += -I$(ROOT)/3rdparty/stb_image/
IPATHS += -I$(ROOT)/3rdparty/openal-soft/
IPATHS += -I$(ROOT)/3rdparty/miniz/
IPATHS += -I$(ROOT)/3rdparty/reactphysics3d/
#The app's own folder, so its headers find each other by plain name.
IPATHS += -I.

#AL_LIBTYPE_STATIC has to be seen by anything that includes the OpenAL headers, which is
#core/SoundSystem.cpp and core/WaveFile.cpp and nothing else. It is set UNCONDITIONALLY and
#not inside the USE_SOUND block below, because those two are core sources and core has to
#compile the same way for every app - see CORE_CFLAGS. It is inert everywhere else.
CFLAGS += -DAL_LIBTYPE_STATIC

DFLAGS = -DDEBUG -Og -g #-g Produce debug info for GDB. -O0 fastest compilation time.
RFLAGS = -DRELEASE -O3 -s #O3 highest optimisation #-s to strip symbols
CFLAGS += $(if $(CONFIG_SUFFIX),$(RFLAGS),$(DFLAGS))

#A measured dead end, recorded so it is not tried twice. The -Wl,--gc-sections above is very
#nearly inert, because without -ffunction-sections/-fdata-sections the linker's unit of
#discard is a whole object file and one referenced symbol keeps everything compiled beside
#it. Adding both to RFLAGS is the textbook fix and it is worth NOTHING here: tetris.exe
#measured 5.68 MB without them and 5.69 MB with, the 10 KB being the extra section headers.
#The reason is that almost none of the bulk is our code - it is libreactphysics3d.a,
#libimgui.a, libOpenAL32.a and libstdc++, none of which were compiled with -ffunction-sections
#either, so nothing the engine's own compile flags say can make them splittable. That lever
#is in how libs/*.a are built, not here. See docs/engine_backlog.md item 79.

#---------------------------------------------------------------------------------------
# THE LINE BETWEEN SHARED AND PER-APP FLAGS
#
# Everything above is app-invariant, and CORE_CFLAGS freezes it here. Core objects are
# compiled with CORE_CFLAGS and NOTHING ELSE, which is what makes it safe to share one set
# of them between every app in $(ROOT)/build/core.
#
# That guarantee has to be structural rather than a promise, because make cannot see it.
# Objects are compared by timestamp, not by the flags they were built with: if an app
# added a -D to the core compile, whichever app built first would win and every other app
# would silently link objects compiled for someone else. Nothing would rebuild and nothing
# would warn. Adding a flag below this line cannot cause that; adding one above it can.
#
# So: an app-specific compile flag goes in CFLAGS (below), never in CORE_CFLAGS.
#---------------------------------------------------------------------------------------
CORE_CFLAGS := $(CFLAGS)

#Sound is opt-in per app. An app opts in with `USE_SOUND := 1` in its makefile; when it's off,
#core/SoundSystem.cpp and core/WaveFile.cpp are dropped from the core sources this app links,
#so nothing references the audio backend at all.
#
#Note this adds two objects to the shared core directory rather than changing any existing
#one, so a sound app and a silent app can share that directory without interfering: the
#silent app simply does not link the two it never asked for.
#
#THE BACKEND IS miniaudio, NOT OpenAL, since 2026-09-13. There is no -l for it: it is compiled
#into libs/libthirdparty.a, which every app already links, and costs nothing in an app that
#does not call it. That is most of the point - OpenAL measured at about 2.4 MB against the
#fifteen functions core/SoundSystem.cpp used, and miniaudio does the same job in about 179 KB.
#See docs/engine_backlog.md items 80 and 85, and 3rdparty/miniaudio_config.h for the feature
#set and the reason those defines live in a header rather than here.
#
#-lole32 is WASAPI's: the backend is COM. libs/libOpenAL32.a is now unreferenced by any app,
#and the note that used to live here about it failing to link with a mismatched toolchain
#(`undefined reference to __emutls_v._ZSt11__once_call`) went with it.
USE_SOUND ?= 0
ifeq ($(USE_SOUND), 1)
CFLAGS += -DUSE_SOUND -lole32
else
CORE_SRCS_NOSOUND += $(ROOT)/core/SoundSystem.cpp
CORE_SRCS_NOSOUND += $(ROOT)/core/WaveFile.cpp
endif

#---------------------------------------------------------------------------------------
# Sources
#---------------------------------------------------------------------------------------
CORE_DIRS += $(ROOT)/core
CORE_DIRS += $(ROOT)/core/skeleton
CORE_DIRS += $(ROOT)/core/physics

CORE_SRCS := $(filter-out $(CORE_SRCS_NOSOUND), $(wildcard $(addsuffix /*.cpp, $(CORE_DIRS))))

#SHARED LIBRARIES: a folder at the repo root that more than one app builds, declared by an app as
#
#    LIB_DIRS += $(ROOT)/isoterrain
#    IPATHS   += -I$(ROOT)/isoterrain
#
#They join the core sources rather than the app's own, and that is the whole point: like core,
#their code does not vary by app, so they compile once with CORE_CFLAGS into the shared object
#tree and every app that declares them links the same objects. Putting them in APP_SRCS instead
#would not just duplicate the work - the objects would land at build/../../isoterrain/*.o, outside
#the app's own build folder, because the pattern rule mirrors the source path.
#
#A folder used by only ONE app is not this: it belongs inside that app (isocity moved into
#apps/tileset, crane into apps/tank). See docs/asset_layout_plan.md section 2.1.
CORE_SRCS += $(wildcard $(addsuffix /*.cpp, $(LIB_DIRS)))

#BinaryAssetMemoryEmpty.cpp is the empty asset table. A build that bakes its assets in
#replaces it with generator output - see docs/asset_layout_plan.md section 5.
CORE_SRCS += $(ROOT)/BinaryAssetMemoryEmpty.cpp

#Core objects land under $(ROOT)/build/core mirroring their path below the root, so
#core/physics/Physics.cpp becomes build/core/core/physics/Physics.o and nothing collides.
CORE_OBJS := $(patsubst $(ROOT)/%.cpp,$(CORE_BUILD_DIR)/%.o,$(CORE_SRCS))

#The app's objects stay in the app's own build folder, under the configuration they were
#compiled for.
APP_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(APP_SRCS))

#---------------------------------------------------------------------------------------
# Header dependency tracking
#
# Every compile also writes a .d file next to its object listing the project headers it
# included (-MMD: no system headers; -MP: an empty phony rule per header so a deleted or
# renamed one doesn't break the build). Including them makes an object rebuild when any
# header it uses changes. Before this existed, a stale object could link months-old code
# against freshly built core objects - different class sizes for Scene/Renderer/... and
# heap corruption (c0000374) at run time.
#---------------------------------------------------------------------------------------
DEPFLAGS = -MMD -MP
DEPS = $(CORE_OBJS:.o=.d) $(APP_OBJS:.o=.d)

#---------------------------------------------------------------------------------------
# Rules
#---------------------------------------------------------------------------------------
#The goal is the exe BY ITS REAL PATH. Naming the target $(PROJECT).exe while writing
#$(BUILD_DIR)/$(PROJECT).exe would mean make never finds the file it just built, so it
#would relink on every single invocation even with nothing changed.
EXE := $(BUILD_DIR)/$(PROJECT)$(CONFIG_SUFFIX).exe

default: $(EXE)

#The static libraries are prerequisites, not just link arguments, and that is worth the two
#lines. Rebuilding libs/libimgui.a or libs/libthirdparty.a used to leave every exe untouched:
#make compares the exe against the OBJECTS, the libraries are not among them, so `make` said
#nothing to be done and the app kept the code it linked last time. Measured the day this was
#added - a rebuilt libimgui.a took 111 KB off each app, and six of the fourteen picked it up
#only because something else had also changed. The other eight silently did not.
#
#Wildcard rather than a fixed list, so this does not have to be kept in step with whatever
#$(CFLAGS) links; it is every library an app could pull from. A missing one is not an error
#here - the wildcard simply yields nothing - which is what keeps this from breaking a tree
#where a library has not been built yet.
ENGINE_LIBS := $(wildcard $(ROOT)/libs/*.a)

$(EXE): $(APP_OBJS) $(CORE_OBJS) $(ENGINE_LIBS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $^ -o $@ $(LINKS) $(LFLAGS) $(CFLAGS) $(IPATHS)
	@echo "Built $@ ($(CONFIG))"

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPFLAGS) $(CFLAGS) $(IPATHS) $< -o $@

#CORE_CFLAGS, not CFLAGS - these objects are shared between apps. See the note above it.
$(CORE_BUILD_DIR)/%.o: $(ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPFLAGS) $(CORE_CFLAGS) $(IPATHS) $< -o $@

-include $(DEPS)

#Only this app's objects and exe - both configurations of them. The shared core objects
#are left alone - another app is probably using them, and they are not this app's to
#delete.
clean:
	-rm -rf $(BUILD_DIR)

#The shared core objects, for when those are what needs rebuilding. Every configuration,
#not just the one being built: this is the "I do not trust what is in there" button, and
#leaving the other configuration's objects behind would make it a worse one.
cleancore:
	-rm -rf $(CORE_BUILD_ROOT)

.PHONY: default clean cleancore
