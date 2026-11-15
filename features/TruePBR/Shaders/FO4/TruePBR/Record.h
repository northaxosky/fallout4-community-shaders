#ifndef FO4_TRUE_PBR_RECORD_H
#define FO4_TRUE_PBR_RECORD_H

#ifdef __cplusplus
#	include <cstdint>
#endif

// One row per 1-based record id; row 0 is never valid.
namespace TruePBRRecord
{
#ifdef __cplusplus
	using uint = std::uint32_t;
#endif

	static const uint Valid = 1;
	static const uint HasEmissive = 2;
	// MRT3 alpha of a TruePBR pixel, in 8-bit units.
	static const uint ClassTag = 128;

	struct Row
	{
		float roughnessScale;
		float specularLevel;
		uint flags;
		uint reserved;
	};
#ifdef __cplusplus
	static_assert(sizeof(Row) == 16);
#else
	bool IsClassTag(float a_unorm)
	{
		return abs(a_unorm * 255.0 - ClassTag) < 0.25;
	}
#endif
}
#endif
