// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

// Converts the HDR proxy (gamma 2.2, 1.0 = Scale.x * 80 nits) to and from scRGB (1.0 = 80 nits).
Texture2D<float4> Source : register(t0);

cbuffer Convert : register(b0)
{
	float4 Scale;
};

float4 main(float4 position : SV_POSITION) : SV_Target0
{
	float4 color = Source.Load(int3(position.xy, 0));
#ifdef TO_SCRGB
	color.rgb = sign(color.rgb) * pow(abs(color.rgb), 2.2) * Scale.x;
#else
	color.rgb = sign(color.rgb) * pow(abs(color.rgb / Scale.x), 1.0 / 2.2);
#endif
	return color;
}
