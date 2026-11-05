// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef CSHADER
#	if defined(GRIDSIZE_X)
#	define GROUP_SIZE (GRIDSIZE_X + TEXTAP - 1)
#	define AXIS x
#	else
#	define GROUP_SIZE (GRIDSIZE_Y + TEXTAP - 1)
#	define AXIS y
#	endif

cbuffer ISBlurCSConstants : register(b2)
{
	float4 ScreenInfos;
	float4 BlurBrightPass;
	float4 BlurScale;
#	if !defined(HDR)
	float4 BlurOffsets[TEXTAP];
#	endif
};

Texture2D<float4> Image : register(t0);
#	if defined(BRIGHTPASS)
Texture2D<float4> AvgLum : register(t1);
#	endif
RWTexture2D<float4> Output : register(u0);

#	if defined(HDR)
// The engine's strength-7 kernel, normalised exp(-2 t^2 / 49), to five decimals.
static const float BlurWeights[TEXTAP] = {
	0.01593, 0.02708, 0.04242, 0.06125, 0.08151, 0.09997, 0.11299, 0.1177,
	0.11299, 0.09997, 0.08151, 0.06125, 0.04242, 0.02708, 0.01593,
};
#	define BLUR_WEIGHT(i) BlurWeights[i]
#	else
#	define BLUR_WEIGHT(i) BlurOffsets[i].z
#	endif

groupshared float3 Run[GROUP_SIZE];

#	if defined(GRIDSIZE_X)
[numthreads(GROUP_SIZE, 1, 1)]
#	else
[numthreads(1, GROUP_SIZE, 1)]
#	endif
void main(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID)
{
	// The run's base is formed before the thread's offset, as native schedules it.
	uint runStart = groupId.AXIS * GROUP_SIZE;
	int offset = int(threadId.AXIS) - TEXTAP / 2;
#	if defined(GRIDSIZE_X)
	uint2 texel = uint2(runStart + offset, groupId.y);
#	else
	uint2 texel = uint2(groupId.x, runStart + offset);
#	endif
	float4 color = Image.Load(int3(texel, 0));
#	if defined(BRIGHTPASS)
	color.xyz = max(color.xyz - BlurBrightPass.x, 0) * BlurBrightPass.y;
#	endif
	Run[threadId.AXIS] = color.xyz;
	GroupMemoryBarrierWithGroupSync();

	if (offset >= 0 && texel.AXIS < ScreenInfos.AXIS)
	{
#	if defined(BRIGHTPASS)
		color.w = dot(AvgLum.Load(int3(groupId.xy, 0)).xyz,
			float3(0.2125, 0.7154, 0.0721));
#	endif
		float3 sum = 0;
		[unroll]
		for (int tap = -(TEXTAP / 2); tap < TEXTAP / 2; ++tap)
		{
			// The centre tap is this thread's own texel.
			float3 tapColor = tap == 0 ? color.xyz : Run[threadId.AXIS + tap];
			sum += BLUR_WEIGHT(tap + TEXTAP / 2) * tapColor;
		}
		Output[texel] = float4(saturate(sum), color.w);
	}
}
#endif
