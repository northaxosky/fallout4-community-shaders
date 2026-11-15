#include "FO4/TruePBR/Record.h"

// MIN blending makes the output of 1 the clamp; PBR pixels keep their range.
Texture2D<float4> Material : register(t0);

float4 main(float4 a_position : SV_POSITION) : SV_Target0
{
	if (TruePBRRecord::IsClassTag(Material.Load(int3(int2(a_position.xy), 0)).w))
		discard;
	return 1.0;
}
