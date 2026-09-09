APP_HEADER := ApplicationTileset.h
APP_CLASS  := ApplicationTileset

SRCS    += ApplicationTileset.cpp
IPATHS  += -Iisoterrain/
DIR_SRC += ./isoterrain
IPATHS  += -Iisocity/
DIR_SRC += ./isocity

#Uses SoundSystem -> needs OpenAL linked in.
USE_SOUND := 1
