#=======================================================================================
# llm - a bench for running a small language model (Ministral 3) in GL compute shaders.
# The model's forward pass runs on the GPU through the engine's own GL context and Shader
# class, so whatever works here can be lifted into a game unchanged. MCP tools drive it
# without a keyboard: load a model, generate, score a text, run a two-character duet.
#
# The model files and the CPU reference implementation (runq) live outside this repo, in
# the llama_c project (H:\llama_c\llama2.c on the desktop); the model is loaded by path.
#
# Build and run from this folder:
#
#     export PATH="/c/msys64/mingw64/bin:$PATH"      # the toolchain is NOT on the default PATH
#     mingw32-make.exe -j8                           # mingw32-make, not /usr/bin/make
#     ./build/llm.exe --minimized --mcp-port 8769 2>stderr.log &
#
# `make clean` removes this app's build folder only. The shared core objects under
# ../../build/core belong to every app; `make cleancore` is the one that clears those.
#=======================================================================================

ROOT    := ../..
PROJECT := llm

APP_SRCS += main.cpp
APP_SRCS += ApplicationLLM.cpp
APP_SRCS += LLMModel.cpp
APP_SRCS += LLMTokenizer.cpp
APP_SRCS += LLMChat.cpp

#No SoundSystem here, so OpenAL is not linked - see the USE_SOUND block in engine.mk.

include $(ROOT)/engine.mk
