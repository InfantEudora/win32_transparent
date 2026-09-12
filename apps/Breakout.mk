APP_HEADER := ApplicationBreakout.h
APP_CLASS  := ApplicationBreakout

SRCS    += ApplicationBreakout.cpp
IPATHS  += -Ibreakout/
DIR_SRC += ./breakout

#The ball, the bricks, the shield and the paddle all make noise - see core/SoundSystem.h.
USE_SOUND := 1
