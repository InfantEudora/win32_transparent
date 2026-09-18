#=======================================================================================
# spritepacker - a GUI for building and inspecting sprite sheets.
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # -> build/spritepacker.exe
#     ./build/spritepacker.exe 2>stderr.log &
#
# COPIED FROM THE ANDROID PORT at C:/code/android/tools/sprite_packer, 2026-09-17, so it
# can be maintained here. The four sources are that tool's, near-verbatim; every local
# change is re-applied by a script that ASSERTS ON ITS ANCHOR, which is the merge method
# docs/engine_backlog.md records under "Reference: merging the Android port". Re-copy and
# re-run it rather than hand-editing, or the next merge is a diff nobody reads.
#
# WHAT WAS ADAPTED, all of it small: the port's Application has SetupScene/BuildAppTabs/
# RenderUI hooks and carries the shader names and the Scene; this one has Init and
# DrawImGuiUI and hands out a Scene from CreateNewScene. Each site says so where it sits.
#
#---------------------------------------------------------------------------------------
# WHY THIS INCLUDES engine.mk RATHER THAN tools.mk
#
# tools.mk builds a console program that links a NAMED HANDFUL of core sources - lockd
# takes MCPServer, TCPServer and Debug, and that is the shape it is for. This tool opens a
# window, makes a GL context, uploads textures and draws ImGui, which is very nearly all of
# core plus imgui plus glad. That is engine.mk's job and duplicating it here would be a
# second copy of the thing engine.mk exists to stop being copied.
#
# engine.mk carries no apps/ path - only $(ROOT) and the relative build/ and assets/ - so
# it works from tools/ unchanged, at the same depth. The exe lands in build/ beside this
# file exactly as an app's does.
#
# THE COST, said out loud: `make ship` and BAKE_ASSETS come along with it, and neither
# means anything for a tool nobody hands to a player. They are harmless (ship needs
# ASSET_ROOTS, which this does not declare, and says so) and not worth a fork of engine.mk
# to remove.
#---------------------------------------------------------------------------------------

ROOT    := ../..
PROJECT := spritepacker

APP_SRCS += main.cpp
APP_SRCS += SpritePackerApplication.cpp

#No MCP server and no sockets. An agent does not drive this - a person does, with a mouse -
#and a tool that binds 8765 would fight whichever app is actually being debugged for it.
#See the note on one-app-at-a-time in CLAUDE.md.
USE_MCP := 0
USE_NET := 0

#ImGui IS the tool. Everything it does is in DrawImGuiUI, so a USE_IMGUI=0 build would open
#an empty window; the guard around that function is there for the compile, not as an option
#worth exercising.
USE_IMGUI ?= 1

include $(ROOT)/engine.mk

#---------------------------------------------------------------------------------------
# AFTER THE INCLUDE, AND THAT IS NOT A STYLE CHOICE.
#
# engine.mk sets `CFLAGS = ...` with a plain assignment rather than `+=`, so anything an app
# adds to CFLAGS ABOVE the include is silently discarded - the flags simply never reach the
# link line and the only symptom is an undefined reference to whatever you were linking for.
# Every other knob in this file is set before the include because engine.mk reads those with
# ?= or tests them; CFLAGS is the one that has to come after.
#
# What it buys: the native folder picker is COM - IFileOpenDialog with FOS_PICKFOLDERS, see
# BrowseForFolder. -lole32 is CoInitializeEx/CoCreateInstance/CoTaskMemFree; -luuid is the
# CLSID_FileOpenDialog and IID_IFileOpenDialog symbols themselves, which live in their own
# import library and are the half that is easy to miss.
#---------------------------------------------------------------------------------------
CFLAGS += -lole32 -luuid
