#ifndef _SOUND_H_
#define _SOUND_H_

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "BinaryAsset.h"

#include "al.h"
#include "alc.h"

#include "WaveFile.h"

/*
    Maybe... use stb_ogg to load stuff.
*/

class SoundSystem{
public:
    SoundSystem(){};
    ~SoundSystem(){}

    //Simple load wav file?


    bool f_initialised;
    void Initialise();
};

#endif