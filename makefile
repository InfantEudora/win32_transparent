#Compiler
CC = g++

#No Console on windows, just the window
FNOCONSOLE = -Wl,-subsystem,windows

#This is a two stage process
COMPILE_ASSETS = 0#Set when all assets need to be compiled into the application binary.
DUMP_ASSETS = 0#Set when all assets need to be dumped to a file.

CFLAGS = -lopengl32 -lgdi32 -lwinmm -Wl,-Bstatic -static-libstdc++ -static-libgcc -static -lstdc++ -Wl,--gc-sections -D_WIN32
#CFLAGS += -ffunction-sections -fdata-sections -Wl,--gc-sections
#CFLAGS += $(FNOCONSOLE)

PROJECT = wind

SRCS += main.cpp
SRCS += Debug.cpp
SRCS += Window.cpp
SRCS += Renderer.cpp
SRCS += File.cpp
SRCS += Shader.cpp

SRCS += Object.cpp
SRCS += Camera.cpp
SRCS += Mesh.cpp
SRCS += Texture.cpp
SRCS += Asset.cpp

ifeq ($(COMPILE_ASSETS), 1)
	SRCS += AssetMemory.cpp
else
	SRCS += AssetMemoryEmpty.cpp
endif

ifeq ($(DUMP_ASSETS), 1)
CFLAGS += -DDUMP_ASSETS
endif

SRCS += InputController.cpp
SRCS += OBJLoader.cpp
SRCS += glad.cpp
SRCS += type_helpers.cpp

SRCS += Scene.cpp
SRCS += Application.cpp

DFLAGS = -DDEBUG -Og -g #-g Produce debug info for GDB. -O0 fastest compilation time.
RFLAGS = -DRELEASE -O3 -s #03 highest optimisation #-s to strip symbols

CFLAGS += $(DFLAGS)

OBJS  +=  $(patsubst %.cpp, %.o, $(SRCS))

default: $(OBJS) $(DEPOBJS)
	$(CC) $^ -o $(PROJECT) $(LINKS) $(LFLAGS) $(CFLAGS) $(IPATHS)
#@echo COMPILE_ASSETS == $(COMPILE_ASSETS)

$(OBJS): %.o: %.cpp
	$(CC) -c $(CFLAGS) $(IPATHS) $< -o $@

all: default

clean:
	-rm -rf $(OBJS)