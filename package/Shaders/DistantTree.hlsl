#if defined(BSDISTANTTREE_PS_SOURCE)
#	include "FO4/FO4ShaderData.hlsli"
#	ifndef RENDER_DEPTH
#		include "FO4/FogConsumer.hlsli"
#		include "FO4/ImageBasedLightingConsumer.hlsli"
#	endif
#	if defined(TERRAIN_SHADOWS) && !defined(RENDER_DEPTH)
#		include "FO4/TerrainShadowsConsumer.hlsli"
#	endif
// SPDX-License-Identifier: GPL-3.0-or-later
// AE 1.11.240, shaders011.fxp ordinals 6-7.

struct PS_INPUT
{
	float4 HPosition: SV_POSITION0;
	float3 TexCoord: TEXCOORD0;
	float4 FogParam: TEXCOORD1;
#	ifdef RENDER_DEPTH
	float2 Depth: TEXCOORD3;
#	endif
};

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#	ifndef RENDER_DEPTH
SamplerState SampDiffuse : register(s0);
Texture2D<float4> TexDiffuse : register(t0);

cbuffer PerTechnique : register(b0)
{
	float4 Reserved[4];
	float3 DiffuseColor;
	float4 AmbientColor;
}
#	endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#	ifdef RENDER_DEPTH
	float depth = input.Depth.x / input.Depth.y;
	psout.Color = float4(depth, depth, depth, 1);
#	else
	float4 baseColor = SAMPLE_MATERIAL(TexDiffuse, SampDiffuse, input.TexCoord.xy);
	float3 diffuseColor =
#		ifdef TERRAIN_SHADOWS
		baseColor.xyz * (input.TexCoord.z * DiffuseColor *
								TerrainShadows::GetShadowFromScreenPosition(input.HPosition.xyz) +
							FO4_IBL_FORWARD_AMBIENT(AmbientColor.xyz, input.HPosition.xyz));
#		else
		baseColor.xyz * (input.TexCoord.z * DiffuseColor + FO4_IBL_FORWARD_AMBIENT(AmbientColor.xyz, input.HPosition.xyz));
#		endif
	float3 color = lerp(diffuseColor, FO4_FOG_FORWARD_COLOR(input.FogParam.xyz, input.HPosition.xyz), input.FogParam.w);
	psout.Color.xyz = color * AmbientColor.w;
	psout.Color.w = 1;
#	endif

	return psout;
}
#elif defined(BSDISTANTTREE_VS_SOURCE)
// SPDX-License-Identifier: GPL-3.0-or-later
// AE 1.11.240, shaders011.fxp ordinals 4-5.

struct VS_INPUT
{
	float3 Position: POSITION0;
	float2 TexCoord: TEXCOORD0;
	float4 InstanceData1: TEXCOORD4;
	float4 InstanceData2: TEXCOORD5;
	float4 InstanceData3: TEXCOORD6;
	float4 InstanceData4: TEXCOORD7;
};

struct VS_OUTPUT
{
	float4 HPosition: SV_POSITION0;
	float3 TexCoord: TEXCOORD0;
	float4 FogParam: TEXCOORD1;
#	ifdef RENDER_DEPTH
	float2 Depth: TEXCOORD3;
#	endif
};

cbuffer PerFrame : register(b12)
{
	row_major float4x4 WorldToView;
	row_major float4x4 ViewToClip;
	row_major float4x4 WorldToClip;
	row_major float4x4 ViewToWorld;
	row_major float4x4 ClipToWorld;
	row_major float4x4 ClipToView;
	row_major float4x4 FirstPersonClipToView;
	float4 HairSpecularScaleAndPower;
	float4 HairSpecularShift;
	float4 WorldWetnessAndRainTransition;
	row_major float4x4 PreviousWorldToClip;
	float4 PosAdjust;
	float4 PreviousPosAdjust;
	row_major float4x4 UnjitteredWorldToClip;
	float4 FogDistanceAndLowHeightRamps;
	float4 FogColor0;
	float4 FogColor1;
	float4 FogColor2;
	float4 FogColor3;
	float4 FogHeightRamps;
}

cbuffer PerTechnique : register(b0)
{
	float3 DiffuseDir;
}

cbuffer PerGeometry : register(b2)
{
	row_major float4x4 WorldViewProj;
}

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float3 scaledModelPosition = input.Position.xyz * input.InstanceData1.w;

	float3 adjustedModelPosition;
	adjustedModelPosition.x =
		dot(input.InstanceData2.xy * float2(1, -1), scaledModelPosition.xy);
	adjustedModelPosition.y =
		dot(input.InstanceData2.yx, scaledModelPosition.xy);
	adjustedModelPosition.z = scaledModelPosition.z;

	float4 msPosition =
		float4(adjustedModelPosition + input.InstanceData1.xyz, 1);
	float4 projSpacePosition = mul(WorldViewProj, msPosition);
	vsout.HPosition = projSpacePosition;

#	ifdef RENDER_DEPTH
	vsout.Depth = projSpacePosition.zw;
#	endif

	vsout.TexCoord = float3(input.TexCoord, DiffuseDir.z);

	float4 fogPlanePosition = float4(projSpacePosition.xyz, 1);
	float fogPlane = dot(ViewToWorld[2], fogPlanePosition) + PosAdjust.z;
	float2 fogRamp = saturate(fogPlane * FogHeightRamps.xy - FogHeightRamps.zw);

	float depth =
		length(projSpacePosition.xyz) * FogDistanceAndLowHeightRamps.x - FogDistanceAndLowHeightRamps.z;
	float saturatedDepth = saturate(depth);
	float fogAmount = lerp(fogRamp.x, fogRamp.y, saturatedDepth);
	float fogDensity = (1 - fogAmount) + fogAmount * FogColor2.w;

	float fogFade = FogColor1.w;
	if (depth > 0.75) {
		fogFade =
			min(lerp(FogColor1.w, 1, (saturatedDepth - 0.75) / 0.25), 1);
	}

	float alphaScale = min(pow(saturatedDepth, FogColor0.w), fogFade);
	float nearScale = 1;
	if (depth < 0.015) {
		nearScale = saturatedDepth / 0.015;
	}

	float3 fogColorNear = lerp(FogColor0.xyz, FogColor2.xyz, alphaScale);
	float3 fogColorFar = lerp(FogColor1.xyz, FogColor3.xyz, alphaScale);
	vsout.FogParam.xyz =
		lerp(fogColorNear, fogColorFar, fogAmount);
	vsout.FogParam.w = (alphaScale * fogDensity) * nearScale;

	return vsout;
}
#else
#	error "Select one shader family stage or kernel."
#endif
