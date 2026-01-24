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

CFLAGS = -std=c++17 -Llibs/ -lreactphysics3d-0.10.2 -limgui -lthirdparty -luser32 -lopengl32 -lgdi32 -lws2_32 -lcrypt32 -Wl,-Bstatic -static-libstdc++ -static-libgcc -static -lstdc++ -Wl,--gc-sections -D_WIN32
#CFLAGS += -std=c++11
#CFLAGS += -ffunction-sections -fdata-sections -Wl,--gc-sections
#CFLAGS += $(FNOCONSOLE)
CFLAGS += -fno-exceptions -DJSON_NOEXCEPTION
#CFLAGS += -Wcast-align=strict
CFLAGS += -lOpenAL32 -DAL_LIBTYPE_STATIC -lole32 -lwinmm


PROJECT = wind

DIR_SRC += ./core
DIR_SRC += ./core/skeleton
DIR_SRC += ./core/physics
IPATHS += -Icore/
IPATHS += -Icore/physics

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
SRC_LIBIMGUI += 3rdparty/imgui/backends/imgui_impl_win32.cpp
SRC_LIBIMGUI += 3rdparty/imgui/backends/imgui_impl_opengl3.cpp
OBJ_LIBIMGUI += $(patsubst %.cpp, %.o, $(SRC_LIBIMGUI))

#Third Party
SRC_LIBTHIRDPARTY += ./3rdparty/stb_image/stb_image.cpp
SRC_LIBTHIRDPARTY += ./3rdparty/tinygltf/tiny_gltf.cpp
SRC_LIBTHIRDPARTY += ./3rdparty/miniz/miniz_tdef.cpp
SRC_LIBTHIRDPARTY += ./3rdparty/miniz/miniz_tinfl.cpp
OBJ_LIBTHIRDPARTY += $(patsubst %.cpp, %.o, $(SRC_LIBTHIRDPARTY))

ifeq ($(COMPILE_BINARYASSETS), 1)
	SRCS += BinaryAssetMemory.cpp
else
	SRCS += BinaryAssetMemoryEmpty.cpp
endif

#ApplicationAnimation
#CFLAGS+= -DAPP_HEADER=\"ApplicationAnimation.h\"
#CFLAGS+= -DAPP_CLASS=ApplicationAnimation
#SRCS += ApplicationAnimation.cpp

#ApplicationGrid
#CFLAGS+= -DAPP_HEADER=\"ApplicationGrid.h\"
#CFLAGS+= -DAPP_CLASS=ApplicationGrid
#SRCS += ApplicationGrid.cpp
#IPATHS += -Iisoterrain/
#DIR_SRC += ./isoterrain

#ApplicationDozer
#SRCS += ApplicationDozer.cpp
#IPATHS += -Idozer/
#DIR_SRC += ./dozer

#SRCS += ImCurveEdit.cpp
#SRCS += ImSequencer.cpp

#ApplicationTileset
#CFLAGS+= -DAPP_HEADER=\"ApplicationTileset.h\"
#CFLAGS+= -DAPP_CLASS=ApplicationTileset
#SRCS += ApplicationTileset.cpp
#IPATHS += -Iisoterrain/
#DIR_SRC += ./isoterrain
#IPATHS += -Iisocity/
#DIR_SRC += ./isocity

#ApplicationSim
#CFLAGS+= -DAPP_HEADER=\"ApplicationSim.h\"
#CFLAGS+= -DAPP_CLASS=ApplicationSim
#IPATHS += -Igalaxy/
#DIR_SRC += ./galaxy
#SRCS += ApplicationSim.cpp
#SRCS += imgooey.cpp

#ApplicationUI
#CFLAGS+= -DAPP_HEADER=\"ApplicationUI.h\"
#CFLAGS+= -DAPP_CLASS=ApplicationUI
#SRCS += ApplicationUI.cpp

#ApplicationOCPP
#CFLAGS+= -DAPP_HEADER=\"ApplicationOCPP.h\"
#CFLAGS+= -DAPP_CLASS=ApplicationOCPP
#SRCS += ApplicationOCPP.cpp

#ApplicationShip
CFLAGS+= -DAPP_HEADER=\"ApplicationShip.h\"
CFLAGS+= -DAPP_CLASS=ApplicationShip
SRCS += ApplicationShip.cpp
IPATHS += -Iship/
DIR_SRC += ./ship


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

$(OBJ_LIBTHIRDPARTY): %.o: %.cpp
	$(CC) -c $(CFLAGS) $(IPATHS) $< -o $@

thirdparty: $(OBJ_LIBTHIRDPARTY)
	mkdir -p libs
	@echo "Linking Thrird Party stuff into static Library thridparty.a"
	-rm -rf libs/libthirdparty.a
	ar q libs/libthirdparty.a $(OBJ_LIBTHIRDPARTY)

imgui: $(OBJ_LIBIMGUI)
	mkdir -p libs
	@echo "Linking IMGui into static Library imgui.a"
	-rm -rf libs/libimgui.a
	ar q libs/libimgui.a $(OBJ_LIBIMGUI)

all: default

reset:
	-rm -rf main.o ApplicationAnimation.o core/Object.o core/skeleton/PlayerCharacter.o

clean:
	-rm -rf $(OBJS) $(OBJ_LIBIMGUI) $(OBJ_LIBTHIRDPARTY)
	-rm -rf $(OBJS) $(OBJ_LIBIMGUI)

superclean:
	-rm -rf libs/libimgui.a