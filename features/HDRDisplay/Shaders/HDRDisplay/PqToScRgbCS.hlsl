// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
// Converts the signal (PQ or gamma 2.2) to linear scRGB, 1.0 = 80 nits.

#include "Common/Color.hlsli"

Texture2D<float4> Signal : register(t0);
RWTexture2D<float4> Output : register(u0);

cbuffer PerFrame : register(b0)
{
	float enableHDR : packoffset(c0.x);
	float paperWhite : packoffset(c0.y);
	float windowsHdr : packoffset(c2.z);
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint width, height;
	Output.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;

	float3 signal = Signal[dispatchID.xy].rgb;
	float3 linearColor;
	if (enableHDR > 0.5) {
		// The signal encodes linear BT.2020 with 1.0 at paper white.
		linearColor = Color::BT2020ToBT709(Color::pq::Decode(saturate(signal), paperWhite));
	} else {
		linearColor = TransferFunctions::Gamma22ToLinear(saturate(signal));
	}
	float white = windowsHdr > 0.5 ? paperWhite / 80.0 : 1.0;
	Output[dispatchID.xy] = float4(linearColor * white, 1.0);
}
