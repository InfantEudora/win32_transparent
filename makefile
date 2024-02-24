#Compiler
CC = g++

#No Console on windows, just the window
FNOCONSOLE = -Wl,-subsystem,windows

CFLAGS = -lopengl32 -lgdi32 -Wl,-Bstatic -static-libstdc++ -static-libgcc -static -lstdc++ -Wl,--gc-sections -D_WIN32
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

SRCS += InputController.cpp
SRCS += OBJLoader.cpp

SRCS += glad.cpp

DFLAGS = -DDEBUG -Og -g #-g Produce debug info for GDB. -O0 fastest compilation time.
RFLAGS = -DRELEASE -O3 -s #03 highest optimisation #-s to strip symbols

CFLAGS += $(RFLAGS)

OBJS  +=  $(patsubst %.cpp, %.o, $(SRCS))

default: $(OBJS)
	$(CC) $^ -o $(PROJECT) $(LINKS) $(LFLAGS) $(CFLAGS) $(IPATHS)

$(OBJS): %.o: %.cpp
	$(CC) -c $(CFLAGS) $(IPATHS) $< -o $@

all: default

clean:
	-rm -rf $(OBJS)