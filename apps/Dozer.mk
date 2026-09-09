APP_HEADER := ApplicationDozer.h
APP_CLASS  := ApplicationDozer

SRCS    += ApplicationDozer.cpp
IPATHS  += -Idozer/
DIR_SRC += ./dozer

#Uses SoundSystem -> needs OpenAL linked in.
USE_SOUND := 1
