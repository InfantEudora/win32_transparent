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
# A SHIPPING build is neither of those. It is release AND baked assets AND the three
# developer facilities switched off, and getting four flags right by hand every time is
# not a thing anyone does reliably - so the set is named once, as a target:
#
#     mingw32-make.exe ship               ->  build/<project>_baked_nomcp_nonet_noimgui_release.exe
#
# That name is derived, not chosen, which is why it reads like that; the ship rule echoes
# the path so nobody has to assemble it in their head. See the `ship` block near the end of
# this file for what the five settings are and why USE_SOUND is not among them.
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

#---------------------------------------------------------------------------------------
# THE SETTINGS ARE A CLOSED LIST, AND A NAME OFF IT IS AN ERROR.
#
# `make CONIFG=release` builds debug, quietly and successfully. Make has no notion of an
# unknown variable: an assignment on the command line defines whatever you name, nothing
# reads it, and the real setting keeps its ?= default. The build then differs from the one
# you asked for in exactly the way you were trying to change, and says nothing.
#
# That is the same class of failure the split object trees below are all about - a build
# that is silently not the one you asked for - except that here nothing is even stale, so
# there is no second build that would behave differently and give it away. The only
# evidence is the exe name, and only for the settings that change it.
#
# $(.VARIABLES) plus $(origin) is the whole mechanism: origin is 'command line' only for
# names actually assigned on the command line, so an app makefile's own ROOT, PROJECT,
# APP_SRCS and the rest are not candidates, and neither are -j8 or the goals. Anything
# left over after filtering out the list below was meant to be one of them.
#
# The list is exactly the ?= settings in this file - the knobs a BUILD has, as opposed to
# the ones an APP declares. ASSET_ROOTS, ASSET_PACK_FLAGS, IPATHS, LIB_DIRS and CFLAGS are
# deliberately NOT here: they are the app's to state, they are built up with +=, and a
# command-line assignment would replace rather than add to them, which is a subtler version
# of the same surprise.
#---------------------------------------------------------------------------------------
BUILD_SETTINGS := CONFIG USE_SOUND USE_PHYSICS USE_MCP USE_NET USE_IMGUI BAKE_ASSETS

CMDLINE_SETTINGS := $(foreach v,$(.VARIABLES),$(if $(filter command line,$(origin $v)),$v))
UNKNOWN_SETTINGS := $(filter-out $(BUILD_SETTINGS),$(CMDLINE_SETTINGS))

ifneq ($(UNKNOWN_SETTINGS),)
$(error not a build setting: $(UNKNOWN_SETTINGS) - this build accepts $(BUILD_SETTINGS), and would otherwise have used the default for whichever of those you meant)
endif

#And the values, for the same reason one step down. Every setting but CONFIG is read with
#`ifeq ($(X),1)`, so USE_MCP=ture, USE_MCP=yes and USE_MCP=true are all read as OFF - and an
#app with its debug server switched off looks exactly like one with it on until something
#tries to connect. CONFIG validates its own value where it is used, a few lines below.
#
#Checked here rather than beside each ?= because an empty value passes: a setting that was
#never given has not been given a wrong value either, and its default is applied later. The
#app makefile's own USE_SOUND := 1 is checked too, which is right - a typo is a typo wherever
#it is written.
BOOL_SETTINGS := USE_SOUND USE_PHYSICS USE_MCP USE_NET USE_IMGUI BAKE_ASSETS

$(foreach v,$(BOOL_SETTINGS),$(if $(filter-out 0 1,$($v)),$(error $v must be 0 or 1, not '$($v)')))

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

#-lreactphysics3d is NOT here: it moved into the USE_PHYSICS block below, which is the whole
#point of that flag. Everything else on this line is wanted by every app unconditionally.
CFLAGS = -std=c++17 -L$(ROOT)/libs/ -lsetupapi -lhid -lthirdparty -luser32 -lopengl32 -lgdi32 -Wl,-Bstatic -static-libstdc++ -static-libgcc -static -lstdc++ -Wl,--gc-sections -D_WIN32
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

#tinygltf's built-in image decoding, which core/GLTFLoader.cpp does not use: it reads the
#still-encoded bytes out of the bufferView and decodes them with 3rdparty/stb_image through
#Texture::LoadFromMemory. Leaving it on decoded every embedded texture a second time, compiled
#a second stb_image in, and kept every decoded image alive inside tinygltf::Model for the life
#of the loader - which for a glb of compressed textures is the real cost, not the ~16 KB of code.
#
#Both sides again, and for the STRONG reason: like NO_FS this switches an in-class initialiser
#(TinyGLTF::LoadImageData, to nullptr), so a TU compiled without it references
#tinygltf::LoadImageData, which the library no longer defines. Repeated in 3rdparty/makefile.
CFLAGS += -DTINYGLTF_NO_STB_IMAGE
#Sound is optional - see the USE_SOUND block below.
#NOTHING links winmm now, not even a sound build: that went with OpenAL, and miniaudio's
#WASAPI backend wants only -lole32. core/PrecisionSleeper resolves timeBeginPeriod from
#winmm.dll at run time anyway, and only on pre-Win10-1803 machines where the
#high-resolution waitable timer is unavailable, so nothing needs it at link time.

#---------------------------------------------------------------------------------------
# USE_PHYSICS - ReactPhysics3D, and whether this build has one at all
#
# USE_PHYSICS=0 drops rp3d entirely: core/physics/*.cpp, Vehicle, Wheel, Particle and
# ParticleEmitter leave the source list, -DUSE_PHYSICS is not defined, and every rp3d
# reference in Object/Scene/Application compiles out. Nothing links libreactphysics3d.a.
#
# WHY IT IS WORTH A FLAG, measured rather than assumed. --gc-sections does NOT recover this
# on its own: bomber never creates a PhysicsWorld, yet 88 of rp3d's ~94 archive members were
# in its shipped exe, because the core objects it links NAME those symbols whether or not the
# app ever calls them. The linker collects on reachability, not on behaviour. Measured on
# apps/bomber, `make ship`, 2026-09-18:
#
#     rp3d                1,520,532 bytes      core/physics/*.o       85,504
#     Vehicle + Wheel        58,224            ObjectCollider.o       26,320
#     Particle.o             25,976            ------------------------------
#                                              total               1,716,556
#
# which was 67% of that exe's non-asset code and data. Five of the fifteen apps create no
# physics world at all (bomber, ocpp, sim, testfx, ui), and a host BUILD TOOL built on core
# - a sprite packer, a mesh viewer - wants this even more than an app does. See item 73 in
# docs/engine_backlog.md.
#
# THIS FLAG IS NOT SWITCHED THE WAY USE_MCP AND USE_IMGUI ARE, and the difference matters.
# Those two swap a whole translation unit against a _none twin precisely so no -D ever
# reaches core. That works for them because they change function BODIES behind a signature
# that stays put. USE_PHYSICS changes a HEADER - the type of Object::physics, the type of
# Scene::physics_world, and which methods Object even declares - and every TU that includes
# Object.h sees it. No twin file can absorb that, so -DUSE_PHYSICS genuinely has to reach the
# shared core objects, and it is therefore set ABOVE the CORE_CFLAGS line below.
#
# WHICH MAKES THE CORE OBJECT TREE THE THING TO GET RIGHT. An app-specific -D in CORE_CFLAGS
# is exactly the silent mix-up that line exists to prevent: make compares objects by
# timestamp, never by the flags they were built under, so whichever app built first would
# win and the rest would link someone else's objects. The answer is CONFIG's, not USE_MCP's -
# a flag that legitimately changes core gets its OWN core tree. build/core/release and
# build/core/release_nophysics cannot collide, neither needs wiping, and switching costs one
# rebuild of the other tree and then nothing. That is also why this needs no .buildflags
# stamp (docs/engine_backlog.md item 72): both the shared objects and the app's own objects
# are already separated by directory.
#
# DEFAULTS ON, unlike USE_SOUND. Ten of fifteen apps genuinely want a physics engine, and an
# app that lost one silently would not shrink, it would fall through the floor.
#---------------------------------------------------------------------------------------
USE_PHYSICS ?= 1

ifeq ($(USE_PHYSICS),1)
CFLAGS += -DUSE_PHYSICS -lreactphysics3d
#Only reachable when physics is on. With it off the -I goes too, so a guard someone forgot
#fails loudly with "reactphysics3d.h: No such file" at compile time instead of quietly
#pulling the library back into a build that asked not to have one.
IPATHS += -I$(ROOT)/3rdparty/reactphysics3d/
else
#The rp3d wrapper itself.
CORE_SRCS_DROP += $(ROOT)/core/physics/Physics.cpp
CORE_SRCS_DROP += $(ROOT)/core/physics/PhysicsBody.cpp
CORE_SRCS_DROP += $(ROOT)/core/physics/PhysicsWorld.cpp
#Classes that ARE a physics body rather than merely having one - there is no meaningful
#no-physics version of any of these, so they drop whole rather than being #ifdef'd. Nothing
#in core includes Vehicle.h or Particle.h; only apps/tank, dozer and ship use them.
CORE_SRCS_DROP += $(ROOT)/core/Vehicle.cpp
CORE_SRCS_DROP += $(ROOT)/core/Wheel.cpp
CORE_SRCS_DROP += $(ROOT)/core/Particle.cpp
CORE_SRCS_DROP += $(ROOT)/core/ParticleEmitter.cpp
#The collider-editing gizmo: its whole job is to hold an rp3d::Collider* and write back to it.
CORE_SRCS_DROP += $(ROOT)/core/ObjectCollider.cpp
#Its own core tree - see the long note above. The app's own objects and the exe are named by
#VARIANT_SUFFIX further down, for the same reason.
CORE_BUILD_DIR := $(CORE_BUILD_DIR)_nophysics
endif

IPATHS += -I$(ROOT)/core/
IPATHS += -I$(ROOT)/core/physics
IPATHS += -I$(ROOT)/core/skeleton
#The vector/matrix/quaternion headers, in their own folder so `core/` lists the engine's classes
#rather than thirteen type_ files. Included by plain name everywhere (`#include "type_vec3.h"`),
#which is what this -I keeps working - so the move needed no include changed anywhere.
IPATHS += -I$(ROOT)/core/types
IPATHS += -I$(ROOT)/3rdparty/imgui/
IPATHS += -I$(ROOT)/3rdparty/
IPATHS += -I$(ROOT)/3rdparty/stb_image/
IPATHS += -I$(ROOT)/3rdparty/openal-soft/
IPATHS += -I$(ROOT)/3rdparty/miniz/
#-I$(ROOT)/3rdparty/reactphysics3d/ is in the USE_PHYSICS block above, not here.
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
CORE_SRCS_DROP += $(ROOT)/core/SoundSystem.cpp
CORE_SRCS_DROP += $(ROOT)/core/WaveFile.cpp
endif

#---------------------------------------------------------------------------------------
# USE_MCP and USE_NET - the debug server, and the sockets underneath it
#
# USE_MCP=0 drops the JSON-RPC server an agent drives the app through: core/MCPServer.cpp,
# the core tools (see below), and the app's own RegisterMCPTools() block. Nothing binds
# 127.0.0.1:8765, which is most of the point - a shipped game has no use for a remote
# control, and item 74 in docs/engine_backlog.md is the long version.
#
# USE_NET=0 drops the sockets with it: Socket, TCPServer, TCPClient, HTTPServer, and
# -lws2_32 -lcrypt32. It is SEPARATE from USE_MCP because apps/ocpp needs an HTTP server
# whether or not it wants a debug interface - its OCPPClient is a TCPClient. Folding the two
# together would make MCP load-bearing for a feature that has nothing to do with it.
#
# USE_MCP=1 implies USE_NET=1; there is no such thing as MCP without a socket.
#
# BOTH DEFAULT ON, unlike USE_SOUND and every other flag in this family, and that is
# deliberate rather than an oversight. Sound defaults off because silence is a safe default.
# MCP is the opposite: screenshot, sim_pause and sim_step are how work in this repo is
# verified at all (see CLAUDE.md), so an app that quietly lost them would break the
# development loop rather than merely shrink. These get turned off ON PURPOSE, when
# releasing something, and never by accident.
#
# HOW THE CORE SIDE IS SWITCHED, because it is not the obvious way. A flag must never
# CHANGE a core translation unit - see the CORE_CFLAGS block below - so -DUSE_MCP is NOT
# visible to core. Instead core/ApplicationMCP.cpp and core/ApplicationMCP_none.cpp define
# the same three symbols, one doing the work and one doing nothing, and exactly one of them
# is compiled. core/Application.cpp calls them unconditionally and is identical either way.
# -DUSE_MCP below the CORE_CFLAGS line reaches only the APP's own objects, which is where
# an app's own RegisterMCPTools() is guarded.
#---------------------------------------------------------------------------------------
USE_MCP ?= 1
USE_NET ?= 1

ifeq ($(USE_MCP),1)
USE_NET := 1
CFLAGS += -DUSE_MCP
CORE_SRCS_DROP += $(ROOT)/core/ApplicationMCP_none.cpp
else
CORE_SRCS_DROP += $(ROOT)/core/ApplicationMCP.cpp
CORE_SRCS_DROP += $(ROOT)/core/MCPServer.cpp
endif

ifeq ($(USE_NET),1)
CFLAGS += -DUSE_NET -lws2_32 -lcrypt32
else
CORE_SRCS_DROP += $(ROOT)/core/Socket.cpp
CORE_SRCS_DROP += $(ROOT)/core/TCPServer.cpp
CORE_SRCS_DROP += $(ROOT)/core/TCPClient.cpp
CORE_SRCS_DROP += $(ROOT)/core/HTTPServer.cpp
endif

#---------------------------------------------------------------------------------------
# USE_IMGUI - the debug panels
#
# The fourth and last member of the family. USE_IMGUI=0 drops the menu bar, the Scene tree,
# the Inspector, the Engine panel, the shader list, click-to-select, each app's own HUD, and
# -limgui with them: 618 KB of a stripped exe plus its vendored font.
#
# WHAT IT DOES NOT DROP is the 2D overlay. core/UIOverlay is a separate thing - one
# screen-space pass drawing rounded rects and SDF text, built for Android where ImGui is not
# an option - and it is how a SHIPPED build says anything to a player. ImGui is developer
# surface; the overlay is the game's. See docs/ui_overlay_plan.md.
#
# Defaults ON for the same reason USE_MCP does: the panels are where the telemetry, the
# inspector and the sliders live, and an app that lost them by accident would be much harder
# to work on rather than merely smaller.
#
# Switched the same two ways as USE_MCP, and for the same reason. CORE: a swapped translation
# unit, because -D must never change a shared core object - core/ApplicationDebugUI.cpp and
# core/WindowImGui.cpp against their _none twins. APPS: -DUSE_IMGUI below the CORE_CFLAGS line,
# where an #ifdef around an app's own panel functions is safe.
#---------------------------------------------------------------------------------------
USE_IMGUI ?= 1

ifeq ($(USE_IMGUI),1)
CFLAGS += -DUSE_IMGUI -limgui
CORE_SRCS_DROP += $(ROOT)/core/ApplicationDebugUI_none.cpp
CORE_SRCS_DROP += $(ROOT)/core/WindowImGui_none.cpp
else
CORE_SRCS_DROP += $(ROOT)/core/ApplicationDebugUI.cpp
CORE_SRCS_DROP += $(ROOT)/core/WindowImGui.cpp
endif

#---------------------------------------------------------------------------------------
# A NON-DEFAULT FLAG GETS ITS OWN OBJECT TREE AND ITS OWN EXE NAME.
#
# Both flags above put a -D into CFLAGS, so they change what the APP's objects compile to -
# and make compares objects by timestamp, never by the flags they were built with. Sharing
# one tree and one exe name between `make` and `make USE_MCP=0` means building one, then the
# other, then the first again leaves make comparing an exe against objects that are all older
# than it: "nothing to be done", and you keep whichever binary you built before.
#
# That is the same failure the CONFIG block describes for debug-vs-release, and it is worth
# more care here than anywhere else in this file. The whole point of USE_MCP=0 is to produce
# a build that does NOT listen on a port; getting a silently stale binary means shipping a
# game with a debug server in it, which is precisely the thing the flag exists to prevent.
#
# The suffix is empty when everything is at its default, so an ordinary build is untouched:
# `make` still writes build/tetris.exe out of build/obj/debug. Only a deliberately non-default
# build gets a name, and it gets one that says what it is.
#---------------------------------------------------------------------------------------
VARIANT_SUFFIX :=
#Physics first, because it is the one that also moves the SHARED core tree (see the
#USE_PHYSICS block above) - having the exe say so as well keeps the two readable together.
ifneq ($(USE_PHYSICS),1)
VARIANT_SUFFIX := $(VARIANT_SUFFIX)_nophysics
endif
ifneq ($(USE_MCP),1)
VARIANT_SUFFIX := $(VARIANT_SUFFIX)_nomcp
endif
ifneq ($(USE_NET),1)
VARIANT_SUFFIX := $(VARIANT_SUFFIX)_nonet
endif
ifneq ($(USE_IMGUI),1)
VARIANT_SUFFIX := $(VARIANT_SUFFIX)_noimgui
endif
OBJ_DIR := $(OBJ_DIR)$(VARIANT_SUFFIX)

#---------------------------------------------------------------------------------------
# Sources
#---------------------------------------------------------------------------------------
CORE_DIRS += $(ROOT)/core
CORE_DIRS += $(ROOT)/core/skeleton
CORE_DIRS += $(ROOT)/core/physics
CORE_DIRS += $(ROOT)/core/types

CORE_SRCS := $(filter-out $(CORE_SRCS_DROP), $(wildcard $(addsuffix /*.cpp, $(CORE_DIRS))))

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

#---------------------------------------------------------------------------------------
# BAKED ASSETS
#
# An app opts in with, in its own makefile:
#
#     BAKE_ASSETS := 1
#     ASSET_ROOTS := $(ROOT)/shared_assets          # same ORDER main.cpp declares them
#     ASSET_PACK_FLAGS += --exclude "www/*"         # optional
#
# Off (the default) nothing here changes: the app links BinaryAssetMemoryEmpty.cpp, the
# asset table is empty, and every name resolves through the search path to a file on disk.
# That is the right arrangement for development and it stays the default.
#
# On, tools/assetpack walks those roots and writes three files that belong together:
#
#     build/generated/assets.bin              the compressed blob
#     build/generated/assets.S                four lines of .incbin pulling it in
#     build/generated/BinaryAssetMemory.cpp   the table, pointing into the blob
#
# and the app links those INSTEAD of BinaryAssetMemoryEmpty.cpp. See tools/assetpack_plan.md
# for why .incbin rather than a byte array: 2 MB of assets is 10 MB of C source and 3.09 s
# to compile, against 0.05 s to assemble, and it scales linearly - grid's 72 MB assembles
# in half a second.
#
# TWO THINGS TO KNOW BEFORE TURNING IT ON.
#
# A BAKED ASSET WINS OVER A FILE ON DISK, always. LoadFile asks GetBinaryAsset before it
# touches the search path (core/File.cpp), so in a baked build editing a shader next to the
# exe does nothing at all - the baked copy answers. That is the correct behaviour for a
# shipped build and a trap during development, which is the other reason the default is off.
# An app that bakes should generally stop declaring disk roots in its main.cpp too; they can
# then only mislead. -DASSETS_BAKED is defined here so it can do that in one #ifndef.
#
# THE GENERATED FILES ARE THE APP'S, NOT THE CORE'S. They are compiled into $(OBJ_DIR) with
# CFLAGS like any other app source, never into the shared $(ROOT)/build/core tree - two apps
# bake different assets, and a shared object holding one app's table linked into another is
# exactly the silent, timestamp-invisible mix-up the CORE_CFLAGS line above exists to stop.
#---------------------------------------------------------------------------------------
BAKE_ASSETS ?= 0
BAKE_SUFFIX :=

ifeq ($(BAKE_ASSETS),1)

ifeq ($(strip $(ASSET_ROOTS)),)
$(error BAKE_ASSETS is 1 but ASSET_ROOTS is empty - name the roots to bake, in the same order main.cpp declares them)
endif

#PER-VARIANT, BUT STILL NOT PER-CONFIGURATION, and the split is the whole point.
#
#Not per-configuration, because debug and release bake the SAME BYTES: one pack serves both and
#switching configuration does not repack. That was the original reasoning and it still holds.
#The OBJECTS built from these files land in $(OBJ_DIR), which is per-configuration.
#
#Per-VARIANT, because $(ASSET_PACK_FLAGS) can depend on the flag family, and now does. An app
#excludes ImGui's font when USE_IMGUI=0 and must keep it otherwise (apps/tetris/makefile has the
#worked example), so `make ship` and `make BAKE_ASSETS=1` want DIFFERENT packs from the same
#tree. With one shared directory the second build to run finds the first one's table newer than
#everything and reuses it: measured 2026-09-14, `make ship` after a measurement build shipped a
#254 KB font it had explicitly excluded, and the reverse order is the one that dies at startup.
#
#So it is exactly the argument the VARIANT_SUFFIX block makes about objects and exe names, about
#a different kind of output: a build whose flags change what comes out needs somewhere of its own
#to put it. $(VARIANT_SUFFIX) is empty for a default build, so an ordinary bake is untouched and
#build/generated is still build/generated.
GENERATED   := $(BUILD_DIR)/generated$(VARIANT_SUFFIX)
PACK_TOOL   := $(ROOT)/tools/assetpack/build/assetpack.exe
BAKED_TABLE := $(GENERATED)/BinaryAssetMemory.cpp
BAKED_STUB  := $(GENERATED)/assets.S
BAKED_BLOB  := $(GENERATED)/assets.bin

#So an app can compile its disk roots out - see the note above.
CFLAGS += -DASSETS_BAKED

#AND THEREFORE ITS OWN OBJECT TREE, for exactly the reason CONFIG has one. -DASSETS_BAKED
#changes what main.cpp compiles to, and make compares objects by TIMESTAMP, not by the flags
#they were built with. Sharing one tree would mean `make BAKE_ASSETS=0` after a baked build
#relinking a main.o that still has its asset roots compiled out - against an empty table, so
#the app would come up and die on its first asset with nothing to suggest why. Splitting the
#tree makes that unrepresentable and costs one rebuild the first time each way is used, which
#is the same trade the debug/release split already makes.
OBJ_DIR := $(OBJ_DIR)/baked

#AND ITS OWN EXE NAME, for the second half of that same reason - the half that is easy to
#stop one step short of. Splitting only the objects is not enough: build loose, then baked,
#then loose again, and make compares the BAKED exe against loose objects that are all older
#than it, says "nothing to be done", and leaves the baked binary sitting there under the name
#you asked for. Measured while writing this, and it is exactly what the CONFIG block above
#describes for debug-vs-release. Distinct names make it unrepresentable and let both exist at
#once, which is also what makes "does baking change anything?" a question you can answer by
#running them side by side.
BAKE_SUFFIX := _baked

APP_SRCS += $(BAKED_TABLE)
APP_ASM  += $(BAKED_STUB)

else

#BinaryAssetMemoryEmpty.cpp is the empty asset table, and what every non-baking app links.
CORE_SRCS += $(ROOT)/BinaryAssetMemoryEmpty.cpp

endif

#Core objects land under $(ROOT)/build/core mirroring their path below the root, so
#core/physics/Physics.cpp becomes build/core/core/physics/Physics.o and nothing collides.
CORE_OBJS := $(patsubst $(ROOT)/%.cpp,$(CORE_BUILD_DIR)/%.o,$(CORE_SRCS))

#The app's objects stay in the app's own build folder, under the configuration they were
#compiled for.
APP_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(APP_SRCS))

#Assembler sources - only the generated .incbin stub uses this today. Kept separate from
#APP_SRCS because the pattern rule and the flags differ: an .S needs no -MMD, no IPATHS and
#no C++ flags, and passing them would be noise at best.
ASM_OBJS := $(patsubst %.S,$(OBJ_DIR)/%.o,$(APP_ASM))

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
#$(BAKE_SUFFIX) is _baked when BAKE_ASSETS=1 and empty otherwise - see the BAKED ASSETS
#block for why it is in the name rather than only in the object path.
EXE := $(BUILD_DIR)/$(PROJECT)$(BAKE_SUFFIX)$(VARIANT_SUFFIX)$(CONFIG_SUFFIX).exe

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

$(EXE): $(APP_OBJS) $(ASM_OBJS) $(CORE_OBJS) $(ENGINE_LIBS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $^ -o $@ $(LINKS) $(LFLAGS) $(CFLAGS) $(IPATHS)
	@echo "Built $@ ($(CONFIG))"

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPFLAGS) $(CFLAGS) $(IPATHS) $< -o $@

#Assembler. g++ rather than `as` so the driver picks the right target and the .S goes
#through the preprocessor, which is what the capital S asks for.
$(OBJ_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@

#CORE_CFLAGS, not CFLAGS - these objects are shared between apps. See the note above it.
$(CORE_BUILD_DIR)/%.o: $(ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPFLAGS) $(CORE_CFLAGS) $(IPATHS) $< -o $@

-include $(DEPS)

#---------------------------------------------------------------------------------------
# Baking the assets
#---------------------------------------------------------------------------------------
ifeq ($(BAKE_ASSETS),1)

#Recursive wildcard. GNU make has no builtin, and $(wildcard $(d)/*) sees ONLY THE TOP
#LEVEL - which stopped being good enough the moment assets moved into meshes/, shaders/
#and sound/, which is now every app. Without it an edit to assets/sound/click.wav would
#not trigger a repack and the app would go on running against a stale baked-in copy, with
#nothing anywhere to say so.
#
#Deliberately pure make rather than $(shell find ...), so it does not depend on which
#shell make picked - this repo has both Bash and PowerShell in play.
#
#THE SPACE BEFORE $(filter IS LOAD-BEARING. Without it the results concatenate and make
#reports a nonsense target like 'No rule to make target assets/sound/probe.wavassets/sound'.
rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

#The ROOTS are prerequisites alongside their contents, and that is not redundant: rwildcard
#lists a directory's CONTENTS, so deleting a whole subdirectory removes both it and its
#files from the list at once, nothing looks out of date, and the deleted assets stay baked
#in. The parent's own mtime is the only thing that changes on a delete.
ASSET_FILES := $(foreach d,$(ASSET_ROOTS),$(call rwildcard,$(d),*))

#A GROUPED target (&:, GNU make 4.3+). One run of the tool writes all three files, so this
#has to be one rule producing three outputs. Three ordinary rules would each invoke the
#packer - three times the work serially, and with -j a race in which three copies write the
#same blob at once.
#
#$(MAKEFILE_LIST) IS A PREREQUISITE BECAUSE THE FLAGS LIVE THERE. The recipe below passes
#$(ASSET_PACK_FLAGS), which an app sets in its own makefile - and make compares TIMESTAMPS, so
#without this an edit to that list changes nothing: the generated table is newer than every
#asset and the tool, make says there is nothing to do, and the app keeps the blob it packed
#under the OLD exclusions. Measured 2026-09-14, adding an exclusion and rebuilding: the pack
#did not re-run, and the only way to notice was to count the entries in the generated .cpp.
#
#That is the same class of bug as the split object trees above, and worse here, because the
#stale artefact is data rather than code: an exclusion that has quietly not taken effect ships
#an asset you meant to drop, and one that has quietly not been REMOVED ships a build that dies
#on its first LoadFile. MAKEFILE_LIST is every makefile read so far - this file and the app's -
#which is exactly the set that can carry ASSET_ROOTS or ASSET_PACK_FLAGS.
$(BAKED_TABLE) $(BAKED_STUB) $(BAKED_BLOB) &: $(PACK_TOOL) $(MAKEFILE_LIST) $(ASSET_ROOTS) $(ASSET_FILES)
	@mkdir -p $(GENERATED)
	$(PACK_TOOL) $(ASSET_PACK_FLAGS) -o $(GENERATED) $(ASSET_ROOTS)

#Built on demand, and rebuilt when its own sources change - not merely when it is missing.
#An app silently baking with a stale packer is the kind of thing that is only noticed much
#later. The + hands the sub-make this make's jobserver rather than starting a second one.
#
#CONFIG=debug IS PINNED, AND IS LOAD-BEARING. A command-line assignment propagates to every
#sub-make through MAKEFLAGS, so without this `make ship` - which recurses with CONFIG=release
#- would hand tools.mk a release configuration, and tools.mk names its exe per configuration
#exactly as this file does. The sub-make would cheerfully build assetpack_release.exe, this
#rule's target assetpack.exe would still not exist, make would consider it updated anyway,
#and the recipe above would fail on a missing packer in the one build nobody runs often.
#The packer is a build-time tool whose own optimisation level has nothing to do with the
#artifact being shipped, so pinning the default is right as well as necessary.
$(PACK_TOOL): $(wildcard $(ROOT)/tools/assetpack/*.cpp) $(ROOT)/tools/assetpack/makefile $(ROOT)/tools/tools.mk
	+$(MAKE) -C $(ROOT)/tools/assetpack CONFIG=debug

endif

#---------------------------------------------------------------------------------------
# ship - the one build that is not for us
#
# FIVE settings have to agree before a build is fit to hand to a player, and every one of
# them defaults to what a DEVELOPER wants. Spelling them out on the command line every time
# is how one of them eventually gets left off, and the one most worth leaving off is
# USE_MCP=0: a shipped game that still listens on 127.0.0.1:8765 is the precise failure
# that flag exists to prevent, and nothing about the running app would look wrong. So the
# set lives here, named once, and the whole instruction is:
#
#     mingw32-make.exe ship -j8
#
# which means:
#
#     CONFIG=release    -O3 -s, no debug symbols       ~13x smaller than the debug exe
#     BAKE_ASSETS=1     assets compiled into the exe   nothing to ship beside it
#     USE_MCP=0         no JSON-RPC debug server       and so no listening socket
#     USE_NET=0         no sockets at all
#     USE_IMGUI=0       no menu bar, Scene tree, Inspector or per-app debug HUD
#
# USE_IMGUI=0 DOES NOT LEAVE THE GAME MUTE. core/UIOverlay is a separate thing and stays -
# it is how a shipped build talks to a player, ImGui is how the engine talks to us. The
# USE_IMGUI block above has the long version.
#
# USE_SOUND IS DELIBERATELY ABSENT from that list. It is a property of the APP, not of the
# build - Tetris ships with sound because Tetris has sound - so ship inherits whatever the
# app's own makefile asked for and does not second-guess it. The four above are all
# developer surface, which is what makes them the build's business rather than the app's.
#
# AN APP THAT HAS NOT DECLARED ASSET_ROOTS CANNOT SHIP YET, and says so: BAKE_ASSETS=1
# raises the $(error) in the BAKED ASSETS block above. That is the correct answer rather
# than a nuisance - quietly dropping the bake would produce an exe that looks shippable and
# dies on its first asset wherever it lands. Declare the roots, in the order main.cpp does.
#
# A SUB-MAKE, not target-specific variables. EXE, CFLAGS, OBJ_DIR and CORE_SRCS are all
# computed while this file is being READ, and a target-specific variable is set long after
# that - `ship: CONFIG=release` would happily build the debug exe and call it release. The
# leading + hands over this make's jobserver, so `make ship -j8` really does use eight.
#---------------------------------------------------------------------------------------
ship:
	+$(MAKE) CONFIG=release BAKE_ASSETS=1 USE_MCP=0 USE_NET=0 USE_IMGUI=0 shipname

#Runs INSIDE that sub-make, where $(EXE) has already been computed AS the ship exe - which
#is the point: the name is derived from the same suffix rules as every other build rather
#than spelled out a second time here, where it could drift. `default` as a prerequisite
#keeps it last under -j, and means the path is still printed when everything is up to date
#and the link rule's own "Built" line never runs.
shipname: default
	@echo "Ship build: $(EXE)"

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

.PHONY: default ship shipname clean cleancore
