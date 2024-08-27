#Compiler
CC = g++

#No Console on windows, just the window
FNOCONSOLE = -Wl,-subsystem,windows

#This is a two stage process:
##First make with DUMP then with COMPILE
DUMP_BINARYASSETS    = 0#Set when all assets need to be dumped to a file.
COMPILE_BINARYASSETS = 0#Set when all assets need to be compiled into the application binary.

CFLAGS = -Llibs/ -limgui -lopengl32 -lgdi32 -lwinmm -Wl,-Bstatic -static-libstdc++ -static-libgcc -static -lstdc++ -Wl,--gc-sections -D_WIN32
#CFLAGS += -ffunction-sections -fdata-sections -Wl,--gc-sections
#CFLAGS += $(FNOCONSOLE)

PROJECT = wind

DIR_SRC += ./core
DIR_SRC += ./isoterrain
IPATHS += -Icore/
IPATHS += -Iisoterrain/

IPATHS += -I3rdparty/imgui/
IPATHS += -I3rdparty/

SRCS += main.cpp
SRCS += $(wildcard $(addsuffix /*.cpp, $(DIR_SRC)))

#ImGUI
SRC_LIBIMGUI += 3rdparty/imgui/imgui.cpp
SRC_LIBIMGUI += 3rdparty/imgui/imgui_draw.cpp
SRC_LIBIMGUI += 3rdparty/imgui/imgui_widgets.cpp
SRC_LIBIMGUI += 3rdparty/imgui/imgui_tables.cpp
SRC_LIBIMGUI += 3rdparty/imgui/imgui_demo.cpp
SRC_LIBIMGUI += 3rdparty/imgui/backends/imgui_impl_win32.cpp
SRC_LIBIMGUI += 3rdparty/imgui/backends/imgui_impl_opengl3.cpp
OBJ_LIBIMGUI += $(patsubst %.cpp, %.o, $(SRC_LIBIMGUI))

ifeq ($(COMPILE_BINARYASSETS), 1)
	SRCS += BinaryAssetMemory.cpp
else
	SRCS += BinaryAssetMemoryEmpty.cpp
endif

#Application
#SRCS += ApplicationCANUI.cpp
#SRCS += $(wildcard $(addsuffix /*.cpp, ./caninterfaces))
#SRCS += $(wildcard $(addsuffix /*.cpp, ./caninterfaces/controllers))

#ApplicationGrid
SRCS += ApplicationGrid.cpp

ifeq ($(DUMP_BINARYASSETS), 1)
CFLAGS += -DDUMP_BINARYASSETS
endif

DFLAGS = -DDEBUG -Og -g #-g Produce debug info for GDB. -O0 fastest compilation time.
RFLAGS = -DRELEASE -O3 -s #03 highest optimisation #-s to strip symbols

CFLAGS += $(DFLAGS)

OBJS  +=  $(patsubst %.cpp, %.o, $(SRCS))

default: $(OBJS) $(DEPOBJS)
	$(CC) $^ -o $(PROJECT) $(LINKS) $(LFLAGS) $(CFLAGS) $(IPATHS)
#@echo COMPILE_ASSETS == $(COMPILE_ASSETS)

$(OBJS): %.o: %.cpp
	$(CC) -c $(CFLAGS) $(IPATHS) $< -o $@

$(OBJ_LIBIMGUI): %.o: %.cpp
	$(CC) -c $(CFLAGS) $(IPATHS) $< -o $@

imgui: $(OBJ_LIBIMGUI) $(OBJ_LOCALLIB_C)
	mkdir -p libs
	@echo "Linking IMGui into static Library imgui.a"
	ar q libs/libimgui.a $(OBJ_LIBIMGUI) $(OBJ_LOCALLIB_C)

all: default

clean:
	-rm -rf $(OBJS) $(OBJ_LIBIMGUI)