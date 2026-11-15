#ifndef FO4_TRUE_PBR_RECORD_H
#define FO4_TRUE_PBR_RECORD_H

#ifdef __cplusplus
#	include <cstdint>
#endif

// One row per material record id (1-based; row 0 is never valid), bound at t68.
namespace TruePBRRecord
{
#ifdef __cplusplus
	using uint = std::uint32_t;
#endif

	static const uint Valid = 1;
	static const uint HasEmissive = 2;

	struct Row
	{
		float roughnessScale;
		float specularLevel;
		uint flags;
		uint reserved;
	};
#ifdef __cplusplus
	static_assert(sizeof(Row) == 16);
#endif
}
#endif
