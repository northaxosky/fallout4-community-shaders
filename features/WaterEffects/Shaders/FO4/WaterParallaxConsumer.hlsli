#ifndef FO4_WATER_PARALLAX_CONSUMER
#define FO4_WATER_PARALLAX_CONSUMER

#include "Common/Random.hlsli"
#include "FO4/FO4ShaderData.hlsli"

// FO4: maps upstream water names onto the reconstructed PS.
#define TexCoord1 normalUv01
#define TexCoord2 normalUv2
#define WPosition eyeVector
#define HPosition screenPosition
#define Normals01Tex texture4
#define Normals01Sampler sampler4
#define Normals02Tex texture5
#define Normals02Sampler sampler5
#define Normals03Tex texture6
#define Normals03Sampler sampler6
#include "WaterEffects/WaterParallax.hlsli"
#undef TexCoord1
#undef TexCoord2
#undef WPosition
#undef HPosition
#undef Normals01Tex
#undef Normals01Sampler
#undef Normals02Tex
#undef Normals02Sampler
#undef Normals03Tex
#undef Normals03Sampler

#endif
