#include "noise.h"
#include "mced.h"

static inline float hash2f(int32_t x, int32_t z, uint64_t seed) {
    uint64_t h = (uint64_t)(uint32_t)x * 0x9e3779b185ebca87ULL;
    h ^= (uint64_t)(uint32_t)z * 0xc2b2ae3d27d4eb4fULL;
    h ^= seed * 0x165667b19e3779f9ULL;
    h = mced_hash64(h);
    return (float)((h >> 40) & 0xffffff) / 8388607.5f - 1.0f;
}

static inline float hash3f(int32_t x, int32_t y, int32_t z, uint64_t seed) {
    uint64_t h = (uint64_t)(uint32_t)x * 0x9e3779b185ebca87ULL;
    h ^= (uint64_t)(uint32_t)y * 0xc2b2ae3d27d4eb4fULL;
    h ^= (uint64_t)(uint32_t)z * 0x165667b19e3779f9ULL;
    h ^= seed * 0x27d4eb2f165667c5ULL;
    h = mced_hash64(h);
    return (float)((h >> 40) & 0xffffff) / 8388607.5f - 1.0f;
}

float noise2(float x, float z, uint64_t seed) {
    int32_t x0 = (int32_t)floorf(x);
    int32_t z0 = (int32_t)floorf(z);
    float fx = x - (float)x0;
    float fz = z - (float)z0;
    float ux = smoothstepf(fx);
    float uz = smoothstepf(fz);

    float a = hash2f(x0, z0, seed);
    float b = hash2f(x0 + 1, z0, seed);
    float c = hash2f(x0, z0 + 1, seed);
    float d = hash2f(x0 + 1, z0 + 1, seed);

    return lerpf(lerpf(a, b, ux), lerpf(c, d, ux), uz);
}

float noise3(float x, float y, float z, uint64_t seed) {
    int32_t x0 = (int32_t)floorf(x);
    int32_t y0 = (int32_t)floorf(y);
    int32_t z0 = (int32_t)floorf(z);
    float fx = x - (float)x0;
    float fy = y - (float)y0;
    float fz = z - (float)z0;
    float ux = smoothstepf(fx);
    float uy = smoothstepf(fy);
    float uz = smoothstepf(fz);

    float c000 = hash3f(x0, y0, z0, seed);
    float c100 = hash3f(x0 + 1, y0, z0, seed);
    float c010 = hash3f(x0, y0 + 1, z0, seed);
    float c110 = hash3f(x0 + 1, y0 + 1, z0, seed);
    float c001 = hash3f(x0, y0, z0 + 1, seed);
    float c101 = hash3f(x0 + 1, y0, z0 + 1, seed);
    float c011 = hash3f(x0, y0 + 1, z0 + 1, seed);
    float c111 = hash3f(x0 + 1, y0 + 1, z0 + 1, seed);

    float x00 = lerpf(c000, c100, ux);
    float x10 = lerpf(c010, c110, ux);
    float x01 = lerpf(c001, c101, ux);
    float x11 = lerpf(c011, c111, ux);
    float y0v = lerpf(x00, x10, uy);
    float y1v = lerpf(x01, x11, uy);
    return lerpf(y0v, y1v, uz);
}

float fbm2(float x, float z, uint64_t seed, int octaves) {
    float sum = 0.0f;
    float amp = 0.5f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; i++) {
        sum += noise2(x, z, seed + (uint64_t)i * 0x9e3779b9ULL) * amp;
        norm += amp;
        x *= 2.01f;
        z *= 2.01f;
        amp *= 0.5f;
    }
    return sum / norm;
}

float fbm3(float x, float y, float z, uint64_t seed, int octaves) {
    float sum = 0.0f;
    float amp = 0.5f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; i++) {
        sum += noise3(x, y, z, seed + (uint64_t)i * 0x9e3779b9ULL) * amp;
        norm += amp;
        x *= 2.03f;
        y *= 2.03f;
        z *= 2.03f;
        amp *= 0.5f;
    }
    return sum / norm;
}

float ridged2(float x, float z, uint64_t seed, int octaves) {
    float sum = 0.0f;
    float amp = 0.5f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; i++) {
        float n = 1.0f - fabsf(noise2(x, z, seed + (uint64_t)i * 0x517cc1b7ULL));
        sum += n * n * amp;
        norm += amp;
        x *= 2.02f;
        z *= 2.02f;
        amp *= 0.5f;
    }
    return sum / norm;
}
