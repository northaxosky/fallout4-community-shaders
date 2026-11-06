// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#if defined(FO4CS_SUBSTRATE) || defined(FO4CS_VS_TEXCOORD1) || (defined(OG) && defined(FO4CS_VS_GEOMETRY_OFFSET))
#	ifdef FO4CS_VS_TEXCOORD1
struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD0;
	float4 TexCoord1: TEXCOORD1;
};
#	else
struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD0;
};
#	endif

#	ifdef VSHADER
#		ifdef FO4CS_SUBSTRATE
#			include "Common/FrameBuffer.hlsli"
#		endif

// FO4: OG calls the base VS constant GeometryOffset; AE/NG ResolutionParams.
#		if defined(OG) && defined(FO4CS_VS_TEXCOORD1)
cbuffer FXAAVertexConstants : register(b2)
{
	float2 PositionShift : packoffset(c0);
	float2 InverseTargetSize : packoffset(c0.z);
};
#		elif defined(OG) && defined(FO4CS_VS_GEOMETRY_OFFSET)
cbuffer PerGeometry : register(b2)
{
	float4 GeometryOffset;
};
#		endif

struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord: TEXCOORD0;
};

#		ifdef FO4CS_SUBSTRATE
// FO4: the engine bakes the render scale into an fp16 UV buffer.
float2 RestoreExactScale(float2 texCoord)
{
	// The fullscreen triangle's far vertices hold twice the scale.
	float2 exactTexCoord = 2.0 * FrameBuffer::DynamicResolutionParams1.xy;
	// Within one fp16 ulp: the engine's float-to-half rounding mode is unknown.
	bool2 isScaled = abs(texCoord - exactTexCoord) <= exactTexCoord * (1.0 / 1024.0);
	return isScaled ? exactTexCoord : texCoord;
}
#		endif

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
#		if defined(OG) && defined(FO4CS_VS_TEXCOORD1)
	vsout.Position = float4(input.Position.xy - PositionShift * float2(2.0, -2.0), input.Position.z, 1.0);
#		elif defined(OG) && defined(FO4CS_VS_GEOMETRY_OFFSET)
	vsout.Position.xy = input.Position.xy - GeometryOffset.xy * float2(2.0, -2.0);
	vsout.Position.zw = float2(input.Position.z, 1.0);
#		else
	vsout.Position = float4(input.Position.xyz, 1.0);
#		endif
#		ifdef FO4CS_SUBSTRATE
	vsout.TexCoord = RestoreExactScale(input.TexCoord);
#		else
	vsout.TexCoord = input.TexCoord;
#		endif
#		if defined(OG) && defined(FO4CS_VS_TEXCOORD1)
	vsout.TexCoord1 = float4(input.TexCoord - InverseTargetSize, input.TexCoord + InverseTargetSize);
#		endif
	return vsout;
}
#	endif
#else
#	include "Common/DummyVSTexCoord.hlsl"
#endif
