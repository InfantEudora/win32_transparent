#ifndef _ARCHER_PLACE_HASH_H_
#define _ARCHER_PLACE_HASH_H_

#include <math.h>
#include <stdint.h>

/*
    The randomness the level dressing uses - Foliage and Vine both.

    A hash of WHERE, not a stream. Each draw is a function of the spot's position, the try and the
    channel it is for, so moving one box re-grows only the tops near it - a stream would reshuffle
    every plant after the first one that changed. Quantised to a millimetre so float noise in an
    editor-moved position cannot flip a plant. And nothing here touches the engine's shared RRandom
    stream, which the simulation's replay depends on.
*/
static inline float Hash01(float x, float y, int a, int b){
    uint32_t h = (uint32_t)(int32_t)lroundf(x * 1000.0f) * 374761393u;
    h += (uint32_t)(int32_t)lroundf(y * 1000.0f) * 668265263u;
    h += (uint32_t)a * 2246822519u;
    h += (uint32_t)b * 3266489917u;
    h = (h ^ (h >> 15)) * 2246822519u;
    h = (h ^ (h >> 13)) * 3266489917u;
    h = h ^ (h >> 16);
    return (float)(h & 0xFFFFFF) / (float)0x1000000;
}

#endif
