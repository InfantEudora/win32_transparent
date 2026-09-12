#ifndef _RRANDOM_H_
#define _RRANDOM_H_
#include <stddef.h>
#include <stdint.h>
#include "type_vec3.h"

/*
    Reproducable random.

    Numbers come from a buffer of noise rather than from an algorithm evaluated per call. The
    buffer is a form the GPU can sample and the network can send: the same noise can drive a
    shader and the simulation, and a second machine can be handed the exact bytes rather than
    trusted to reproduce them.

    --- WHY THIS IS A PLAIN BUFFER AND NOT A Texture ---------------------------------------------
    It used to hold a `Texture*`, which made this header include `Texture.h` and so `glad.h`. That
    meant asking for a seeded integer pulled in the entire OpenGL loader - and the cost landed
    exactly where it hurt most: a RULES layer, the part of a game deliberately written with no
    engine types in it, could not use this class at all. Two apps (`tetris/Playfield.h` and
    `breakout/Field.h`) independently reimplemented the same xorshift32 that `Generate` below
    already uses, both citing this include. That is what the dependency actually cost.

    Nothing here ever needed a Texture. A Texture carries a GL id, a storage format, an upload
    path and a width and height; the noise needs bytes and a length. The 2D shape in particular
    carries NO INFORMATION - this is noise, so any w*h whose product is the buffer size is as
    correct as any other, which is why GetSquareSide() below can simply derive one instead of
    storing it.

    The two still meet, just the other way round: `Texture::LoadIntoRRandomNoiseFile` loads an
    image and hands its pixels to UseNoise below, so one decode feeds both the GPU and the
    simulation. Texture knows about RRandom; RRandom knows nothing about Texture, about files or
    about GL. That direction is the whole point - it is what lets a rules layer include this.

    --- HOW TO USE IT -------------------------------------------------------------------------

    RECOMMENDED: ONE INSTANCE PER GAME, created once during Init() and used everywhere.

        rrand = new RRandom(seed);      //Application::rrand already exists for this
        rrand->Generate(512,512);

    That is the shape the class is built for. A single stream is trivially reproducible: given the
    seed, every draw in the run follows. It is also what the deterministic-replay direction needs
    (see docs/engine_backlog.md) - though note that a single shared stream only replays if every
    draw happens on the simulation thread, in tick order. A draw from the UI or an MCP tool thread
    shifts the stream for the simulation and breaks that, whatever the seeding looks like. THIS IS
    WHY A RULES LAYER SHOULD OWN ITS OWN INSTANCE rather than borrow Application::rrand: its
    stream then depends on nothing but the tick.

    SEVERAL INSTANCES ARE SUPPORTED, and the two ways of filling a stream differ in what the seed
    means, because the two cases want different things:

      Generate(...)  - this instance gets its OWN buffer, filled from `seed`. Two instances with
                       different seeds hold genuinely different noise and share nothing. Use this
                       when the streams must be independent (a world generator that must not be
                       perturbed by anything else drawing, or a game's rules).

      UseNoise(...)  - the instance reads a buffer SOMEBODY ELSE owns (in practice a Texture's
                       pixels, see below), and `seed` becomes this instance's STARTING OFFSET into
                       it. Cheap - one copy of the noise in memory, and only one thing to upload
                       or send - but the streams overlap: read far enough and one instance walks
                       into where another started. Use it when the point is that everyone samples
                       the same noise.

    Either way the instance owns its cursor, so instances never disturb each other's position.

    --- THE BUFFER IS FINITE, AND THE STREAM WRAPS ----------------------------------------------
    Get_uint8 returns to the start of the buffer when it reaches the end, so a stream of N bytes
    REPEATS after N draws. Size the buffer for the number of draws the run will make, not for the
    number that feels tidy: GetInt spends four bytes, GetFloat one GetInt, and GetNormalFloat two
    GetFloats on the call that fills its spare. 512x512 is a quarter of a million bytes and no app
    in this repo has come close; a rules layer drawing a handful per tick wants far less.

    NOT THREAD SAFE, by design - it is a cursor into a buffer with no lock anywhere. Two threads
    drawing from one instance will interleave unpredictably, which is exactly what makes a run
    unreproducible. Give a thread its own instance, or keep all draws on one thread.
*/
class RRandom{
public:
	//The seed means different things depending on which filler is called - see above.
	RRandom(int seed = 1);
	~RRandom();

	/*
	    Draw from noise SOMEBODY ELSE owns, starting at an offset derived from this instance's
	    seed. The bytes are not copied and never freed here - the owner must outlive the
	    generator.

	    This is the other half of the rule at the top of this file. Loading an image is a Texture's
	    job and a Texture already does it, so the dependency runs THAT way:
	    `Texture::LoadIntoRRandomNoiseFile` loads the file once and hands the pixels here, which
	    means one image can feed the GPU and the simulation with a single decode and no duplicated
	    loading code. RRandom itself stays free of files, of stb_image and of GL.

	    Several instances pointed at one buffer share the noise and not the cursor, which is what
	    makes the seed an offset: everybody samples the same bytes from a different place. Read far
	    enough and one instance walks into where another started.
	*/
	void UseNoise(uint8_t* data, size_t size);

	/*
	    Changes this instance's seed, and makes the instance reflect that straight away:

	      - a PRIVATE buffer (from Generate) is refilled at the same size and the cursor rewinds
	        to the start, because a private buffer's CONTENTS are what the seed decided. This is
	        what a game restarting wants: the same seed must always deal the same stream.
	      - on a buffer from UseNoise the noise belongs to somebody else, so nothing is rewritten
	        and only this instance's start offset moves.
	      - with no buffer yet, the seed is recorded for the next Generate.

	    An earlier SetSeed was removed on 2026-09-11 because it wrote a field nothing read, so a
	    caller that asked for a seed got the same stream regardless. This one is the opposite: it
	    is the supported way to restart a stream, and it acts wherever there is something to act on.
	*/
	void SetSeed(uint32_t new_seed);

	//Builds a buffer of num_bytes belonging to THIS instance, filled deterministically from the
	//seed. Calling it again rebuilds that buffer and rewinds this instance to the start of it.
	void Generate(size_t num_bytes);
	//Same, taking the dimensions a caller may already be thinking in. The shape is not stored -
	//see the note at the top - so this is exactly Generate(w*h) and exists only so the existing
	//call sites keep reading the way they did.
	void Generate(int w, int h);

	uint8_t Get_uint8();
	int GetInt();
	int GetInt(int min, int max);
	float GetFloat(float min, float max);
	float GetNormalFloat(float mean, float stdev);
	vec3 GetVec3(float min, float max);
	bool Roll(float chance); //Returns if you won with a chance of 0 ... 1

	//--- For an app that wants this noise on the GPU or on the wire ----------------------------
	//The bytes themselves, and how many. NULL/0 before a Generate or UseNoise has run.
	const uint8_t* GetBuffer() const { return buffer; }
	size_t GetBufferSize() const { return buffer_sz; }
	//Side of a square that holds the whole buffer, for a caller uploading it as a 2D texture.
	//Derived rather than stored, because the shape of noise means nothing - see the top of this
	//file. Returns 0 for an empty buffer, and floor(sqrt(size)) otherwise, so side*side may be
	//SMALLER than the buffer when the size is not a perfect square: upload side*side bytes, or
	//generate a square number in the first place (512*512, 256*256 - what every caller does).
	int GetSquareSide() const;

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

	//The buffer this instance reads: a private one from Generate, or one handed to UseNoise that
	//belongs to somebody else. f_owns_buffer says which, and so whether the destructor may free it.
	uint8_t* buffer = NULL;
	size_t   buffer_sz = 0;
	bool     f_owns_buffer = false;
	//So the "you never filled this" error is logged once rather than on every draw.
	bool     f_warned_empty = false;

	//Frees the buffer if this instance owns it, and detaches either way. Every filler starts here.
	void ReleaseBuffer();
	//Places the cursor at seed % buffer size. Only meaningful for a buffer from UseNoise - with a
	//private one the seed already decided the contents, so the stream starts at 0.
	void SetStartOffset();
};

#endif
