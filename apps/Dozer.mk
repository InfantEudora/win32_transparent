# NOTE: predates the APP_HEADER/APP_CLASS pattern - add those two defines
# below (see apps/ship.mk for the shape) before this will link against main.cpp.
SRCS    += ApplicationDozer.cpp
IPATHS  += -Idozer/
DIR_SRC += ./dozer
