// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef BSSKY_VERTEX_SHADER
#ifdef TEX
#define HAS_TEXCOORD
#endif
#ifdef HORIZFADE
#ifndef HAS_TEXCOORD
#define HAS_TEXCOORD
#endif
#endif

struct VS_INPUT
{
	float4 Position : POSITION0;
#ifdef HAS_TEXCOORD
	float2 TexCoord : TEXCOORD0;
#endif
	float4 Color : COLOR0;
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
#ifdef HAS_TEXCOORD
	float2 TexCoord : TEXCOORD0;
#endif
#ifdef TEXLERP
	float2 TexCoord1 : TEXCOORD1;
#endif
#ifdef HORIZFADE
	float TexCoord2 : TEXCOORD2;
#endif
#ifndef OCCLUSION
	float4 Color : COLOR0;
	float4 WorldPosition : POSITION0;
	float4 PreviousWorldPosition : POSITION1;
#endif
};

cbuffer PerGeometry : register(b2)
{
	row_major float4x4 WorldViewProj;
#ifndef OCCLUSION
	row_major float3x4 World;
#ifdef HORIZFADE
	float4 EyePosition;
#endif
	float4 BlendColor[3];
	float2 TexCoordOff;
	row_major float3x4 PreviousWorld;
#endif
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float4 inputPosition = float4(input.Position.xyz, 1.0);
	float4 viewPosition = mul(WorldViewProj, inputPosition);
	vsout.Position.xy = viewPosition.xy;
	vsout.Position.zw = viewPosition.ww;

#ifdef HAS_TEXCOORD
#ifdef CLOUDS
	vsout.TexCoord = input.TexCoord + TexCoordOff;
#else
	vsout.TexCoord = input.TexCoord;
#endif
#endif

#ifdef TEXLERP
	vsout.TexCoord1 = input.TexCoord + TexCoordOff;
#endif

#ifndef OCCLUSION
	float3 worldPosition = mul(World, inputPosition);

#ifdef HORIZFADE
	vsout.TexCoord2 = saturate((worldPosition.z - EyePosition.z) / 17.0);
#endif

#ifdef MOONMASK
	vsout.Color = 1.0;
#else
#ifdef HORIZFADE
	vsout.Color = BlendColor[0];
#else
	float4 skyColor = input.Color.xxxw * BlendColor[0];
	skyColor.xyz += BlendColor[1].xyz * input.Color.y;
	skyColor.xyz += BlendColor[2].xyz * input.Color.z;
	vsout.Color = BlendColor[0] * 0.000001 + skyColor;
#endif
#endif

	vsout.WorldPosition = float4(worldPosition, 1.0);
	vsout.PreviousWorldPosition = float4(mul(PreviousWorld, inputPosition), 1.0);
#endif

	return vsout;
}
#endif

#ifdef BSSKY_PIXEL_SHADER
cbuffer PerFrame : register(b12)
{
    float4 perFrame[41];
};

cbuffer PerTechnique : register(b2)
{
    float4 perTechnique[1];
};

SamplerState sampler0 : register(s0);
SamplerState sampler1 : register(s1);
SamplerState sampler2 : register(s2);

Texture2D<float4> texture0 : register(t0);
Texture2D<float4> texture1 : register(t1);
Texture2D<float4> texture2 : register(t2);

struct PS_OUTPUT
{
    float4 color : SV_Target0;
    float4 motion : SV_Target1;
};

float4 motionVector(float4 currentPosition, float4 previousPosition)
{
    float2 current;
    current.x = dot(perFrame[37], currentPosition);
    current.y = dot(perFrame[38], currentPosition);
    float currentW = dot(perFrame[40], currentPosition);
    current /= currentW;

    float2 previous;
    previous.x = dot(perFrame[31], previousPosition);
    previous.y = dot(perFrame[32], previousPosition);
    float previousW = dot(perFrame[34], previousPosition);
    previous /= previousW;

    return float4((current - previous) * float2(-0.5, 0.5), 1.0, 1.0);
}

#ifdef OCCLUSION
float4 main(float4 position : SV_POSITION) : SV_Target0
{
    return float4(0.0, 0.0, 0.0, 1.0);
}
#endif

#ifdef DITHER
#ifdef TEX
#else
struct PS_INPUT_SKY
{
    float4 screenPosition : SV_POSITION;
    float4 color : COLOR0;
    float4 currentPosition : POSITION0;
    float4 previousPosition : POSITION1;
};

PS_OUTPUT main(PS_INPUT_SKY input)
{
    PS_OUTPUT output;
    float3 color = input.color;
    if (perTechnique[0].y > 0.0)
    {
        color *= perTechnique[0].y;
    }

    float occlusion = texture2.Sample(sampler2, input.screenPosition.xy * 0.125).x;
    occlusion = occlusion * 0.0078125 - 0.001953125;
    output.color = float4(color + occlusion, 1.0);
    output.motion = motionVector(input.currentPosition, input.previousPosition);
    return output;
}
#endif
#endif

#ifdef TEX
#ifdef MOONMASK
struct PS_INPUT_MOON_AND_STARS_MASK
{
    float4 screenPosition : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    float4 currentPosition : POSITION0;
    float4 previousPosition : POSITION1;
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
#endif
#endif

#ifdef HORIZFADE
struct PS_INPUT_STARS
{
    float4 screenPosition : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float fade : TEXCOORD2;
    float4 color : COLOR0;
    float4 currentPosition : POSITION0;
    float4 previousPosition : POSITION1;
};

PS_OUTPUT main(PS_INPUT_STARS input)
{
    PS_OUTPUT output;
    float4 color = texture0.Sample(sampler0, input.texcoord) * input.color;
    output.color.w = color.w * input.fade;
    if (perTechnique[0].y > 0.0)
    {
        color.xyz *= perTechnique[0].y;
    }
    output.color.xyz = color.xyz * 1.5;
    output.motion = motionVector(input.currentPosition, input.previousPosition);
    return output;
}
#endif

#ifdef TEX
#ifdef MOONMASK
#else
#ifdef CLOUDS
#else
#ifdef DITHER
#else
struct PS_INPUT_TEXTURE
{
    float4 screenPosition : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    float4 currentPosition : POSITION0;
    float4 previousPosition : POSITION1;
};

PS_OUTPUT main(PS_INPUT_TEXTURE input)
{
    PS_OUTPUT output;
    float4 texel = texture0.Sample(sampler0, input.texcoord);
    float3 color = texel.xyz * input.color.xyz;
    output.color.w = pow(texel.w, 2.2) * input.color.w;
    if (perTechnique[0].y > 0.0)
    {
        color *= perTechnique[0].y;
    }
    output.color.xyz = color;
    output.motion = motionVector(input.currentPosition, input.previousPosition);
    return output;
}
#endif
#endif
#endif
#endif

#ifdef TEX
#ifdef CLOUDS
#ifdef TEXLERP
#else
#ifdef TEXFADE
#else
struct PS_INPUT_CLOUDS
{
    float4 screenPosition : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    float4 currentPosition : POSITION0;
    float4 previousPosition : POSITION1;
};

PS_OUTPUT main(PS_INPUT_CLOUDS input)
{
    PS_OUTPUT output;
    float4 texel = texture0.Sample(sampler0, input.texcoord);
    float3 color = texel.xyz * input.color.xyz;
    output.color.w = texel.w * input.color.w;
    if (perTechnique[0].y > 0.0)
    {
        color *= perTechnique[0].y;
    }
    output.color.xyz = color;
    output.motion = motionVector(input.currentPosition, input.previousPosition);
    return output;
}
#endif
#endif
#endif
#endif

#ifdef TEX
#ifdef CLOUDS
#ifdef TEXLERP
struct PS_INPUT_CLOUDS_LERP
{
    float4 screenPosition : SV_POSITION;
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
    float4 color : COLOR0;
    float4 currentPosition : POSITION0;
    float4 previousPosition : POSITION1;
};

PS_OUTPUT main(PS_INPUT_CLOUDS_LERP input)
{
    PS_OUTPUT output;
    float4 color0 = texture0.Sample(sampler0, input.texcoord0);
    float4 color1 = texture1.Sample(sampler1, input.texcoord1);
    float4 blended = lerp(color0, color1, perTechnique[0].x);
    float3 color = blended.xyz * input.color.xyz;
    output.color.w = blended.w * input.color.w;
    if (perTechnique[0].y > 0.0)
    {
        color *= perTechnique[0].y;
    }
    output.color.xyz = color;
    output.motion = motionVector(input.currentPosition, input.previousPosition);
    return output;
}
#endif
#endif
#endif

#ifdef TEX
#ifdef CLOUDS
#ifdef TEXFADE
struct PS_INPUT_CLOUDS_FADE
{
    float4 screenPosition : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    float4 currentPosition : POSITION0;
    float4 previousPosition : POSITION1;
};

PS_OUTPUT main(PS_INPUT_CLOUDS_FADE input)
{
    PS_OUTPUT output;
    float4 texel = texture0.Sample(sampler0, input.texcoord);
    float3 color = texel.xyz * input.color.xyz;
    float alpha = saturate(perTechnique[0].x - 0.4) * texel.w * input.color.w;
    output.color.w = alpha * (5.0 / 3.0);
    if (perTechnique[0].y > 0.0)
    {
        color *= perTechnique[0].y;
    }
    output.color.xyz = color;
    output.motion = motionVector(input.currentPosition, input.previousPosition);
    return output;
}
#endif
#endif
#endif

#ifdef TEX
#ifdef DITHER
struct PS_INPUT_SUN_GLARE
{
    float4 screenPosition : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    float4 currentPosition : POSITION0;
    float4 previousPosition : POSITION1;
};

PS_OUTPUT main(PS_INPUT_SUN_GLARE input)
{
    PS_OUTPUT output;
    float3 color = texture0.Sample(sampler0, input.texcoord).xyz * input.color.xyz;
    if (perTechnique[0].y > 0.0)
    {
        color *= perTechnique[0].y;
    }

    float occlusion = texture2.Sample(sampler2, input.screenPosition.xy * 0.125).x;
    occlusion = occlusion * 0.0078125 - 0.001953125;
    output.color = float4(color + occlusion, 1.0);
    output.motion = motionVector(input.currentPosition, input.previousPosition);
    return output;
}
#endif
#endif
#endif
