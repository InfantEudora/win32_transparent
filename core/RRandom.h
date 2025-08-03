#ifndef _RRANDOM_H_
#define _RRANDOM_H_
#include <stdint.h>
#include "Texture.h"

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
	bool Roll(float chance); //Returns if you won with a chance of 0 ... 1

	uint32_t state;
	//Spare for normal generation
	float spare = 0;
	bool hasspare = false;


private:
	uint32_t seed;
	static Texture* rnd_texture;
};

#endif