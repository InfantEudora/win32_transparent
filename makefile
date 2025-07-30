#Compiler
CC = g++

#No Console on windows, just the window
FNOCONSOLE = -Wl,-subsystem,windows

#Building
##ImGui can be compiled from source into a static library first. Again, to save on compile time.
##TODO: Add more stuff that barely changes in to a static library.

#This is a two stage process:
##First make with DUMP then with COMPILE
##All loaded assets will get dumped in a single C file.
##There can be no space after a 0 or 1 in a makefile... for some reason.
##Then, recompile with the comile flag on... and all files will get loaded from the executable!
DUMP_BINARYASSETS    = 0#Set when all assets need to be dumped to a file.
COMPILE_BINARYASSETS = 0#Set when all assets need to be compiled into the application binary.

CFLAGS = -std=c++17 -Llibs/ -lreactphysics3d -limgui -luser32 -lopengl32 -lgdi32 -lwinmm -lws2_32 -Wl,-Bstatic -static-libstdc++ -static-libgcc -static -lstdc++ -Wl,--gc-sections -D_WIN32
#CFLAGS += -std=c++11
#CFLAGS += -ffunction-sections -fdata-sections -Wl,--gc-sections
#CFLAGS += $(FNOCONSOLE)
CFLAGS += -fno-exceptions -DJSON_NOEXCEPTION


PROJECT = wind

DIR_SRC += ./core
DIR_SRC += ./core/skeleton
IPATHS += -Icore/

SRCS += ./3rdparty/stb_image/stb_image.cpp
SRCS += ./3rdparty/tinygltf/tiny_gltf.cpp
DIR_SRC += ./3rdparty/miniz

IPATHS += -I3rdparty/imgui/
IPATHS += -I3rdparty/
IPATHS += -I3rdparty/stb_image/
IPATHS += -I3rdparty/openal-soft/
IPATHS += -I3rdparty/miniz/
IPATHS += -I3rdparty/reactphysics3d/


SRCS += main.cpp

#ImGUI
SRC_LIBIMGUI += 3rdparty/imgui/imgui.cpp
SRC_LIBIMGUI += 3rdparty/imgui/imgui_draw.cpp
SRC_LIBIMGUI += 3rdparty/imgui/imgui_widgets.cpp
SRC_LIBIMGUI += 3rdparty/imgui/imgui_tables.cpp
#SRC_LIBIMGUI += 3rdparty/imgui/imgui_demo.cpp
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
#SRCS += ApplicationGrid.cpp
#IPATHS += -Iisoterrain/
#DIR_SRC += ./isoterrain

#ApplicationDozer
SRCS += ApplicationDozer.cpp

#SRCS += ImCurveEdit.cpp
#SRCS += ImSequencer.cpp

#ApplicationTileset
#SRCS += ApplicationTileset.cpp

#ApplicationSim
#IPATHS += -Igalaxy/
#DIR_SRC += ./galaxy
#SRCS += ApplicationSim.cpp
#SRCS += imgooey.cpp

SRCS += $(wildcard $(addsuffix /*.cpp, $(DIR_SRC)))

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

superclean:
	-rm -rf libs/libimgui.a