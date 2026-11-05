// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef VSHADER
#	ifdef SCALE_BIAS
cbuffer ISCopyScaleBiasConstants : register(b2)
{
	float4 ScaleBias : packoffset(c2);
};
#	elif defined(MASK)
cbuffer ISCopyTextureMaskConstants : register(b2)
{
	float4 RotationMatrix : packoffset(c0);
};
#	endif

struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord: TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	vsout.Position = float4(input.Position.xyz, 1.0);
#	ifdef SCALE_BIAS
	vsout.TexCoord = input.TexCoord * ScaleBias.xy + ScaleBias.zw;
#	elif defined(MASK)
	vsout.TexCoord = mul(input.TexCoord - 0.5,
						 float2x2(RotationMatrix.xy, RotationMatrix.zw)) +
	                 0.5;
#	else
	vsout.TexCoord = input.TexCoord;
#	endif
	return vsout;
}
#endif

#ifdef PSHADER
struct PS_INPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD0;
};

#	ifdef DOWNSAMPLE_DEPTH
Texture2D<float4> Depth : register(t0);
SamplerState DepthSampler : register(s0);

float main(PS_INPUT input) : SV_Depth
{
	uint width, height;
	Depth.GetDimensions(width, height);
	float texelWidth = 1.0 / width;
	float texelHeight = 1.0 / height;
	float depth0 = Depth.Sample(DepthSampler,
							input.TexCoord + float2(-0.5 * texelWidth, -0.5 * texelHeight))
	                   .x;
	float depth1 = Depth.Sample(DepthSampler,
							input.TexCoord + float2(-0.5 * texelWidth, 0.5 * texelHeight))
	                   .x;
	float depth2 = Depth.Sample(DepthSampler,
							input.TexCoord + float2(0.5 * texelWidth, -0.5 * texelHeight))
	                   .x;
	float depth3 = Depth.Sample(DepthSampler,
							input.TexCoord + float2(0.5 * texelWidth, 0.5 * texelHeight))
	                   .x;
	return max(max(depth0, depth1), max(depth2, depth3));
}

#	elif defined(COPY_STENCIL)
Texture2D<uint4> Stencil : register(t1);

uint main(PS_INPUT input) : SV_Target0
{
	uint width, height;
	Stencil.GetDimensions(width, height);
	return Stencil.Load(int3(float2(width, height) * input.TexCoord, 0)).x;
}

#	elif defined(COPY_STENCIL_AND_WATER_MAT_ID)
Texture2D<uint4> Stencil : register(t1);

struct PS_OUTPUT
{
	uint Stencil: SV_Target0;
	float4 Material: SV_Target1;
};

static const float WaterMaterialOdd = 0.0078432;
static const float WaterMaterialEven = 0.0117648;

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	uint stencil = Stencil.Load(int3(input.TexCoord, 0)).x;
	clip((float)stencil - 1.0);
	psout.Stencil = stencil;
	psout.Material.w = (stencil & 1) ? WaterMaterialOdd : WaterMaterialEven;
	return psout;
}

#	elif defined(COPY_DEPTH)
Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);

float main(PS_INPUT input) : SV_Depth
{
	return Image.Sample(ImageSampler, input.TexCoord).x;
}

#	else

#		ifdef GREYSCALE
cbuffer ISCopyGreyScaleConstants : register(b2)
{
	float4 ColorSelect : packoffset(c1);
};
#		endif

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);

float4 main(PS_INPUT input) : SV_Target0
{
	float4 color = Image.Sample(ImageSampler, input.TexCoord);
#		ifdef VIS_ALPHA
	return float4(color.aaa, 1.0);
#		elif defined(MASK)
	return float4(color.rgb, 1.0 - color.r);
#		elif defined(GREYSCALE)
	float grey = dot(color, ColorSelect);
	return float4(grey.xxx, color.a);
#		else
	return color;
#		endif
}

#	endif
#endif
