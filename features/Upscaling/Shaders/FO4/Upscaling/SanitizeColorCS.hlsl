// SPDX-License-Identifier: GPL-3.0-or-later

cbuffer UpscalingData : register(b0)
{
	float2 TrueSamplingDim;
	float2 pad0;
};

Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Output : register(u0);

// Bit test: fast-math folds isfinite away.
float Finite(float a_value)
{
	return (asuint(a_value) & 0x7F800000) == 0x7F800000 ? 0.0 : a_value;
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= uint2(TrueSamplingDim)))
		return;

	// A NaN reaching the provider survives in its history.
	const float4 color = Source[dispatchID.xy];
	Output[dispatchID.xy] = float4(Finite(color.x), Finite(color.y), Finite(color.z), Finite(color.w));
}
