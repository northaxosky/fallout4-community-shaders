// SSGI v2: normal-pyramid build.
// 5-mip encoded-normal chain. Adapted from upstream Skyrim CS @ bb6460db
// `features/Screen Space GI/Shaders/ScreenSpaceGI/prefilterNormal.cs.hlsl`.
//
// FO4-specific: the engine's kGbufferNormal is R10G10B10A2 with an
// already-encoded 2-component view-space normal in the .rg channels
// (matches the v1 path in features/ScreenSpaceGI/Shaders/AOCS.hlsl).
// Decode -> filter -> re-encode to unorm float2 for the pyramid mips.

#include "Common.hlsli"

Texture2D<float4> srcNormalRoughness : register(t0);

RWTexture2D<unorm float2> outNormal0 : register(u0);
RWTexture2D<unorm float2> outNormal1 : register(u1);
RWTexture2D<unorm float2> outNormal2 : register(u2);
RWTexture2D<unorm float2> outNormal3 : register(u3);
RWTexture2D<unorm float2> outNormal4 : register(u4);

float2 EncodeFO4Normal(float2 nxy)
{
	// FO4 stores normal.xy unscaled in [-1,1] within the R10G10 channels of the gbuffer
	// (see DecodeViewNormal in features/ScreenSpaceGI/Shaders/Common.hlsli). v1 SSGI samples
	// the raw .rg float2 and unpacks via `n*2-1`. Recover the half-range octahedral form for
	// the SH2 pipeline by remapping into [0,1].
	float3 n = float3(nxy * 2.0 - 1.0, 0.0);
	n.z = sqrt(saturate(1.0 - dot(n.xy, n.xy)));
	return GBuffer::EncodeNormal(normalize(n));
}

float2 NormalMIPFilter(float2 enc0, float2 enc1, float2 enc2, float2 enc3)
{
	float3 avg = GBuffer::DecodeNormal(enc0) + GBuffer::DecodeNormal(enc1) + GBuffer::DecodeNormal(enc2) + GBuffer::DecodeNormal(enc3);
	return GBuffer::EncodeNormal(normalize(avg));
}

groupshared float2 g_scratchNormal[8][8];

[numthreads(8, 8, 1)]
void main(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID)
{
	const float2 frameScale = FrameDim * RcpTexDim;

	// MIP 0: gather a 2x2 source quad per thread, write 4 encoded normals.
	// RCP_OUT_FRAME_DIM (not RcpFrameDim): dispatch covers work-res in HALF/QUARTER, so the rcp must scale to OUT_FRAME_DIM.
	const uint2 baseCoord = dispatchThreadID;
	const uint2 pixCoord  = baseCoord * 2;
	const float2 uv       = (pixCoord + .5) * RCP_OUT_FRAME_DIM;

	float4 nr0 = srcNormalRoughness.GatherRed(samplerPointClamp,   uv * frameScale);
	float4 nr1 = srcNormalRoughness.GatherGreen(samplerPointClamp, uv * frameScale);

	float2 normal0 = EncodeFO4Normal(float2(nr0.w, nr1.w));
	float2 normal1 = EncodeFO4Normal(float2(nr0.z, nr1.z));
	float2 normal2 = EncodeFO4Normal(float2(nr0.x, nr1.x));
	float2 normal3 = EncodeFO4Normal(float2(nr0.y, nr1.y));

	outNormal0[pixCoord + uint2(0, 0)] = normal0;
	outNormal0[pixCoord + uint2(1, 0)] = normal1;
	outNormal0[pixCoord + uint2(0, 1)] = normal2;
	outNormal0[pixCoord + uint2(1, 1)] = normal3;

	// MIP 1
	float2 nm1 = NormalMIPFilter(normal0, normal1, normal2, normal3);
	outNormal1[baseCoord] = nm1;
	g_scratchNormal[groupThreadID.x][groupThreadID.y] = nm1;

	GroupMemoryBarrierWithGroupSync();

	// MIP 2
	[branch] if (all((groupThreadID.xy % 2) == 0))
	{
		float2 inTL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 0];
		float2 inTR = g_scratchNormal[groupThreadID.x + 1][groupThreadID.y + 0];
		float2 inBL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 1];
		float2 inBR = g_scratchNormal[groupThreadID.x + 1][groupThreadID.y + 1];

		float2 nm2 = NormalMIPFilter(inTL, inTR, inBL, inBR);
		outNormal2[baseCoord / 2] = nm2;
		g_scratchNormal[groupThreadID.x][groupThreadID.y] = nm2;
	}

	GroupMemoryBarrierWithGroupSync();

	// MIP 3
	[branch] if (all((groupThreadID.xy % 4) == 0))
	{
		float2 inTL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 0];
		float2 inTR = g_scratchNormal[groupThreadID.x + 2][groupThreadID.y + 0];
		float2 inBL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 2];
		float2 inBR = g_scratchNormal[groupThreadID.x + 2][groupThreadID.y + 2];

		float2 nm3 = NormalMIPFilter(inTL, inTR, inBL, inBR);
		outNormal3[baseCoord / 4] = nm3;
		g_scratchNormal[groupThreadID.x][groupThreadID.y] = nm3;
	}

	GroupMemoryBarrierWithGroupSync();

	// MIP 4
	[branch] if (all((groupThreadID.xy % 8) == 0))
	{
		float2 inTL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 0];
		float2 inTR = g_scratchNormal[groupThreadID.x + 4][groupThreadID.y + 0];
		float2 inBL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 4];
		float2 inBR = g_scratchNormal[groupThreadID.x + 4][groupThreadID.y + 4];

		float2 nm4 = NormalMIPFilter(inTL, inTR, inBL, inBR);
		outNormal4[baseCoord / 8] = nm4;
	}
}
