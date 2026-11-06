// SPDX-License-Identifier: GPL-3.0-or-later

struct PS_INPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD;
};

cbuffer UpscalingData : register(b0)
{
	float2 TrueSamplingDim;
	float2 pad0;
};

Texture2D<float2> MotionVectors : register(t0);
Texture2D<float4> PipboyMask : register(t1);
Texture2D<float4> VatsMask : register(t2);

struct PS_OUTPUT
{
	float2 MotionVectors: SV_Target0;
	float4 PipboyMask: SV_Target1;
	float4 VatsMask: SV_Target2;
};

PS_OUTPUT main(PS_INPUT input)
{
	uint width;
	uint height;
	MotionVectors.GetDimensions(width, height);
	// Nearest keeps the Pip-Boy class ids, VATS coverage and velocities unblended.
	const uint2 source = min(
		uint2(input.Position.xy * (TrueSamplingDim / float2(width, height))),
		uint2(TrueSamplingDim) - 1);

	PS_OUTPUT psout;
	psout.MotionVectors = MotionVectors[source];
	psout.PipboyMask = PipboyMask[source];
	psout.VatsMask = VatsMask[source];
	return psout;
}
