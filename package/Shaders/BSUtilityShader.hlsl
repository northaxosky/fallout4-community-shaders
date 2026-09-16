#if defined(BSUTILITY_PS_SOURCE)
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
#elif defined(BSUTILITY_VS_SOURCE)
//------------------------------------------------------------------------------
// Utility.hlsl - vertex stage (reconstruction candidate, strict selector policy)
//------------------------------------------------------------------------------
// Selector policy: an engine-emitted macro name is tested exactly once, on its
// own bare ifdef line in the lift table below, and appears nowhere else in this
// file. Everything past the table branches on the internal 0/1 selectors.
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

#define U_BINORMAL_TANGENT 0
#ifdef BINORMAL_TANGENT
#	undef U_BINORMAL_TANGENT
#	define U_BINORMAL_TANGENT 1
#endif

#define U_CLIP_VOLUME 0
#ifdef CLIP_VOLUME
#	undef U_CLIP_VOLUME
#	define U_CLIP_VOLUME 1
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

#define U_EYE 0
#ifdef EYE
#	undef U_EYE
#	define U_EYE 1
#endif

#define U_GRAYSCALE_MASK 0
#ifdef GRAYSCALE_MASK
#	undef U_GRAYSCALE_MASK
#	define U_GRAYSCALE_MASK 1
#endif

#define U_LOD_OBJECT 0
#ifdef LOD_OBJECT
#	undef U_LOD_OBJECT
#	define U_LOD_OBJECT 1
#endif

#define U_MERGE_INSTANCED 0
#ifdef MERGE_INSTANCED
#	undef U_MERGE_INSTANCED
#	define U_MERGE_INSTANCED 1
#endif

#define U_NORMALS 0
#ifdef NORMALS
#	undef U_NORMALS
#	define U_NORMALS 1
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

#define U_RENDER_NORMAL_FALLOFF 0
#ifdef RENDER_NORMAL_FALLOFF
#	undef U_RENDER_NORMAL_FALLOFF
#	define U_RENDER_NORMAL_FALLOFF 1
#endif

#define U_RENDER_SHADOWMAP 0
#ifdef RENDER_SHADOWMAP
#	undef U_RENDER_SHADOWMAP
#	define U_RENDER_SHADOWMAP 1
#endif

#define U_RENDER_SHADOWMAP_CLAMPED 0
#ifdef RENDER_SHADOWMAP_CLAMPED
#	undef U_RENDER_SHADOWMAP_CLAMPED
#	define U_RENDER_SHADOWMAP_CLAMPED 1
#endif

#define U_RENDER_SHADOWMAP_PB 0
#ifdef RENDER_SHADOWMAP_PB
#	undef U_RENDER_SHADOWMAP_PB
#	define U_RENDER_SHADOWMAP_PB 1
#endif

#define U_SKINNED 0
#ifdef SKINNED
#	undef U_SKINNED
#	define U_SKINNED 1
#endif

#define U_SPLINE 0
#ifdef SPLINE
#	undef U_SPLINE
#	define U_SPLINE 1
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
// Vertex-visible channels the stage carries.
//------------------------------------------------------------------------------

#define DEPTH_TARGET (U_RENDER_SHADOWMAP || U_RENDER_DEPTH)
#define ALPHA_SOURCE (U_ALPHA_TEST || U_ADDITIONAL_ALPHA_MASK)
#define VATS_NORMAL (U_VATS_MASK && !U_VATS_DEBUG_COLOR)
#define UV_MODE (U_RENDER_BASE_TEXTURE || (U_ALPHA_TEST && U_VATS_DEBUG_COLOR))
#define PB_DEPTH (DEPTH_TARGET && U_RENDER_SHADOWMAP_PB)
#define MERGED_DEPTH_UV (DEPTH_TARGET && ALPHA_SOURCE && !U_RENDER_SHADOWMAP_PB)
#define PLAIN_DEPTH (DEPTH_TARGET && !ALPHA_SOURCE && !U_RENDER_SHADOWMAP_PB)
#define NORMAL_TARGET (U_RENDER_NORMAL)
#define UV_OUTPUT (U_TEXTURE && (UV_MODE || VATS_NORMAL || NORMAL_TARGET || U_DEBUG_SHADOWSPLIT || (U_RENDER_SHADOWMAP_PB && ALPHA_SOURCE)))
#define COLOR_OUTPUT (U_ALPHA_TEST && U_VC)
// VATS and the normal target share this channel; the VATS form wins, dropping clamp and alpha.
#define NORMAL_CHANNEL (VATS_NORMAL || NORMAL_TARGET)
#define NORMAL_CHANNEL_IS_TARGET (NORMAL_TARGET && !VATS_NORMAL)
// The depth and shadow-split channels take TEXCOORD1 first; the normal channel moves aside.
#define TEXCOORD1_CLAIMED (PLAIN_DEPTH || PB_DEPTH || U_DEBUG_SHADOWSPLIT)
// The eye family carries the instance index in; only the combined family passes it on.
#define INSTANCE_INDEX_INPUT (U_EYE || U_COMBINED)
#define INSTANCE_INDEX_OUTPUT (U_COMBINED)
#define INSTANCE_TRANSFORM (U_COMBINED && !U_SKINNED)

//------------------------------------------------------------------------------
// Constant buffers, in the order the native containers declare them.
//------------------------------------------------------------------------------

cbuffer PerFrame : register(b12)
{
	row_major float4x4 CameraView;
	float4 PerFrameReserved4[4];
	row_major float4x4 CameraViewProj;
	float4 PerFrameReserved12[23];
	float3 CameraPosAdjust;
}

cbuffer PerTechnique : register(b0)
{
	float4 PerTechniqueReserved0;
	float2 ParaboloidParams;
#if U_CLIP_VOLUME
	float3 ClipVolumeCenter;
	float3 ClipVolumeRadius;
#endif
}

cbuffer PerMaterial : register(b1)
{
	float4 TexCoordOffset;
}

cbuffer PerGeometry : register(b2)
{
#if U_SKINNED
	// The skinned families never bind a world matrix, so the falloff record sits first.
	float4 NormalFalloff;
#else
	// Every member below World shifts register with the geometry family selectors.
	row_major float4x4 World;
	// xyz: falloff origin in object space, w: normal-target opacity scale.
	float4 NormalFalloff;
#if U_TREE_ANIM
	float4 TreeParams;
	float4 PerGeometryReserved6;
#elif U_STENCIL_ABOVE_WATER
	float AboveWaterOffset;
#else
	float4 PerGeometryReserved5;
#endif
#if U_MERGE_INSTANCED
	uint4 MergedAttributeOffsets;
	uint4 MergedGeometryParams;
	uint4 MergedGeometryReserved;
#elif U_SPLINE
	float4 SplineSway;
	float4 SplineParams;
#endif
#if U_TREE_ANIM
	float4 WindData;
	float4 WindStrength;
#endif
#endif
}

cbuffer BonePalette : register(b10)
{
	float4 BoneRows[180];
}

struct MergedInstanceTransform
{
	float4 Row0;
	float4 Row1;
	float4 Row2;
	float4 PositionScale;
	float4 Unused;
};

#if U_MERGE_INSTANCED
ByteAddressBuffer MergedVertexData : register(t5);
ByteAddressBuffer MergedIndexData : register(t6);
ByteAddressBuffer MergedInstanceIndexData : register(t7);
StructuredBuffer<MergedInstanceTransform> MergedInstanceData : register(t8);
#endif

// 100-byte per-instance record; only the three affine rows at byte 0/16/32 are read.
struct InstanceRecord
{
	float4 WorldRows[3];
	float4 UnusedRows[3];
	float UnusedTail;
};

#if INSTANCE_TRANSFORM
StructuredBuffer<InstanceRecord> InstanceData : register(t6);
#endif

struct VS_INPUT
{
#if U_MERGE_INSTANCED
	uint VertexId : SV_VertexID0;
#else
	float4 Position : POSITION0;
#if U_TEXTURE
	float2 TexCoord : TEXCOORD0;
#endif
#if U_NORMALS
	float4 Normal : NORMAL0;
#endif
#if U_BINORMAL_TANGENT
	float4 Bitangent : BINORMAL0;
#endif
#if U_VC
	float4 Color : COLOR0;
#endif
#if U_SKINNED
	float4 BlendWeight : BLENDWEIGHT0;
	float4 BlendIndices : BLENDINDICES0;
#endif
#if INSTANCE_INDEX_INPUT
	float InstanceIndex : TEXCOORD2;
#endif
#endif
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION0;
#if MERGED_DEPTH_UV
	float4 DepthUv : TEXCOORD4;
#elif UV_OUTPUT
	float4 TexCoord : TEXCOORD0;
#endif
#if NORMAL_CHANNEL && TEXCOORD1_CLAIMED
	float4 Normal : TEXCOORD7;
#elif NORMAL_CHANNEL
	float4 Normal : TEXCOORD1;
#endif
#if PLAIN_DEPTH
	float2 Depth : TEXCOORD1;
#elif PB_DEPTH
	float3 Depth : TEXCOORD1;
#elif U_DEBUG_SHADOWSPLIT
	float ShadowSplit : TEXCOORD1;
#endif
#if COLOR_OUTPUT && !U_TREE_ANIM
	float ColorAlpha : TEXCOORD2;
#endif
#if INSTANCE_INDEX_OUTPUT
	uint InstanceIndex : COLOR1;
#endif
#if COLOR_OUTPUT
	float4 Color : TEXCOORD5;
#endif
#if U_CLIP_VOLUME
	float ClipDistance : SV_ClipDistance0;
#endif
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT output;

#if U_STENCIL_ABOVE_WATER
	// This selector replaces the transform pipeline with an object-space pass-through.
	output.Position.xyz = input.Position.xyz;
	output.Position.y += AboveWaterOffset * 2.0;
	output.Position.w = 1.0;
#else
#if U_MERGE_INSTANCED
	uint vertexId = input.VertexId + MergedGeometryParams.z;
	uint vertexParity = vertexId & 1;
	uint packedVertexIndex = MergedIndexData.Load(2 * vertexId - 2 * vertexParity);
	uint mergedVertex =
		(packedVertexIndex & 0xffff) * (1 - vertexParity) +
		(packedVertexIndex >> 16) * vertexParity;
	uint2 packedPosition = MergedVertexData.Load2(mergedVertex * MergedGeometryParams.x);
	float3 localPosition = float3(
		f16tof32(packedPosition.x),
		f16tof32(packedPosition.x >> 16),
		f16tof32(packedPosition.y));

	uint instanceSlot = vertexId / (MergedGeometryParams.y * 3);
	uint instanceParity = instanceSlot & 1;
	uint packedInstanceIndex = MergedInstanceIndexData.Load(2 * instanceSlot - 2 * instanceParity);
	uint mergedInstance =
		(packedInstanceIndex & 0xffff) * (1 - instanceParity) +
		(packedInstanceIndex >> 16) * instanceParity;

	MergedInstanceTransform instanceTransform = MergedInstanceData[mergedInstance];
	float3 instancePosition = float3(
		dot(localPosition, float3(instanceTransform.Row0.x, instanceTransform.Row1.x, instanceTransform.Row2.x)),
		dot(localPosition, float3(instanceTransform.Row0.y, instanceTransform.Row1.y, instanceTransform.Row2.y)),
		dot(localPosition, float3(instanceTransform.Row0.z, instanceTransform.Row1.z, instanceTransform.Row2.z)));
	float4 position = float4(
		instanceTransform.PositionScale.w * instancePosition + instanceTransform.PositionScale.xyz,
		1.0);

#if U_TREE_ANIM || NORMAL_CHANNEL
	uint packedNormal = MergedVertexData.Load(
		mergedVertex * MergedGeometryParams.x + MergedAttributeOffsets.x);
#endif
#if MERGED_DEPTH_UV || UV_OUTPUT
	uint packedTexCoord = MergedVertexData.Load(
		mergedVertex * MergedGeometryParams.x + MergedAttributeOffsets.z);
#endif
#if COLOR_OUTPUT || U_TREE_ANIM || (NORMAL_CHANNEL_IS_TARGET && U_VC)
	uint packedColor = MergedVertexData.Load(
		mergedVertex * MergedGeometryParams.x + MergedAttributeOffsets.w);
#endif
#else
	float4 position = float4(input.Position.xyz, 1.0);
#endif

#if INSTANCE_INDEX_OUTPUT
	uint instanceIndex = (uint)input.InstanceIndex;
	output.InstanceIndex = instanceIndex;
#endif

#if U_SKINNED
	int4 boneIndices = (int4)(input.BlendIndices * 765.01);
	int4 boneIndices01 = boneIndices.xxyy + int4(1, 2, 1, 2);
	int4 boneIndices23 = boneIndices.zzww + int4(1, 2, 1, 2);
	float remainingWeight = 1.0 - saturate(
		input.BlendWeight.x + input.BlendWeight.y + input.BlendWeight.z);
	float4 cameraOffsetX = float4(0.0, 0.0, 0.0, CameraPosAdjust.x);
	float4 cameraOffsetY = float4(0.0, 0.0, 0.0, CameraPosAdjust.y);
	float4 cameraOffsetZ = float4(0.0, 0.0, 0.0, CameraPosAdjust.z);
	float4 skinRowX =
		input.BlendWeight.x * (BoneRows[boneIndices.x] - cameraOffsetX) +
		input.BlendWeight.y * (BoneRows[boneIndices.y] - cameraOffsetX) +
		input.BlendWeight.z * (BoneRows[boneIndices.z] - cameraOffsetX) +
		remainingWeight * (BoneRows[boneIndices.w] - cameraOffsetX);
	float4 skinRowY =
		input.BlendWeight.x * (BoneRows[boneIndices01.x] - cameraOffsetY) +
		input.BlendWeight.y * (BoneRows[boneIndices01.z] - cameraOffsetY) +
		input.BlendWeight.z * (BoneRows[boneIndices23.x] - cameraOffsetY) +
		remainingWeight * (BoneRows[boneIndices23.z] - cameraOffsetY);
	float4 skinRowZ =
		input.BlendWeight.x * (BoneRows[boneIndices01.y] - cameraOffsetZ) +
		input.BlendWeight.y * (BoneRows[boneIndices01.w] - cameraOffsetZ) +
		input.BlendWeight.z * (BoneRows[boneIndices23.y] - cameraOffsetZ) +
		remainingWeight * (BoneRows[boneIndices23.w] - cameraOffsetZ);
	float4 skinnedPosition = float4(
		dot(position, skinRowX),
		dot(position, skinRowY),
		dot(position, skinRowZ),
		1.0);
	output.Position = mul(CameraViewProj, skinnedPosition);
#else
	// The engine folds the camera translation into the world translation column.
#if INSTANCE_TRANSFORM
	float4x4 world;
	world[0] = InstanceData[instanceIndex].WorldRows[0];
	world[1] = InstanceData[instanceIndex].WorldRows[1];
	world[2] = InstanceData[instanceIndex].WorldRows[2];
	world[3] = float4(0.0, 0.0, 0.0, 1.0);
#else
	float4x4 world = World;
#endif
	world[0].w -= CameraPosAdjust.x;
	world[1].w -= CameraPosAdjust.y;
	world[2].w -= CameraPosAdjust.z;

#if U_TREE_ANIM
	float treeSeed = world[0].w + world[1].w + world[2].w +
		CameraPosAdjust.x + CameraPosAdjust.y + CameraPosAdjust.z;
#if U_MERGE_INSTANCED
	float3 leafNormal;
	leafNormal.x = (packedNormal & 0xff) * (2.0 / 255.0) - 1.0;
	leafNormal.y = ((packedNormal >> 8) & 0xff) * (2.0 / 255.0) - 1.0;
	leafNormal.z = ((packedNormal >> 16) & 0xff) * (2.0 / 255.0) - 1.0;
	float3 treeNormal = float3(
		dot(leafNormal, float3(instanceTransform.Row0.x, instanceTransform.Row1.x, instanceTransform.Row2.x)),
		dot(leafNormal, float3(instanceTransform.Row0.y, instanceTransform.Row1.y, instanceTransform.Row2.y)),
		dot(leafNormal, float3(instanceTransform.Row0.z, instanceTransform.Row1.z, instanceTransform.Row2.z)));
	float treeAlpha = (packedColor >> 24) / 255.0;
#else
#if U_NORMALS
	float3 treeNormal = input.Normal.xyz * 2.0 - 1.0;
#else
	float3 treeNormal = float3(0.0, 0.0, 1.0);
#endif
#if U_VC
	float treeAlpha = saturate(input.Color.w);
#else
	float treeAlpha = 1.0;
#endif
#endif
	float windHalfRange = (WindStrength.y - WindStrength.x) * 0.5;
	float windFrequency = TreeParams.w * 20.0;
	float leafAmplitude = TreeParams.z * 0.035;
	float leafPhase = (treeNormal.x + treeNormal.y + treeNormal.z) * 3.0 + treeSeed +
		(windFrequency * WindStrength.z * WindData.w) * 5.0;
	float leafScale = (sin(leafPhase) * windHalfRange + WindStrength.x) * leafAmplitude;
	position.xyz = treeNormal * leafScale * treeAlpha + position.xyz;

	float4 worldPosition = mul(world, position);
	float3 worldOrigin = float3(world[0].w, world[1].w, world[2].w);
	float3 treeOffset = worldPosition.xyz - worldOrigin;
	float bendHeight = (treeOffset.x + treeOffset.y + treeOffset.z) / TreeParams.x;
	float trunkPhase = bendHeight * -1.5 + treeSeed + (WindData.w * WindStrength.z) * windFrequency;
	float trunkWave = sin(trunkPhase);
	float gust = sin(trunkWave * 3.1415927) * 0.3 + sin(trunkWave * 6.2831855) + 2.0;
	float bend = (gust * windHalfRange + WindStrength.x) * bendHeight;
	float2 windVector = float2(cos(WindData.x), sin(WindData.x));
	float3 sway = bend * float3(0.33333334, 0.33333334, 0.0);
	sway = sway * float3(windVector, 1.0);
	sway = sway * treeAlpha;
	float3 bentOffset = treeOffset + sway;
	float3 treeWorld = normalize(bentOffset) * length(treeOffset) + worldOrigin;
	output.Position = mul(CameraViewProj, float4(treeWorld, worldPosition.w));
#else
#if U_SPLINE
#if U_VC
	float swayWeight = input.Color.w;
#else
	float swayWeight = 1.0;
#endif
	float3 originWorld = float3(world[0].w, world[1].w, world[2].w) + CameraPosAdjust;
	float span = SplineParams.y - SplineParams.x;
	float amplitude = span * span * 0.002;
	float phase = dot(originWorld, 1.0) * 0.001;
	float timeScale = SplineSway.w * 5.0;
	float period = SplineParams.z * 10.0;
	float offsetHz = SplineParams.z * -40.0;
	float bias = SplineParams.x * 0.25;
	float bend = (phase * sin(phase * timeScale)) / period;
	float q = bend * 3.1415927 + phase;
	float stiffness = SplineSway.y * swayWeight + 1.0;
	float wave = period * timeScale + (stiffness * q + offsetHz);
	float displace = (amplitude * sin(wave) + bias) * swayWeight;
	float2 splineDirection;
	sincos(SplineSway.x, splineDirection.y, splineDirection.x);
	world._m03_m13 += splineDirection * displace;
#endif

	float4x4 worldViewProj = mul(CameraViewProj, world);
	output.Position = mul(worldViewProj, position);
#endif
#endif

#if NORMAL_TARGET
	// The normal target's depth fade reads the clip depth before any target clamp.
	float normalFadeDepth = output.Position.z;
#endif

#if U_RENDER_SHADOWMAP_PB
	float4 clipPosition = output.Position;
	float3 paraboloidInput = clipPosition.xyz / clipPosition.w;
	float paraboloidDepth = length(paraboloidInput) * ParaboloidParams.x;
	float3 paraboloidDirection = normalize(
		normalize(paraboloidInput) + float3(0.0, 0.0, ParaboloidParams.y));
	float paraboloidSlice =
		clipPosition.z * ParaboloidParams.y * 0.5 + 0.5;
	output.Position = float4(
		paraboloidDirection.xy / paraboloidDirection.z,
		paraboloidDepth,
		clipPosition.w);
#endif

#if U_RENDER_SHADOWMAP_CLAMPED
	output.Position.z = max(output.Position.z, 0.0);
#endif

#if MERGED_DEPTH_UV || UV_OUTPUT
#if U_MERGE_INSTANCED
	float2 uv = float2(f16tof32(packedTexCoord), f16tof32(packedTexCoord >> 16));
#elif U_TEXTURE
	float2 uv = input.TexCoord;
#else
	float2 uv = 0.0;
#endif
	float2 texCoord = uv * TexCoordOffset.zw + TexCoordOffset.xy;
#endif

#if NORMAL_CHANNEL
#if U_MERGE_INSTANCED
	float3 localNormal = float3(
		(packedNormal & 0xff) * (2.0 / 255.0) - 1.0,
		((packedNormal >> 8) & 0xff) * (2.0 / 255.0) - 1.0,
		((packedNormal >> 16) & 0xff) * (2.0 / 255.0) - 1.0);
	float3 targetNormal = float3(
		dot(localNormal, float3(instanceTransform.Row0.x, instanceTransform.Row1.x, instanceTransform.Row2.x)),
		dot(localNormal, float3(instanceTransform.Row0.y, instanceTransform.Row1.y, instanceTransform.Row2.y)),
		dot(localNormal, float3(instanceTransform.Row0.z, instanceTransform.Row1.z, instanceTransform.Row2.z)));
#elif U_NORMALS
	float3 targetNormal = input.Normal.xyz * 2.0 - 1.0;
#else
	float3 targetNormal = float3(0.0, 0.0, 1.0);
#endif

#if NORMAL_TARGET
#if U_RENDER_NORMAL_FALLOFF
	float3 falloffDirection = normalize(NormalFalloff.xyz - input.Position.xyz);
	float normalFalloff = dot(targetNormal, falloffDirection) * NormalFalloff.w;
#else
	float normalFalloff = NormalFalloff.w;
#endif
#endif

#if U_SKINNED
	float3 skinnedTargetNormal = float3(
		dot(targetNormal, skinRowX.xyz),
		dot(targetNormal, skinRowY.xyz),
		dot(targetNormal, skinRowZ.xyz));
	skinnedTargetNormal = normalize(skinnedTargetNormal);
	float3 viewNormal = mul((float3x3)CameraView, skinnedTargetNormal);
#else
	float4x4 viewWorld = mul(CameraView, world);
	float3 viewNormal = mul((float3x3)viewWorld, targetNormal);
#endif

#if NORMAL_CHANNEL_IS_TARGET
#if U_RENDER_NORMAL_CLAMP
	// fxc lowers clamp() as max-then-min; the native order proves the source shape.
	viewNormal = max(min(viewNormal, 0.1), -0.1);
#endif
#if !U_VC
	output.Normal.w = 1.0;
#elif U_MERGE_INSTANCED
	output.Normal.w = (packedColor >> 24) * (1.0 / 255.0);
#else
	output.Normal.w = input.Color.w;
#endif
#else
	output.Normal.w = 1.0;
#endif
	output.Normal.xyz = viewNormal;
#endif

#if MERGED_DEPTH_UV
	output.DepthUv.xy = output.Position.zw;
	output.DepthUv.zw = texCoord;
#elif UV_OUTPUT
	output.TexCoord.xy = texCoord;
#if NORMAL_TARGET
	output.TexCoord.z = max(normalFadeDepth / 750.0 + 0.8, 1.0);
	output.TexCoord.w = normalFalloff;
#else
	output.TexCoord.zw = 1.0;
#endif
#endif

#if PLAIN_DEPTH
	output.Depth = output.Position.zw;
#elif PB_DEPTH
	output.Depth.xy = output.Position.zw;
	output.Depth.z = paraboloidSlice;
#elif U_DEBUG_SHADOWSPLIT
	output.ShadowSplit = output.Position.z;
#endif

#if COLOR_OUTPUT
#if U_MERGE_INSTANCED
#if U_TREE_ANIM
	output.Color.w = 1.0;
#else
	float colorAlpha = (packedColor >> 24) / 255.0;
	output.ColorAlpha = colorAlpha;
	output.Color.w = colorAlpha;
#endif
	output.Color.x = (packedColor & 0xff) / 255.0;
	output.Color.y = ((packedColor >> 8) & 0xff) / 255.0;
	output.Color.z = ((packedColor >> 16) & 0xff) / 255.0;
#elif U_TREE_ANIM
	output.Color = float4(input.Color.xyz, 1.0);
#else
	output.ColorAlpha = input.Color.w;
	output.Color = input.Color;
#endif
#endif

#if U_CLIP_VOLUME
	float3 clipVolumePosition = float3(
		dot(world[0], position),
		dot(world[1], position),
		dot(world[2], position));
	output.ClipDistance =
		length((clipVolumePosition - ClipVolumeCenter) / ClipVolumeRadius) - 1.0;
#endif

#endif

	return output;
}
#else
#error "Select one shader family stage or kernel."
#endif
