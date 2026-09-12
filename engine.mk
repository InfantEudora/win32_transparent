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
#=======================================================================================

CC = g++

ifndef ROOT
$(error ROOT is not set - an app makefile must set ROOT to the repo root before including engine.mk)
endif
ifndef PROJECT
$(error PROJECT is not set - an app makefile must set PROJECT to its exe name before including engine.mk)
endif

.DEFAULT_GOAL := default

#Where objects go. The app's own objects are private to the app; the core, imgui and
#third-party objects are shared, because they no longer vary by app.
BUILD_DIR      := build
CORE_BUILD_DIR := $(ROOT)/build/core

#No Console on windows, just the window
FNOCONSOLE = -Wl,-subsystem,windows

CFLAGS = -std=c++17 -L$(ROOT)/libs/ -lreactphysics3d -limgui -lsetupapi -lhid -lthirdparty -luser32 -lopengl32 -lgdi32 -lws2_32 -lcrypt32 -Wl,-Bstatic -static-libstdc++ -static-libgcc -static -lstdc++ -Wl,--gc-sections -D_WIN32
CFLAGS += -lXinput9_1_0
#CFLAGS += $(FNOCONSOLE)
CFLAGS += -fno-exceptions -DJSON_NOEXCEPTION
#Sound (OpenAL) is optional - see the USE_SOUND block below.
#Nothing outside it links winmm any more: core/PrecisionSleeper resolves timeBeginPeriod
#from winmm.dll at run time, and only on pre-Win10-1803 machines where the
#high-resolution waitable timer is unavailable.

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
CFLAGS += $(DFLAGS)

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

#Sound is opt-in per app. libs/libOpenAL32.a is a prebuilt static library; when it hasn't
#been rebuilt with the current toolchain it won't link (mismatched libstdc++ TLS symbols:
#`undefined reference to __emutls_v._ZSt11__once_call`). Apps that never touch SoundSystem
#shouldn't pay for that, so an app opts in with `USE_SOUND := 1` in its makefile. When it's
#off, core/SoundSystem.cpp and core/WaveFile.cpp are dropped from the core sources this app
#links and -lOpenAL32 is not passed, so nothing references OpenAL at all.
#
#Note this adds two objects to the shared core directory rather than changing any existing
#one, so a sound app and a silent app can share that directory without interfering: the
#silent app simply does not link the two it never asked for.
USE_SOUND ?= 0
ifeq ($(USE_SOUND), 1)
CFLAGS += -DUSE_SOUND -lOpenAL32 -lole32 -lwinmm
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

#BinaryAssetMemoryEmpty.cpp is the empty asset table. A build that bakes its assets in
#replaces it with generator output - see docs/asset_layout_plan.md section 5.
CORE_SRCS += $(ROOT)/BinaryAssetMemoryEmpty.cpp

#Core objects land under $(ROOT)/build/core mirroring their path below the root, so
#core/physics/Physics.cpp becomes build/core/core/physics/Physics.o and nothing collides.
CORE_OBJS := $(patsubst $(ROOT)/%.cpp,$(CORE_BUILD_DIR)/%.o,$(CORE_SRCS))

#The app's objects stay in the app's own build folder.
APP_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(APP_SRCS))

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
default: $(BUILD_DIR)/$(PROJECT).exe

$(BUILD_DIR)/$(PROJECT).exe: $(APP_OBJS) $(CORE_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $^ -o $@ $(LINKS) $(LFLAGS) $(CFLAGS) $(IPATHS)
	@echo "Built $@"

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPFLAGS) $(CFLAGS) $(IPATHS) $< -o $@

#CORE_CFLAGS, not CFLAGS - these objects are shared between apps. See the note above it.
$(CORE_BUILD_DIR)/%.o: $(ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPFLAGS) $(CORE_CFLAGS) $(IPATHS) $< -o $@

-include $(DEPS)

#Only this app's objects and exe. The shared core objects are left alone - another app
#is probably using them, and they are not this app's to delete.
clean:
	-rm -rf $(BUILD_DIR)

#The shared core objects, for when those are what needs rebuilding.
cleancore:
	-rm -rf $(CORE_BUILD_DIR)

.PHONY: default clean cleancore
