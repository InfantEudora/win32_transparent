#=======================================================================================
# OCPP - an Open Charge Point Protocol client/server test bench, with a browser UI served
# by the engine's own HTTPServer on port 9090.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/ocpp.exe 2>stderr.log &
#
# assets/www is the page it serves on port 9090. core/HTTPServer is what loads it, but OCPP
# is the only app that runs one, so it owns it - see docs/asset_layout_plan.md section 2.2.
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := ocpp

#This app IS a network application - it serves a page over HTTP and speaks OCPP over
#websockets, and its OCPPClient is a TCPClient. USE_NET=0 would leave it linking against
#sockets that were not compiled; said plainly here rather than as 26 undefined references
#from the linker. USE_MCP=0 is fine and supported - that is exactly why the two are separate
#flags, see the block in engine.mk.
ifneq ($(USE_NET),1)
$(error apps/ocpp cannot be built with USE_NET=0 - HTTP and websockets are what it is)
endif

APP_SRCS += main.cpp
APP_SRCS += ApplicationOCPP.cpp

#The protocol itself, moved out of core/ on 2026-09-14. It lived there, and core/HTTPServer
#held an OCPPServerHandler as a member, so every app in the tree linked a charge-point client
#and its server handler - core/MCPServer stands on HTTPServer, and every app has MCP. Measured
#at 94,720 bytes of stripped exe for OCPPClient alone, in all fourteen apps.
#
#HTTPServer now exposes websocket hooks instead (WebSocketApp in core/HTTPServer.h) and this
#app installs the protocol into them, which is the arrangement the handler was always written
#for - it has taken its send and set-variable dependencies as std::function from the start.
#One app, one owner: docs/asset_layout_plan.md section 2.1.
APP_SRCS += OCPPClient.cpp
APP_SRCS += OCPPServerHandler.cpp

include $(ROOT)/engine.mk
