APP_HEADER := ApplicationShip.h
APP_CLASS  := ApplicationShip

SRCS    += ApplicationShip.cpp
IPATHS  += -Iship/
DIR_SRC += ./ship

#Ship owns the raymarched cloud shaders (raymarch_volume.frag, cloud_shadow.comp, noise3d.comp
#and the density.glsl they both #include). Nothing else uses them, so they live in the app's own
#asset root rather than the shared one - see docs/asset_layout_plan.md.
APP_ASSETS += assets/ship
