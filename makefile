#Compiler
CC = g++

#No Console on windows, just the window
FLAG_NOCONSOLE = -Wl,-subsystem,windows

default:
	$(CC) main.cpp -o main -lopengl32  -lgdi32 -lglu32 -Wl,-Bstatic -static-libstdc++ -static-libgcc  -static -lstdc++  -Wl,--gc-sections

all: default