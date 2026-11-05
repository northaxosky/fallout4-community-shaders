// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef VSHADER
cbuffer SSLRBlurConstants : register(b0)
{
	float4 ViewportSize;
};

struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord: TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord[5] : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	vsout.Position = float4(input.Position.xyz, 1.0);
#	ifdef HORIZONTAL
	float2 texel = float2(rcp(ViewportSize.x), 0.0);
#	elif defined(VERTICAL)
	float2 texel = float2(0.0, rcp(ViewportSize.y));
#	endif
	vsout.TexCoord[0] = input.TexCoord + texel * -3.294215;
	vsout.TexCoord[1] = input.TexCoord + texel * -1.407333;
	vsout.TexCoord[2] = input.TexCoord;
	vsout.TexCoord[3] = input.TexCoord + texel * 1.407333;
	vsout.TexCoord[4] = input.TexCoord + texel * 3.294215;
	return vsout;
}
#endif

#ifdef PSHADER
Texture2D<float4> SrcBuffer : register(t0);
SamplerState SrcBufferSampler : register(s0);

struct PS_INPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord[5] : TEXCOORD0;
};

static const int TapCount = 5;
static const float TapWeights[TapCount] = {
	0.093913,
	0.304005,
	0.204164,
	0.304005,
	0.093913,
};
static const float MinConfidence = 0.01;

float4 main(PS_INPUT input) : SV_Target0
{
	float4 taps[TapCount];
	float missingWeight = 0;
	float missingCount = 0;
	[unroll] for (int i = 0; i < TapCount; ++i)
	{
		taps[i] = SrcBuffer.SampleLevel(SrcBufferSampler, input.TexCoord[i], 0);
		if (taps[i].w < MinConfidence) {
			missingWeight += TapWeights[i];
			missingCount += 1;
		}
	}
	float extraWeight = missingWeight * rcp(TapCount - missingCount);

	float4 color = 0;
	[unroll] for (int j = 0; j < TapCount; ++j)
	{
		float4 weighted = taps[j] * TapWeights[j];
		if (taps[j].w >= MinConfidence) {
			color.rgb += weighted.rgb + extraWeight * taps[j].rgb;
		}
		color.a += weighted.a;
	}
	return color;
}
#endif
