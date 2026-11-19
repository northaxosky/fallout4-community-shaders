// SPDX-License-Identifier: GPL-3.0-or-later

cbuffer UpscalingData : register(b0)
{
	float2 TrueSamplingDim;
	float2 pad0;
};

Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Output : register(u0);

// Bit test: fast-math folds isfinite away.
bool IsFinite(float4 a_value)
{
	return all((asuint(a_value) & 0x7F800000) != 0x7F800000);
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= uint2(TrueSamplingDim)))
		return;

	// A NaN reaching the provider survives in its history.
	const float4 color = Source[dispatchID.xy];
	if (IsFinite(color)) {
		Output[dispatchID.xy] = color;
		return;
	}

	// Zero would upscale as a black box; borrow the nearest finite ring instead.
	const int2 maxCoord = int2(TrueSamplingDim) - 1;
	float4 sum = 0.0;
	float count = 0.0;
	[loop] for (int radius = 1; radius <= 16 && count == 0.0; radius *= 2)
	{
		[unroll] for (int tap = 0; tap < 4; ++tap)
		{
			const int2 offset = int2((tap & 1) ? 0 : (tap == 0 ? -radius : radius), (tap & 1) ? (tap == 1 ? -radius : radius) : 0);
			const float4 neighbor = Source[clamp(int2(dispatchID.xy) + offset, 0, maxCoord)];
			if (IsFinite(neighbor)) {
				sum += neighbor;
				count += 1.0;
			}
		}
	}
	Output[dispatchID.xy] = count > 0.0 ? sum / count : 0.0;
}
