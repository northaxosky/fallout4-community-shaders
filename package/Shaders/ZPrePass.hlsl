// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef VSHADER
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
};

cbuffer Geometry : register(b2)
{
	float4 World[4];
#	if defined(MERGE_INSTANCE) && defined(ALPHA_TEST)
	float4 TexCoordOffsetScale;
	uint4 AttributeOffsets;
	uint4 MergeInstanceParams;
#	elif defined(MERGE_INSTANCE)
	uint4 AttributeOffsets;
	uint4 MergeInstanceParams;
#	elif defined(ALPHA_TEST)
	float4 TexCoordOffsetScale;
#	endif
};

#	ifdef INSTANCED
struct InstanceData
{
	float4 World[3];
	float4 Unread0[3];
	float4 TexCoordOffsetScale;
	float4 Unread1[2];
};

cbuffer Instances : register(b13)
{
	InstanceData Instance[455];
};
#	endif

#	ifdef MERGE_INSTANCE
struct ShapeData
{
	row_major float4x4 Transform;
	float4 Unread;
};

ByteAddressBuffer Vertices : register(t4);
ByteAddressBuffer Indices : register(t5);
ByteAddressBuffer ShapeIds : register(t6);
StructuredBuffer<ShapeData> Shapes : register(t7);

uint LoadUShort(ByteAddressBuffer buffer, uint index)
{
	uint odd = index & 1;
	uint packed = buffer.Load(index * 2 - 2 * odd);
	return (packed & 0xffff) * (1 - odd) + (packed >> 16) * odd;
}
#	endif

struct VS_INPUT
{
#	ifdef MERGE_INSTANCE
	uint VertexID: SV_VertexID;
#	else
	float4 Position: POSITION0;
#		ifdef ALPHA_TEST
	float2 TexCoord: TEXCOORD0;
	float4 Color: COLOR0;
#		endif
#		ifdef INSTANCED
	uint InstanceID: SV_InstanceID;
#		endif
#	endif
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;
#	ifdef ALPHA_TEST
	float3 TexCoord: TEXCOORD0;
#	endif
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT output;
#	ifdef MERGE_INSTANCE
	uint indexPosition = input.VertexID + MergeInstanceParams.z;
	uint vertex = LoadUShort(Indices, indexPosition) * MergeInstanceParams.x;
	uint2 packedPosition = Vertices.Load2(vertex);
	float3 shapePosition = float3(f16tof32(packedPosition.x), f16tof32(packedPosition.x >> 16), f16tof32(packedPosition.y));
	uint shape = LoadUShort(ShapeIds, indexPosition / (MergeInstanceParams.y * 3));
	float4x4 transform = Shapes[shape].Transform;
	float4 localPosition = float4(transform._m33 * mul(shapePosition, (float3x3)transform) + transform._m30_m31_m32, 1.0);
#	else
	float4 localPosition = float4(input.Position.xyz, 1.0);
#	endif
	float4 worldPosition;
#	ifdef INSTANCED
	InstanceData instance = Instance[input.InstanceID];
	worldPosition.x = dot(float4(instance.World[0].xyz, instance.World[0].w - PosAdjust.x), localPosition);
	worldPosition.y = dot(float4(instance.World[1].xyz, instance.World[1].w - PosAdjust.y), localPosition);
	worldPosition.z = dot(float4(instance.World[2].xyz, instance.World[2].w - PosAdjust.z), localPosition);
	worldPosition.w = 1.0;
#	else
	worldPosition.x = dot(float4(World[0].xyz, World[0].w - PosAdjust.x), localPosition);
	worldPosition.y = dot(float4(World[1].xyz, World[1].w - PosAdjust.y), localPosition);
	worldPosition.z = dot(float4(World[2].xyz, World[2].w - PosAdjust.z), localPosition);
	worldPosition.w = dot(World[3], localPosition);
#	endif
	output.Position.x = dot(WorldToClip[0], worldPosition);
	output.Position.y = dot(WorldToClip[1], worldPosition);
	output.Position.z = dot(WorldToClip[2], worldPosition);
	output.Position.w = dot(WorldToClip[3], worldPosition);
// FO4: terrain blending biases terrain depth; mirrors upstream Utility.hlsl.
#	ifdef OFFSET_DEPTH
	output.Position.z += 10.0;
#	endif
#	ifdef ALPHA_TEST
#		ifdef MERGE_INSTANCE
	uint packedTexCoord = Vertices.Load(vertex + AttributeOffsets.z);
	uint packedColor = Vertices.Load(vertex + AttributeOffsets.w);
	output.TexCoord.z = (packedColor >> 24) * (1.0 / 255.0);
	output.TexCoord.xy = float2(f16tof32(packedTexCoord), f16tof32(packedTexCoord >> 16)) * TexCoordOffsetScale.zw + TexCoordOffsetScale.xy;
#		elif defined(INSTANCED)
	output.TexCoord.xy = input.TexCoord * instance.TexCoordOffsetScale.zw + instance.TexCoordOffsetScale.xy;
	output.TexCoord.z = input.Color.w;
#		else
	output.TexCoord.xy = input.TexCoord * TexCoordOffsetScale.zw + TexCoordOffsetScale.xy;
	output.TexCoord.z = input.Color.w;
#		endif
#	endif
	return output;
}
#endif

#ifdef PSHADER
#	include "FO4/FO4ShaderData.hlsli"

cbuffer AlphaTest : register(b2)
{
	float2 AlphaTestParams;
};

Texture2D<float4> BaseTexture : register(t0);
SamplerState BaseSampler : register(s0);

struct PS_INPUT
{
	float4 Position: SV_POSITION0;
	float3 TexCoord: TEXCOORD0;
};

void main(PS_INPUT input)
{
	float alpha = SAMPLE_MATERIAL(BaseTexture, BaseSampler, input.TexCoord.xy).w;
	float vertexAlpha = saturate(input.TexCoord.z + AlphaTestParams.y);
	clip(alpha * vertexAlpha - AlphaTestParams.x);
}
#endif
