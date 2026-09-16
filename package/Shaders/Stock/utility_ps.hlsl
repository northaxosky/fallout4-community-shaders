//------------------------------------------------------------------------------
// Utility.hlsl - pixel stage (reconstruction candidate, strict selector policy)
//------------------------------------------------------------------------------
// Selector policy: an engine-emitted macro name is tested exactly once, on its
// own bare `#ifdef` line in the lift table below, and appears nowhere else in
// this file. Everything past the table branches on the internal 0/1 selectors.
//------------------------------------------------------------------------------

#define U_ADDITIONAL_ALPHA_MASK 0
#ifdef ADDITIONAL_ALPHA_MASK
#	undef U_ADDITIONAL_ALPHA_MASK
#	define U_ADDITIONAL_ALPHA_MASK 1
#endif

#define U_ALPHA_TEST 0
#ifdef ALPHA_TEST
#	undef U_ALPHA_TEST
#	define U_ALPHA_TEST 1
#endif

#define U_COMBINED 0
#ifdef COMBINED
#	undef U_COMBINED
#	define U_COMBINED 1
#endif

#define U_DEBUG_COLOR 0
#ifdef DEBUG_COLOR
#	undef U_DEBUG_COLOR
#	define U_DEBUG_COLOR 1
#endif

#define U_DEBUG_SHADOWSPLIT 0
#ifdef DEBUG_SHADOWSPLIT
#	undef U_DEBUG_SHADOWSPLIT
#	define U_DEBUG_SHADOWSPLIT 1
#endif

#define U_RENDER_BASE_TEXTURE 0
#ifdef RENDER_BASE_TEXTURE
#	undef U_RENDER_BASE_TEXTURE
#	define U_RENDER_BASE_TEXTURE 1
#endif

#define U_RENDER_DEPTH 0
#ifdef RENDER_DEPTH
#	undef U_RENDER_DEPTH
#	define U_RENDER_DEPTH 1
#endif

#define U_RENDER_NORMAL 0
#ifdef RENDER_NORMAL
#	undef U_RENDER_NORMAL
#	define U_RENDER_NORMAL 1
#endif

#define U_RENDER_NORMAL_CLAMP 0
#ifdef RENDER_NORMAL_CLAMP
#	undef U_RENDER_NORMAL_CLAMP
#	define U_RENDER_NORMAL_CLAMP 1
#endif

#define U_RENDER_SHADOWMAP 0
#ifdef RENDER_SHADOWMAP
#	undef U_RENDER_SHADOWMAP
#	define U_RENDER_SHADOWMAP 1
#endif

#define U_RENDER_SHADOWMAP_PB 0
#ifdef RENDER_SHADOWMAP_PB
#	undef U_RENDER_SHADOWMAP_PB
#	define U_RENDER_SHADOWMAP_PB 1
#endif

#define U_STENCIL_ABOVE_WATER 0
#ifdef STENCIL_ABOVE_WATER
#	undef U_STENCIL_ABOVE_WATER
#	define U_STENCIL_ABOVE_WATER 1
#endif

#define U_TEXTURE 0
#ifdef TEXTURE
#	undef U_TEXTURE
#	define U_TEXTURE 1
#endif

#define U_TREE_ANIM 0
#ifdef TREE_ANIM
#	undef U_TREE_ANIM
#	define U_TREE_ANIM 1
#endif

#define U_VATS_DEBUG_COLOR 0
#ifdef VATS_DEBUG_COLOR
#	undef U_VATS_DEBUG_COLOR
#	define U_VATS_DEBUG_COLOR 1
#endif

#define U_VATS_MASK 0
#ifdef VATS_MASK
#	undef U_VATS_MASK
#	define U_VATS_MASK 1
#endif

#define U_VC 0
#ifdef VC
#	undef U_VC
#	define U_VC 1
#endif

//------------------------------------------------------------------------------
// Derived selectors.
//------------------------------------------------------------------------------

#define UTIL_SAMPLE_UV (U_ALPHA_TEST || U_ADDITIONAL_ALPHA_MASK || U_RENDER_BASE_TEXTURE)
#define UTIL_DEPTH_PACKED ((U_RENDER_SHADOWMAP && !U_RENDER_SHADOWMAP_PB) || U_RENDER_DEPTH)
#define UTIL_UV_IN_TEXCOORD4 (UTIL_SAMPLE_UV && UTIL_DEPTH_PACKED)
#define UTIL_VATS_NORMAL (U_VATS_MASK && !U_VATS_DEBUG_COLOR)
#define UTIL_FLAT_COLOR (U_DEBUG_COLOR || U_VATS_DEBUG_COLOR)
#define UTIL_TEXALPHA (U_ALPHA_TEST && U_VC && !U_TREE_ANIM)
#define UTIL_TEXALPHA_USED (UTIL_TEXALPHA && (U_RENDER_SHADOWMAP || U_RENDER_DEPTH))

//------------------------------------------------------------------------------

struct PS_INPUT
{
	float4 HPosition : SV_POSITION;

#if UTIL_UV_IN_TEXCOORD4
	float4 TexCoord4 : TEXCOORD4;
#elif UTIL_SAMPLE_UV || U_RENDER_NORMAL || U_DEBUG_SHADOWSPLIT || U_RENDER_BASE_TEXTURE || (U_TEXTURE && UTIL_VATS_NORMAL)
	float4 TexCoord0 : TEXCOORD0;
#endif

#if U_RENDER_SHADOWMAP_PB
	float3 Depth : TEXCOORD1;
#elif UTIL_VATS_NORMAL || U_RENDER_NORMAL
	float4 Normal : TEXCOORD1;
#elif U_DEBUG_SHADOWSPLIT
	float ShadowDepth : TEXCOORD1;
#elif UTIL_DEPTH_PACKED && !UTIL_SAMPLE_UV
	float2 Depth2 : TEXCOORD1;
#endif

#if UTIL_TEXALPHA
	float TexAlpha : TEXCOORD2;
#endif

#if U_COMBINED
	nointerpolation uint InstanceIndex : COLOR1;
#endif

#if U_ALPHA_TEST && U_VC
	float4 Color : TEXCOORD5;
#endif
};

//------------------------------------------------------------------------------

#if U_ALPHA_TEST && U_COMBINED
struct UtilityInstanceData
{
	float4 Unused0;
	float3 Unused1;
	float AlphaTestRef;
	float4 Unused2;
	float4 Unused3;
	uint ArrayIndex;
	float3 Unused4;
	float3 Unused5;
};
StructuredBuffer<UtilityInstanceData> InstanceData : register(t5);
Texture2DArray<float4> BaseTexArray : register(t0);
SamplerState BaseSampler : register(s0);
#elif U_ALPHA_TEST || U_DEBUG_SHADOWSPLIT || U_RENDER_BASE_TEXTURE
Texture2D<float4> BaseTex : register(t0);
SamplerState BaseSampler : register(s0);
#endif

#if U_RENDER_NORMAL
Texture2D<float4> NormalTex : register(t1);
SamplerState NormalSampler : register(s1);
#endif

#if U_ADDITIONAL_ALPHA_MASK
Texture2D<float4> MaskTex : register(t4);
SamplerState MaskSampler : register(s4);
#endif

cbuffer PerGeometry : register(b0)
{
	float4 UtilPad0;
	float4 UtilPad1;
	float4 ShadowSplits;
};

cbuffer PerFrame : register(b1)
{
	float4 NormalScale;
};

cbuffer PerMaterial : register(b2)
{
	float4 PropertyColor;
	float4 AlphaData;
};

//------------------------------------------------------------------------------

#if U_RENDER_SHADOWMAP
void main(PS_INPUT input)
#else
float4 main(PS_INPUT input) : SV_Target0
#endif
{
#if U_RENDER_SHADOWMAP_PB
	clip(input.Depth.z - 0.5);
#endif

#if UTIL_SAMPLE_UV
#	if UTIL_UV_IN_TEXCOORD4
	float2 uv = input.TexCoord4.zw;
#	else
	float2 uv = input.TexCoord0.xy;
#	endif
#endif

#if U_ALPHA_TEST || U_RENDER_BASE_TEXTURE
#	if U_COMBINED && U_ALPHA_TEST
	float4 baseColor = BaseTexArray.Sample(BaseSampler, float3(uv, (float)InstanceData[input.InstanceIndex].ArrayIndex));
	float alphaRef = InstanceData[input.InstanceIndex].AlphaTestRef;
#	else
	float4 baseColor = BaseTex.Sample(BaseSampler, uv);
	float alphaRef = AlphaData.x;
#	endif
#endif

#if U_ALPHA_TEST
	float alphaValue = baseColor.w;
#	if UTIL_TEXALPHA_USED
	alphaValue = alphaValue * input.TexAlpha;
#	endif

#	if !U_RENDER_SHADOWMAP
	clip(alphaValue - alphaRef);
#	endif

#	if U_VC
	clip(alphaValue * input.Color.w - alphaRef);
#	else
	clip(alphaValue - alphaRef);
#	endif
#endif

#if U_ADDITIONAL_ALPHA_MASK
	clip(AlphaData.w - MaskTex.Sample(MaskSampler, uv).w);
#endif

#if !U_RENDER_SHADOWMAP
#	if UTIL_FLAT_COLOR
	return PropertyColor;
#	elif U_RENDER_DEPTH
	float4 depthColor;
#		if UTIL_UV_IN_TEXCOORD4
	depthColor.x = input.TexCoord4.x / input.TexCoord4.y;
#		else
	depthColor.x = input.Depth2.x / input.Depth2.y;
#		endif
	depthColor.y = 1;
	depthColor.z = 1;
#		if U_ALPHA_TEST
	depthColor.w = alphaValue;
#		else
	depthColor.w = 1;
#		endif
	return depthColor;
#	elif UTIL_VATS_NORMAL
	return float4(input.Normal.xyz * 0.5 + 0.5, 1);
#	elif U_RENDER_BASE_TEXTURE
	return baseColor;
#	elif U_RENDER_NORMAL
	float2 offset = NormalTex.Sample(NormalSampler, input.TexCoord0.xy).xy - 0.5;
	offset = offset * 2;
#		if U_RENDER_NORMAL_CLAMP
	offset = max(min(offset, 0.1), -0.1);
#		endif
	float2 projected = (offset * 0.9 + input.Normal.xy) / input.TexCoord0.z;
	return float4(projected * 0.5 + 0.5, input.TexCoord0.w * NormalScale.x * input.Normal.w, 1);
#	elif U_DEBUG_SHADOWSPLIT
	float depth = input.ShadowDepth;
	float3 splitColor = float3(0, 1, 0) * (depth < ShadowSplits.x) + float3(0, 0, 1) * (depth < ShadowSplits.y && depth > ShadowSplits.x) + float3(1, 0, 0) * (depth < ShadowSplits.z && depth > ShadowSplits.y) + float3(0, 1, 1) * (depth < ShadowSplits.w && depth > ShadowSplits.z);
	float4 texColor = BaseTex.Sample(BaseSampler, input.TexCoord0.xy);
	return float4(lerp(splitColor, texColor.xyz, 0.75), texColor.w);
#	elif U_STENCIL_ABOVE_WATER
	return float4(1, 0, 0, 0.5);
#	else
	return float4(1, 1, 1, 1);
#	endif
#endif
}
