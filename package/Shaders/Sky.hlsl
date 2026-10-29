// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
#ifdef BSSKY_VERTEX_SHADER
// Empty-string selectors are presence-only.
#	ifdef TEX
#		define HAS_TEXCOORD
#	endif
#	ifdef HORIZFADE
#		ifndef HAS_TEXCOORD
#			define HAS_TEXCOORD
#		endif
#	endif

struct VS_INPUT
{
	float4 Position: POSITION0;
#	ifdef HAS_TEXCOORD
	float2 TexCoord: TEXCOORD0;
#	endif
	float4 Color: COLOR0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
#	ifdef HAS_TEXCOORD
	float2 TexCoord: TEXCOORD0;
#	endif
#	ifdef TEXLERP
	float2 TexCoord1: TEXCOORD1;
#	endif
#	ifdef HORIZFADE
	float TexCoord2: TEXCOORD2;
#	endif
#	ifndef OCCLUSION
	float4 Color: COLOR0;
	float4 WorldPosition: POSITION0;
	float4 PreviousWorldPosition: POSITION1;
#	endif
};

cbuffer PerGeometry : register(b2)
{
	row_major float4x4 WorldViewProj;
#	ifndef OCCLUSION
	row_major float3x4 World;
#		ifdef HORIZFADE
	float4 EyePosition;
#		endif
#		ifdef CLOUDS
	float4 BlendColor[3];
	float2 TexCoordOff;
#		elif defined(MOONMASK)
	float4 World_row3[3];
	float2 World_row6;
#		else
	float4 BlendColor[3];
	float2 BlendColor_row3;
#		endif
	row_major float3x4 PreviousWorld;
#	endif
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float4 inputPosition = float4(input.Position.xyz, 1.0);
	float4 viewPosition = mul(WorldViewProj, inputPosition);
	vsout.Position.xy = viewPosition.xy;
	vsout.Position.zw = viewPosition.ww;

#	ifdef HAS_TEXCOORD
#		ifdef CLOUDS
	vsout.TexCoord = input.TexCoord + TexCoordOff;
#		else
	vsout.TexCoord = input.TexCoord;
#		endif
#	endif

#	ifdef TEXLERP
	vsout.TexCoord1 = input.TexCoord + TexCoordOff;
#	endif

#	ifndef OCCLUSION
	float3 worldPosition = mul(World, inputPosition);

#		ifdef HORIZFADE
	vsout.TexCoord2 = saturate((worldPosition.z - EyePosition.z) / 17.0);
#		endif

#		ifdef MOONMASK
	vsout.Color = 1.0;
#		else
#			ifdef HORIZFADE
	vsout.Color = BlendColor[0];
#			else
	float4 skyColor = input.Color.xxxw * BlendColor[0];
	skyColor.xyz += BlendColor[1].xyz * input.Color.y;
	skyColor.xyz += BlendColor[2].xyz * input.Color.z;
	vsout.Color = BlendColor[0] * 0.000001 + skyColor;
#			endif
#		endif

	vsout.WorldPosition = float4(worldPosition, 1.0);
	vsout.PreviousWorldPosition = float4(mul(PreviousWorld, inputPosition), 1.0);
#	endif

	return vsout;
}
#endif

#ifdef BSSKY_PIXEL_SHADER
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
};

cbuffer PerTechnique : register(b2)
{
	float4 PParams[1];  // Sky technique parameters.
};

SamplerState sampler0 : register(s0);
SamplerState sampler1 : register(s1);
SamplerState sampler2 : register(s2);

Texture2D<float4> texture0 : register(t0);
Texture2D<float4> texture1 : register(t1);
Texture2D<float4> texture2 : register(t2);

struct PS_OUTPUT
{
	float4 color: SV_Target0;
	float4 motion: SV_Target1;
#	if defined(CLOUD_SHADOWS) && defined(CLOUDS)
	// FO4: RT0 color, RT1 motion; upstream sits after its normal target.
	float4 cloudShadows: SV_Target2;
	float4 cloudLayer: SV_Target3;
#	endif
};

#	if defined(CLOUD_SHADOWS) && defined(CLOUDS)
Texture2D<float> TexDepthSampler : register(t17);

void WriteCloudShadows(inout PS_OUTPUT output, float4 position)
{
	// A broadcast alpha would square coverage under SRC_ALPHA blending.
	// FO4: no alphaTransmittance, the cloud alpha is the coverage.
	output.cloudShadows = float4(1.0, 1.0, 1.0, output.color.w);
	// Debug view: the blend factor colors this broadcast alpha.
	output.cloudLayer = output.color.w;

	// Keep sun behind scene depth to prevent halo leaks through geometry.
	float depth = TexDepthSampler.Load(int3(position.xy, 0));
	if (depth < position.z)
		output.color.w = 0.0;
}
#	endif

float4 motionVector(float4 currentPosition, float4 previousPosition)
{
	float2 current;
	current.x = dot(UnjitteredWorldToClip[0], currentPosition);
	current.y = dot(UnjitteredWorldToClip[1], currentPosition);
	float currentW = dot(UnjitteredWorldToClip[3], currentPosition);
	current /= currentW;

	float2 previous;
	previous.x = dot(PreviousWorldToClip[0], previousPosition);
	previous.y = dot(PreviousWorldToClip[1], previousPosition);
	float previousW = dot(PreviousWorldToClip[3], previousPosition);
	previous /= previousW;

	return float4((current - previous) * float2(-0.5, 0.5), 1.0, 1.0);
}

#	ifdef OCCLUSION
float4 main(float4 position : SV_POSITION) : SV_Target0
{
	return float4(0.0, 0.0, 0.0, 1.0);
}
#	endif

#	ifdef DITHER
#		ifdef TEX
#		else
struct PS_INPUT_SKY
{
	float4 screenPosition: SV_POSITION;
	float4 color: COLOR0;
	float4 currentPosition: POSITION0;
	float4 previousPosition: POSITION1;
};

PS_OUTPUT main(PS_INPUT_SKY input)
{
	PS_OUTPUT output;
	float3 color = input.color;
	if (PParams[0].y > 0.0) {
		color *= PParams[0].y;
	}

	float occlusion = texture2.Sample(sampler2, input.screenPosition.xy * 0.125).x;
	occlusion = occlusion * 0.0078125 - 0.001953125;
	output.color = float4(color + occlusion, 1.0);
	output.motion = motionVector(input.currentPosition, input.previousPosition);
	return output;
}
#		endif
#	endif

#	ifdef TEX
#		ifdef MOONMASK
struct PS_INPUT_MOON_AND_STARS_MASK
{
	float4 screenPosition: SV_POSITION;
	float2 texcoord: TEXCOORD0;
	float4 color: COLOR0;
	float4 currentPosition: POSITION0;
	float4 previousPosition: POSITION1;
};

PS_OUTPUT main(PS_INPUT_MOON_AND_STARS_MASK input)
{
	PS_OUTPUT output;
	float4 texel = texture0.Sample(sampler0, input.texcoord);
	texel.w = pow(texel.w, 2.2);
	output.color = texel;
	clip(texel.w - (1.0 / 255.0));
	output.motion = motionVector(input.currentPosition, input.previousPosition);
	return output;
}
#		endif
#	endif

#	ifdef HORIZFADE
struct PS_INPUT_STARS
{
	float4 screenPosition: SV_POSITION;
	float2 texcoord: TEXCOORD0;
	float fade: TEXCOORD2;
	float4 color: COLOR0;
	float4 currentPosition: POSITION0;
	float4 previousPosition: POSITION1;
};

PS_OUTPUT main(PS_INPUT_STARS input)
{
	PS_OUTPUT output;
	float4 color = texture0.Sample(sampler0, input.texcoord) * input.color;
	output.color.w = color.w * input.fade;
	if (PParams[0].y > 0.0) {
		color.xyz *= PParams[0].y;
	}
	output.color.xyz = color.xyz * 1.5;
	output.motion = motionVector(input.currentPosition, input.previousPosition);
	return output;
}
#	endif

#	ifdef TEX
#		ifdef MOONMASK
#		else
#			ifdef CLOUDS
#			else
#				ifdef DITHER
#				else
struct PS_INPUT_TEXTURE
{
	float4 screenPosition: SV_POSITION;
	float2 texcoord: TEXCOORD0;
	float4 color: COLOR0;
	float4 currentPosition: POSITION0;
	float4 previousPosition: POSITION1;
};

PS_OUTPUT main(PS_INPUT_TEXTURE input)
{
	PS_OUTPUT output;
	float4 texel = texture0.Sample(sampler0, input.texcoord);
	float3 color = texel.xyz * input.color.xyz;
	output.color.w = pow(texel.w, 2.2) * input.color.w;
	if (PParams[0].y > 0.0) {
		color *= PParams[0].y;
	}
	output.color.xyz = color;
	output.motion = motionVector(input.currentPosition, input.previousPosition);
	return output;
}
#				endif
#			endif
#		endif
#	endif

#	ifdef TEX
#		ifdef CLOUDS
#			ifdef TEXLERP
#			else
#				ifdef TEXFADE
#				else
struct PS_INPUT_CLOUDS
{
	float4 screenPosition: SV_POSITION;
	float2 texcoord: TEXCOORD0;
	float4 color: COLOR0;
	float4 currentPosition: POSITION0;
	float4 previousPosition: POSITION1;
};

PS_OUTPUT main(PS_INPUT_CLOUDS input)
{
	PS_OUTPUT output;
	float4 texel = texture0.Sample(sampler0, input.texcoord);
	float3 color = texel.xyz * input.color.xyz;
	output.color.w = texel.w * input.color.w;
	if (PParams[0].y > 0.0) {
		color *= PParams[0].y;
	}
	output.color.xyz = color;
	output.motion = motionVector(input.currentPosition, input.previousPosition);
#					if defined(CLOUD_SHADOWS) && defined(CLOUDS)
	WriteCloudShadows(output, input.screenPosition);
#					endif
	return output;
}
#				endif
#			endif
#		endif
#	endif

#	ifdef TEX
#		ifdef CLOUDS
#			ifdef TEXLERP
struct PS_INPUT_CLOUDS_LERP
{
	float4 screenPosition: SV_POSITION;
	float2 texcoord0: TEXCOORD0;
	float2 texcoord1: TEXCOORD1;
	float4 color: COLOR0;
	float4 currentPosition: POSITION0;
	float4 previousPosition: POSITION1;
};

PS_OUTPUT main(PS_INPUT_CLOUDS_LERP input)
{
	PS_OUTPUT output;
	float4 color0 = texture0.Sample(sampler0, input.texcoord0);
	float4 color1 = texture1.Sample(sampler1, input.texcoord1);
	float4 blended = lerp(color0, color1, PParams[0].x);
	float3 color = blended.xyz * input.color.xyz;
	output.color.w = blended.w * input.color.w;
	if (PParams[0].y > 0.0) {
		color *= PParams[0].y;
	}
	output.color.xyz = color;
	output.motion = motionVector(input.currentPosition, input.previousPosition);
#				if defined(CLOUD_SHADOWS) && defined(CLOUDS)
	WriteCloudShadows(output, input.screenPosition);
#				endif
	return output;
}
#			endif
#		endif
#	endif

#	ifdef TEX
#		ifdef CLOUDS
#			ifdef TEXFADE
struct PS_INPUT_CLOUDS_FADE
{
	float4 screenPosition: SV_POSITION;
	float2 texcoord: TEXCOORD0;
	float4 color: COLOR0;
	float4 currentPosition: POSITION0;
	float4 previousPosition: POSITION1;
};

PS_OUTPUT main(PS_INPUT_CLOUDS_FADE input)
{
	PS_OUTPUT output;
	float4 texel = texture0.Sample(sampler0, input.texcoord);
	float3 color = texel.xyz * input.color.xyz;
	float alpha = saturate(PParams[0].x - 0.4) * texel.w * input.color.w;
	output.color.w = alpha * (5.0 / 3.0);
	if (PParams[0].y > 0.0) {
		color *= PParams[0].y;
	}
	output.color.xyz = color;
	output.motion = motionVector(input.currentPosition, input.previousPosition);
#				if defined(CLOUD_SHADOWS) && defined(CLOUDS)
	WriteCloudShadows(output, input.screenPosition);
#				endif
	return output;
}
#			endif
#		endif
#	endif

#	ifdef TEX
#		ifdef DITHER
struct PS_INPUT_SUN_GLARE
{
	float4 screenPosition: SV_POSITION;
	float2 texcoord: TEXCOORD0;
	float4 color: COLOR0;
	float4 currentPosition: POSITION0;
	float4 previousPosition: POSITION1;
};

PS_OUTPUT main(PS_INPUT_SUN_GLARE input)
{
	PS_OUTPUT output;
	float3 color = texture0.Sample(sampler0, input.texcoord).xyz * input.color.xyz;
	if (PParams[0].y > 0.0) {
		color *= PParams[0].y;
	}

	float occlusion = texture2.Sample(sampler2, input.screenPosition.xy * 0.125).x;
	occlusion = occlusion * 0.0078125 - 0.001953125;
	output.color = float4(color + occlusion, 1.0);
	output.motion = motionVector(input.currentPosition, input.previousPosition);
	return output;
}
#		endif
#	endif
#endif
