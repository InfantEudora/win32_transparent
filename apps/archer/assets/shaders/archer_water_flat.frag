#version 430 core
//The pool and the stream. Everything is in archer_water.glsl - see WHY TWO FILES there. No
//discard in this one, so the stream hidden under the grass is depth-rejected before it is shaded.
#include "archer_water.glsl"
