#ifndef _RRANDOM_H_
#define _RRANDOM_H_
#include <stdint.h>
#include <string>
#include "Texture.h"
#include "type_vec3.h"

/*
    Reproducable random.

    Numbers come from a buffer of noise rather than from an algorithm evaluated per call. The
    buffer is a Texture because that is a form the GPU can sample directly and the network can
    send: the same noise can drive a shader and the simulation, and a second machine can be handed
    the exact bytes rather than trusted to reproduce them.

    --- HOW TO USE IT -------------------------------------------------------------------------

    RECOMMENDED: ONE INSTANCE PER GAME, created once during Init() and used everywhere.

        rrand = new RRandom(seed);      //Application::rrand already exists for this
        rrand->Generate(512,512);

    That is the shape the class is built for. A single stream is trivially reproducible: given the
    seed, every draw in the run follows. It is also what the deterministic-replay direction needs
    (see docs/engine_backlog.md) - though note that a single shared stream only replays if every
    draw happens on the simulation thread, in tick order. A draw from the UI or an MCP tool thread
    shifts the stream for the simulation and breaks that, whatever the seeding looks like.

    SEVERAL INSTANCES ARE SUPPORTED, and the two constructors of a stream differ in what the seed
    means, because the two cases want different things:

      Generate(w,h)  - this instance gets its OWN buffer, filled from `seed`. Two instances with
                       different seeds hold genuinely different noise and share nothing. Use this
                       when the streams must be independent (a world generator that must not be
                       perturbed by anything else drawing).

      LoadFromTexture(file) - the buffer is loaded ONCE and SHARED by every instance that asks for
                       it, and `seed` becomes this instance's STARTING OFFSET into it. Cheap - one
                       copy of the noise in memory, and only one thing to upload or send - but the
                       streams overlap: read far enough and one instance walks into where another
                       started. Use it when the point is that everyone samples the same noise.

    Either way the instance owns its cursor, so instances never disturb each other's position.

    NOT THREAD SAFE, by design - it is a cursor into a buffer with no lock anywhere. Two threads
    drawing from one instance will interleave unpredictably, which is exactly what makes a run
    unreproducible. Give a thread its own instance, or keep all draws on one thread.
*/
class RRandom{
public:
	//The seed means different things depending on which of the two below is called - see above.
	RRandom(int seed = 1);
	~RRandom();

	//Shares one process-wide buffer loaded from `filename`, and starts this instance at an offset
	//derived from its seed. The first caller decides the file; a later caller asking for a
	//different one is warned and gets the already-loaded buffer.
	void LoadFromTexture(const std::string& filename);

	//Builds a private w*h buffer for this instance alone, filled deterministically from the seed.
	//Calling it again rebuilds that buffer and rewinds this instance to the start of it.
	void Generate(int w, int h);

	uint8_t Get_uint8();
	int GetInt();
	int GetInt(int min, int max);
	float GetFloat(float min, float max);
	float GetNormalFloat(float mean, float stdev);
	vec3 GetVec3(float min, float max);
	bool Roll(float chance); //Returns if you won with a chance of 0 ... 1

	//The read cursor: which byte of the buffer the next draw comes from. Public because it is the
	//whole of this generator's state - save it and the seed and a stream can be resumed exactly.
	uint32_t state = 0;
	//Spare for normal generation
	float spare = 0;
	bool hasspare = false;

	// Pick a random element from an array
	template <typename T, size_t N>
	T PickFromArray(const T (&array)[N]) {
		if (N == 0) return T();
		int index = GetInt(0, N - 1);
		return array[index];
	}
private:
	uint32_t seed = 0;

	//The buffer this instance reads. Either the shared one below, or a private one from
	//Generate - f_owns_texture says which, and so whether the destructor may delete it.
	Texture* rnd_texture = NULL;
	bool f_owns_texture = false;

	//The one buffer shared by every instance that went through LoadFromTexture. Deliberately
	//never freed: it outlives any individual generator and there is exactly one of it.
	static Texture* shared_texture;
	static std::string shared_texture_filename;

	//Places the cursor at seed % buffer size. Only meaningful for a shared buffer - with a private
	//one the seed already decided the contents, so the stream starts at 0.
	void SetStartOffset();
};

#endif
