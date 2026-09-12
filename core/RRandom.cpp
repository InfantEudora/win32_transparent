#include "RRandom.h"
#include "Debug.h"
#include "type_helpers.h"

#include <math.h>
#include <stdlib.h>

static Debugger* debug = new Debugger("RRandom",DEBUG_ALL);

//Alternatively, the rand() from stlib is quite easy. And there are some easy float implementations:
//https://iquilezles.org/articles/sfrand/


RRandom::RRandom(int seed){
    this->seed = (uint32_t)seed;
    state = 0;
}

RRandom::~RRandom(){
    ReleaseBuffer();
}

//Only a buffer from Generate belongs to this instance. One handed over by UseNoise is left
//alone: its owner is still using it, and is meant to outlive this generator.
void RRandom::ReleaseBuffer(){
    if (f_owns_buffer && buffer){
        free(buffer);
    }
    buffer = NULL;
    buffer_sz = 0;
    f_owns_buffer = false;
}

//Places this instance's cursor at an offset derived from its seed, so two instances sharing one
//buffer read different parts of it. Only sensible for the shared buffer: Generate puts the seed
//into the CONTENTS instead, and starting that at a non-zero offset would mean the first draws of a
//fresh private stream came from the middle of it for no reason.
void RRandom::SetStartOffset(){
    if (buffer_sz > 0){
        state = (uint32_t)(seed % buffer_sz);
    }else{
        state = 0;
    }
}

//See the header for why each of the three cases does what it does.
void RRandom::SetSeed(uint32_t new_seed){
    seed = new_seed;
    if (f_owns_buffer && buffer && (buffer_sz > 0)){
        //Taken by value first: Generate releases the buffer before it allocates the new one, so
        //reading buffer_sz inside the call would be reading a member that is about to be zeroed.
        size_t size = buffer_sz;
        Generate(size);
    }else if (buffer && (buffer_sz > 0)){
        SetStartOffset();
    }
}

//Side of a square holding the buffer. Derived, never stored - noise has no meaningful shape, so
//there is nothing to remember. floor, so side*side never runs off the end of the buffer.
int RRandom::GetSquareSide() const{
    if (buffer_sz == 0){
        return 0;
    }
    return (int)sqrt((double)buffer_sz);
}

//Points this instance at noise somebody else owns - see the header, and
//Texture::LoadIntoRRandomNoiseFile, which is what calls it. Nothing is copied and nothing will be
//freed here: f_owns_buffer stays false, so the destructor leaves the owner's memory alone.
void RRandom::UseNoise(uint8_t* data, size_t size){
    //A previous Generate on this instance left a private buffer behind; it is about to become
    //unreachable, so free it rather than leak it.
    ReleaseBuffer();

    if (!data || (size == 0)){
        debug->Err("RRandom::UseNoise given %s - every draw will return 0\n",
                   data ? "a zero-length buffer" : "no buffer");
        return;
    }
    buffer = data;
    buffer_sz = size;
    f_owns_buffer = false;
    //The seed means an OFFSET here rather than contents: several instances on one buffer are
    //meant to sample the same noise from different places.
    SetStartOffset();
    //A fresh buffer deserves a fresh complaint if it turns out to be unusable.
    f_warned_empty = false;
}

//Builds a buffer belonging to THIS instance, filled from its seed. Two instances generated with
//different seeds therefore share nothing at all.
void RRandom::Generate(size_t num_bytes){
    ReleaseBuffer();

    if (num_bytes == 0){
        debug->Err("RRandom::Generate called with 0 bytes - every draw will return 0\n");
        return;
    }

    uint8_t* data = (uint8_t*)malloc(num_bytes);
    if (!data){
        debug->Err("RRandom::Generate could not allocate %llu bytes\n",(unsigned long long)num_bytes);
        return;
    }

    //xorshift32 rather than srand()/rand(). Two reasons, both of which matter to a class whose
    //entire purpose is reproducibility: rand() is one global stream, so seeding it here silently
    //reseeded it for everything else in the process that uses it; and its sequence is a property
    //of whichever C runtime the binary was linked against, so the "same" seed gives different
    //noise on a different toolchain - which defeats handing a seed to another machine. This is
    //nine lines and produces the same bytes everywhere.
    //
    //The high byte is taken rather than the low one: the low bits of an xorshift word are its
    //weakest, and the top 8 are what a byte-sized draw should come from. (The old fill was
    //`rand() % 255`, which as a bonus could never produce 255 at all.)
    uint32_t s = seed ? seed : 1u;  //xorshift32 is stuck at zero, and 0 is a plausible seed
    //Warm the state up before taking any output. A small seed (and the seeds actually used here
    //are small - the default is 1) leaves the word small for the first few rounds, so its TOP byte
    //is still 0 and the one after it is nearly a multiple of the seed. Measured without this:
    //seeds 1 and 2 both began 0, and then 4 and 8 - i.e. the first draws of every stream were
    //almost seed-independent, which is precisely what a seed is for. Sixteen rounds is far more
    //than enough to mix a 32-bit state and costs nothing once.
    for (int warmup=0;warmup<16;warmup++){
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
    }
    for (size_t i=0;i<num_bytes;i++){
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        data[i] = (uint8_t)(s >> 24);
    }

    buffer = data;
    buffer_sz = num_bytes;
    f_owns_buffer = true;
    //A private buffer starts at the beginning: the seed is already expressed in its contents.
    state = 0;
}

//The 2D spelling, for callers already thinking in a width and a height. The shape is not kept -
//see the header - so this is only a multiply.
void RRandom::Generate(int w, int h){
    if ((w <= 0) || (h <= 0)){
        debug->Err("RRandom::Generate called with %ix%i - every draw will return 0\n",w,h);
        ReleaseBuffer();
        return;
    }
    Generate((size_t)w * (size_t)h);
}

//Returns a random uint8_t
uint8_t RRandom::Get_uint8(){
    if (!buffer || (buffer_sz == 0)){
        //Once, not per draw - this is called thousands of times a second and the point is to be
        //noticed, not to bury the log. Silently returning 0 forever is the failure an instance
        //that was never Generate()d produces, and from the outside it looks like a generator
        //that simply always rolls the minimum, which is a miserable thing to track down.
        if (!f_warned_empty){
            f_warned_empty = true;
            debug->Err("RRandom: drawing from an empty generator - call Generate() or "
                       "UseNoise() first. Every draw returns 0.\n");
        }
        return 0;
    }
    //Reads at the cursor and THEN advances, so the offset SetStartOffset picked is genuinely the
    //first byte this instance draws. (This used to advance first, which meant byte 0 was only ever
    //reached by wrapping around, and a seeded offset would have been off by one.)
    if (state >= buffer_sz){
        state = 0;  //buffer replaced by a smaller one since the cursor was last set
    }
    uint8_t r = buffer[state];
    state++;
    if (state >= buffer_sz){
        state = 0;
    }
    return r;
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

//Returns a random integer between min and max (both inclusive)
//
//The arithmetic is done UNSIGNED on purpose. This used to be `abs(GetInt()) % (dist+1)`, which has
//two defects: abs(INT_MIN) is undefined behaviour (GetInt builds its int out of four random bytes,
//so it produces INT_MIN about one draw in four billion), and `imax - imin` overflows for any range
//wider than INT_MAX. Neither can happen in uint32_t, which wraps instead of trapping and holds
//every possible distance between two ints.
int RRandom::GetInt(int imin, int imax){
    if (imin >= imax){
        return min(imin,imax);
    }
    //Width of the range as an unsigned count of steps. Can be up to 0xFFFFFFFF (INT_MIN..INT_MAX),
    //in which case dist+1 would wrap to 0 - so that one case takes the whole 32-bit draw as-is.
    uint32_t dist = (uint32_t)imax - (uint32_t)imin;
    uint32_t r = (uint32_t)GetInt();
    if (dist == 0xFFFFFFFFu){
        return (int)((uint32_t)imin + r);
    }
    return (int)((uint32_t)imin + (r % (dist + 1u)));
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

vec3 RRandom::GetVec3(float min, float max){
    vec3 r;
    r.x = GetFloat(min,max);
    r.y = GetFloat(min,max);
    r.z = GetFloat(min,max);
    return r;
}
