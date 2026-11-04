#ifndef MCED_NOISE_H
#define MCED_NOISE_H

#include <stdint.h>

float noise2(float x, float z, uint64_t seed);
float noise3(float x, float y, float z, uint64_t seed);
float fbm2(float x, float z, uint64_t seed, int octaves);
float fbm3(float x, float y, float z, uint64_t seed, int octaves);
float ridged2(float x, float z, uint64_t seed, int octaves);

#endif
