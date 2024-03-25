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

DIR_SRC += ./core
IPATHS += -Icore/

SRCS += main.cpp
SRCS += $(wildcard $(addsuffix /*.cpp, $(DIR_SRC)))

ifeq ($(COMPILE_ASSETS), 1)
	SRCS += AssetMemory.cpp
else
	SRCS += AssetMemoryEmpty.cpp
endif

ifeq ($(DUMP_ASSETS), 1)
CFLAGS += -DDUMP_ASSETS
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

all: default

clean:
	-rm -rf $(OBJS)