#include "RRandom.h"
#include "Debug.h"
#include "type_helpers.h"

static Debugger* debug = new Debugger("RRandom",DEBUG_ALL);

//Since we need a random texture at some point, this might as well sample from that.
Texture* RRandom::rnd_texture = NULL;

//Alternatively, the rand() from stlib is quite easy. And there are some easy float implementations:
//https://iquilezles.org/articles/sfrand/

//Initialize static texture where we can sample from
RRandom::RRandom(){
    if (rnd_texture == NULL){
        rnd_texture = new Texture();
        rnd_texture->LoadFromFile("data/textures/noise.png",GL_TEXTURE_2D,-1);
    }
}

//Sets the index where we start reading noise values from.
void RRandom::SetSeed(uint32_t value){
    if (rnd_texture && (rnd_texture->img_data_sz > 0)){
        seed = value % (rnd_texture->img_data_sz + 1);
    }
}

//Returns a random uint8_t
uint8_t RRandom::Get_uint8(){
    if (rnd_texture && (rnd_texture->img_data_sz > 0)){
        //Limit state to size of texture
        if (state >= rnd_texture->img_data_sz){
            state = 0;
        }else{
            state++;
        }
        uint8_t r = rnd_texture->img_data[state];
        return r;
    }else{
        return 0;
    }
}

//Returns a random int between INT_MIN and INT_MAX
int RRandom::GetInt(){
    int d = 0;
    int* iptr = &d;
    for (int i = 0;i<sizeof(int);i++){
        uint8_t* uptr = (uint8_t*)iptr + i;
        *uptr = Get_uint8();
    }
    return d;
}

//Returns a random integer between min and max
int RRandom::GetInt(int imin, int imax){
    if (imin >= imax){
        return min(imin,imax);
    }
    int dist = imax - imin;
    int r = abs(GetInt());
    r = r % (dist+1);
    return imin + r;
}

float RRandom::GetFloat(float fmin, float fmax){
    //Size so that floats can be gotten in range 0 - 1/size
    float size = 1000;
    if (fmin >= fmax){
        return min(fmin,fmax);
    }
    float res = GetInt(fmin * size,fmax * size);
    res /= size;
    return res;
}

//Returns a float from a normal distribution specified by mean and standard deviation
//We use the Marsaglia Polar Method because it's easy to read mainly....
float RRandom::GetNormalFloat(float mean, float stdev){
    if (hasspare) {
        hasspare = false;
        return spare * stdev + mean;
    }else{
        double u, v, s;
        do {
            u = GetFloat(-1,1);
            v = GetFloat(-1,1);
            s = u * u + v * v;
        } while (s >= 1.0 || s == 0.0);
        s = sqrt(-2.0 * log(s) / s);
        spare = v * s;
        hasspare = true;
        return mean + stdev * u * s;
    }
}

//Returns if you won with a certain chance of winning.
bool RRandom::Roll(float chance){
    chance = clamp(chance,0,1);
    if (chance == 0){
        return false;
    }
    int imax = 10000;
    int draw = GetInt(0,imax);
    if (draw <= (chance * imax)){
        return true;
    }
    return false;
}