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
