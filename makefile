#Compiler
CC = g++

#No Console on windows, just the window
FLAG_NOCONSOLE = -Wl,-subsystem,windows

CFLAGS = -lopengl32  -lgdi32 -lglu32 -Wl,-Bstatic -static-libstdc++ -static-libgcc  -static -lstdc++  -Wl,--gc-sections

PROJECT = wind

SRCS += main.cpp
SRCS += Debug.cpp
SRCS += Window.cpp
SRCS += glad.cpp

DFLAGS = -DDEBUG -Og -g #-g Produce debug info for GDB. -O0 fastest compilation time.
RFLAGS = -DRELEASE -O3 -s #03 highest optimisation #-s to strip symbols

#CFLAGS += $(DFLAGS);

OBJS  +=  $(patsubst %.cpp, %.o, $(SRCS))

default: $(OBJS)
	$(CC) $^ -o $(PROJECT) $(LINKS) $(LFLAGS) $(CFLAGS) $(IPATHS)

$(OBJS): %.o: %.cpp
	$(CC) -c $(CFLAGS) $(IPATHS) $< -o $@

all: default

clean:
	-rm -rf $(OBJS)