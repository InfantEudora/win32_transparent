APP_HEADER := ApplicationTetris.h
APP_CLASS  := ApplicationTetris

SRCS    += ApplicationTetris.cpp
IPATHS  += -Itetris/
DIR_SRC += ./tetris

#Line clears, lock-downs and rotations play sounds - see core/SoundSystem.h.
USE_SOUND := 1
