APP_HEADER := ApplicationSim.h
APP_CLASS  := ApplicationSim

IPATHS  += -Igalaxy/
DIR_SRC += ./galaxy
SRCS    += ApplicationSim.cpp
SRCS    += imgooey.cpp

#Uses SoundSystem -> needs OpenAL linked in.
USE_SOUND := 1
