#=======================================================================================
# lockd - the advisory file-lock broker several agents working in one checkout claim
# paths from before editing them. See docs/lock_broker.md and lockd.cpp.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"     # the toolchain is NOT on the default PATH
#     cd tools/lockd && mingw32-make.exe -j8        # mingw32-make, not /usr/bin/make
#     ./build/lockd.exe                             # foreground, with a console: type 'help'
#     ./build/lockd.exe --no-console 2>lockd.log &  # ...or backgrounded, the older way
#     ./build/lockd.exe --list                      # the same exe is also the client
#     ./build/lockd.exe --release-all               # the panic button
#     ./build/lockd.exe --help
#
# It runs in the FOREGROUND by default, reading commands from stdin, which is why nothing
# here may ever start MCPServer's stdio transport - see the note at the top of lockd.cpp.
#
# NOTE that the linker cannot overwrite a running exe, so a rebuild means stopping the
# broker - which drops every lease in the table, including other agents'. Check
# `--list` first and pick a quiet moment, or expect to have interrupted somebody. From
# the broker's own console that is `list` then `quit`.
#
# Three core sources and nothing else - no window, no GL, no physics. MCPServer pulls in
# TCPServer and Debug and stops there, which is the whole reason this is cheap: the broker
# gets the same JSON-RPC dispatch, tool registry and HTTP transport every app has, for
# about 300 lines of its own.
#
# It is a TOOL rather than an app on purpose. tools.mk compiles these core sources into
# this folder's own build/obj tree, so building the broker cannot race an app build in the
# shared ../../build/core - which for the one process whose job is stopping agents from
# colliding would be a poor way to start.
#=======================================================================================

ROOT    := ../..
PROJECT := lockd

TOOL_SRCS += lockd.cpp
TOOL_SRCS += LockTable.cpp

CORE_SRCS += $(ROOT)/core/MCPServer.cpp
CORE_SRCS += $(ROOT)/core/TCPServer.cpp
CORE_SRCS += $(ROOT)/core/Debug.cpp
CORE_SRCS += $(ROOT)/core/Debug_win32.cpp

#Winsock, for TCPServer's listening socket, and crypt32 for the base64 in MCPServer's
#image attachment - unused here (nothing returns a PNG) but it is one symbol in a shared
#source and not worth an #ifdef.
LIBS += -lws2_32 -lcrypt32

include $(ROOT)/tools/tools.mk
