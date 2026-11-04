// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef CSHADER
#	ifdef DOWNADAPT
// Rows the stock layout keeps but the code never reads.
cbuffer ISHDRCSConstants : register(b2)
{
	float4 UnmappedRow0;
	float4 Param;
	float4 UnmappedRows2To3[2];
	float4 ScreenInfos;
};
#	else
cbuffer ISHDRCSConstants : register(b2)
{
	float4 UnmappedRows[4];
	float4 ScreenInfos;
};
#	endif

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);
#	ifdef DOWNADAPT
Texture2D<float4> Adapt : register(t1);
#	endif
RWTexture2D<float4> Output : register(u0);

#	define GroupWidth (DOWNSAMPLE / 2)

#	if DOWNSAMPLE == 2
[numthreads(1, 1, 1)] void main(uint3 dispatchId : SV_DispatchThreadID, uint3 groupId : SV_GroupID) {
	float2 uv = dispatchId.xy * ScreenInfos.zw + ScreenInfos.zw * 0.5;
	float4 sample = Image.SampleLevel(ImageSampler, uv, 0.0);
	float adapted = Adapt.Load(int3(groupId.xy, 0)).x;
	float blended = (sample.x - adapted) * Param.z + adapted;
	Output[dispatchId.xy] = float4(blended, blended, blended, sample.w);
}
#	else
groupshared float3 GroupValues[GroupWidth * GroupWidth];

[numthreads(GroupWidth, GroupWidth, 1)] void main(uint3 dispatchId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
	float2 uv = dispatchId.xy * ScreenInfos.zw + ScreenInfos.zw * 0.5;
	uint quadBits = dispatchId.x | dispatchId.y;
	float4 sample = Image.SampleLevel(ImageSampler, uv, 0.0);
#		ifdef RGB2LUM
	float3 own = dot(float3(0.2125, 0.7154, 0.0721), sample.xyz);
#		else
	float3 own = sample.x;
#		endif
	GroupValues[groupIndex] = own;
#		if DOWNSAMPLE == 4
	GroupMemoryBarrierWithGroupSync();
	if ((quadBits & 1) == 0) {
		float3 sum = own + GroupValues[groupIndex + 1] +
		             GroupValues[groupIndex + 2] + GroupValues[3];
		float3 average = sum * 0.25;
		Output[dispatchId.xy >> 1] = float4(average, sample.w);
	}
#		else
	uint remaining = GroupWidth;
	uint level = 0;
	while (remaining != 1) {
		GroupMemoryBarrierWithGroupSync();
		remaining >>= 1;
		uint nextLevel = level + 1;
		if ((quadBits & ((1u << nextLevel) - 1u)) == 0) {
			uint stepX = 1u << level;
			uint stepY = GroupWidth << level;
			uint stepXY = stepY + stepX;
			float3 sum = own + GroupValues[groupIndex + stepX] +
			             GroupValues[groupIndex + stepY] +
			             GroupValues[groupIndex + stepXY];
			own = sum * 0.25;
			GroupValues[groupIndex] = own;
			if (remaining == 1) {
				Output[dispatchId.xy >> nextLevel] = float4(own, sample.w);
			}
		}
		level = nextLevel;
	}
#		endif
}
#	endif
#endif
