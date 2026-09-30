#ifndef __RANDOM_DEPENDENCY_HLSL__
#define __RANDOM_DEPENDENCY_HLSL__

namespace Random
{
	uint rotl(uint x, uint r)
	{
		return (x << r) | (x >> (32u - r));
	}

	uint fmix(uint h)
	{
		h ^= h >> 16;
		h *= 2246822507u;  // 0x85ebca6b
		h ^= h >> 13;
		h *= 3266489917u;  // 0xc2b2ae35
		h ^= h >> 16;
		return h;
	}

	uint murmur3(uint3 x, uint seed = 0)
	{
		static const uint c1 = 3432918353u;  // 0xcc9e2d51
		static const uint c2 = 461845907u;   // 0x1b873593

		uint h = seed;
		uint k = x.x;

		k *= c1;
		k = rotl(k, 15u);
		k *= c2;

		h ^= k;
		h = rotl(h, 13u);
		h = h * 5u + 3864292196u;  // 0xe6546b64

		k = x.y;

		k *= c1;
		k = rotl(k, 15u);
		k *= c2;

		h ^= k;
		h = rotl(h, 13u);
		h = h * 5u + 3864292196u;  // 0xe6546b64

		k = x.z;

		k *= c1;
		k = rotl(k, 15u);
		k *= c2;

		h ^= k;
		h = rotl(h, 13u);
		h = h * 5u + 3864292196u;  // 0xe6546b64

		h ^= 12u;

		return fmix(h);
	}

	float3 perlinGradient(uint hash)
	{
		switch (hash & 15u) {  // look at the last four bits to pick a gradient direction
		case 0:
			return float3(1, 1, 0);
		case 1:
			return float3(-1, 1, 0);
		case 2:
			return float3(1, -1, 0);
		case 3:
			return float3(-1, -1, 0);
		case 4:
			return float3(1, 0, 1);
		case 5:
			return float3(-1, 0, 1);
		case 6:
			return float3(1, 0, -1);
		case 7:
			return float3(-1, 0, -1);
		case 8:
			return float3(0, 1, 1);
		case 9:
			return float3(0, -1, 1);
		case 10:
			return float3(0, 1, -1);
		case 11:
			return float3(0, -1, -1);
		case 12:
			return float3(1, 1, 0);
		case 13:
			return float3(-1, 1, 0);
		case 14:
			return float3(0, -1, 1);
		case 15:
			return float3(0, -1, -1);
		default:
			return float3(0, 0, 0);  // Should never reach here
		}
	}

	// range: -1 to 1
	float perlinNoise(float3 position, uint seed = 0x578437ADu)
	{
		float3 i_f = floor(position);
		float3 f = position - i_f;
		uint3 i = asuint(int3(i_f));
		float value1 = dot(perlinGradient(murmur3(i, seed)), f);
		float value2 = dot(perlinGradient(murmur3((i + uint3(1, 0, 0)), seed)), f - float3(1, 0, 0));
		float value3 = dot(perlinGradient(murmur3((i + uint3(0, 1, 0)), seed)), f - float3(0, 1, 0));
		float value4 = dot(perlinGradient(murmur3((i + uint3(1, 1, 0)), seed)), f - float3(1, 1, 0));
		float value5 = dot(perlinGradient(murmur3((i + uint3(0, 0, 1)), seed)), f - float3(0, 0, 1));
		float value6 = dot(perlinGradient(murmur3((i + uint3(1, 0, 1)), seed)), f - float3(1, 0, 1));
		float value7 = dot(perlinGradient(murmur3((i + uint3(0, 1, 1)), seed)), f - float3(0, 1, 1));
		float value8 = dot(perlinGradient(murmur3((i + uint3(1, 1, 1)), seed)), f - float3(1, 1, 1));

		float3 u = f * f * (3.0 - 2.0 * f);  // wetness specific
		return lerp(
			lerp(lerp(value1, value2, u.x), lerp(value3, value4, f.x), u.y),
			lerp(lerp(value5, value6, u.x), lerp(value7, value8, f.x), u.y),
			u.z);
	}
}

#endif  // __RANDOM_DEPENDENCY_HLSL__
