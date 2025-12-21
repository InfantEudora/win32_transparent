#ifndef _RRANDOM_H_
#define _RRANDOM_H_
#include <stdint.h>
#include "Texture.h"
#include "type_vec3.h"

//Reproducable random
class RRandom{
public:
	RRandom();

	void LoadFromTexture(const std::string& filename);
	void Generate(int w, int h);

	void SetSeed(uint32_t value);
	uint8_t Get_uint8();
	int GetInt();
	int GetInt(int min, int max);
	float GetFloat(float min, float max);
	float GetNormalFloat(float mean, float stdev);
	vec3 GetVec3(float min, float max);
	bool Roll(float chance); //Returns if you won with a chance of 0 ... 1

	uint32_t state;
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
	uint32_t seed;
	static Texture* rnd_texture;
};

#endif