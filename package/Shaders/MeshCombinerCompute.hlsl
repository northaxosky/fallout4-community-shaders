// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef CSHADER
cbuffer MeshCombinerConstants : register(b0)
{
	uint NormalOffset;
	uint NormalOutputOffset;
	uint BinormalOffset;
	uint BinormalOutputOffset;
	uint ColorOffset;
	uint ColorOutputOffset;
	uint PositionOffset;
	uint VertexStride;
	float3 BoundCenter;
	uint VertexCount;
};

struct ShapeTransform
{
	// Rows 0-2 the rotation rows, row 3 the translation and (w) the scale,
	// stored column-major, so World's columns are the stored rows.
	float4x4 World;
	// x colour scale.
	float4 Params;
};

StructuredBuffer<ShapeTransform> ShapeTransforms : register(t0);
#	if !defined(POSITION_AS32BITS)
StructuredBuffer<uint> ShapeIndices : register(t1);
#	endif
RWByteAddressBuffer Vertices : register(u0);

float4 UnpackPosition(uint2 packed)
{
	float4 position;
	position.x = f16tof32(packed.x);
	position.y = f16tof32(packed.x >> 16);
	position.z = f16tof32(packed.y);
	position.w = f16tof32(packed.y >> 16);
	return position;
}

float4 UnpackNormal(uint packed)
{
	float4 normal;
	normal.x = (packed & 0xff) * (2.0 / 255.0) - 1.0;
	normal.y = ((packed >> 8) & 0xff) * (2.0 / 255.0) - 1.0;
	normal.z = ((packed >> 16) & 0xff) * (2.0 / 255.0) - 1.0;
	normal.w = (packed >> 24) * (2.0 / 255.0) - 1.0;
	return normal;
}

uint PackNormal(float4 normal)
{
	uint packed = (uint)(normal.x * 127.5 + 127.5);
	packed |= (uint)(normal.y * 127.5 + 127.5) << 8;
	packed |= (uint)(normal.z * 127.5 + 127.5) << 16;
	packed |= (uint)(normal.w * 127.5 + 127.5) << 24;
	return packed;
}

#	if defined(COLOR_REMAP)
float4 UnpackColor(uint packed)
{
	float4 color;
	color.x = (packed & 0xff) * (1.0 / 255.0);
	color.y = ((packed >> 8) & 0xff) * (1.0 / 255.0);
	color.z = ((packed >> 16) & 0xff) * (1.0 / 255.0);
	color.w = (packed >> 24) * (1.0 / 255.0);
	return color;
}

uint PackColor(float4 color)
{
	uint packed = (uint)(color.x * 255.0);
	packed |= (uint)(color.y * 255.0) << 8;
	packed |= (uint)(color.z * 255.0) << 16;
	packed |= (uint)(color.w * 255.0) << 24;
	return packed;
}
#	endif

[numthreads(GROUP_DIM_X, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
	uint index = groupId.x * GROUP_DIM_X + groupThreadId.x;
	if (index < VertexCount)
	{
#	if defined(POSITION_AS32BITS)
		uint vertex = VertexStride * index;
		uint shape = Vertices.Load(index * VertexStride);
#	else
		uint shape = ShapeIndices[index];
		uint vertex = index * VertexStride;
#	endif
		uint2 packedPosition = Vertices.Load2(vertex + PositionOffset);
#	if defined(NORMAL_ATTRIBUTE)
		uint packedNormal = Vertices.Load(vertex + NormalOffset);
#	endif
#	if defined(BINORMAL_ATTRIBUTE)
		uint packedBinormal = Vertices.Load(vertex + BinormalOffset);
#	endif
#	if defined(COLOR_REMAP) && defined(COLOR_ATTRIBUTE)
		uint packedColor = Vertices.Load(vertex + ColorOffset);
#	endif
		float4 position = UnpackPosition(packedPosition);
#	if defined(NORMAL_ATTRIBUTE)
		float4 normal = UnpackNormal(packedNormal);
#	endif
#	if defined(BINORMAL_ATTRIBUTE)
		float4 binormal = UnpackNormal(packedBinormal);
#	endif
#	if defined(COLOR_REMAP)
#	if defined(COLOR_ATTRIBUTE)
		float4 color = UnpackColor(packedColor);
#	else
		float4 color = 1.0;
#	endif
#	endif
		float3 bitangent = 0;
#	if defined(NORMAL_ATTRIBUTE) || defined(BINORMAL_ATTRIBUTE)
		bitangent.x = position.w;
#	endif
#	if defined(NORMAL_ATTRIBUTE)
		bitangent.y = normal.w;
#	endif
#	if defined(BINORMAL_ATTRIBUTE)
		bitangent.z = binormal.w;
#	endif
		ShapeTransform transform = ShapeTransforms[shape];
		float3x3 rotation = (float3x3)transform.World;
		float3 worldPosition = transform.World._m33 * mul(rotation, position.xyz) + transform.World._m03_m13_m23 - BoundCenter;
#	if defined(NORMAL_ATTRIBUTE)
		float3 worldNormal = mul(rotation, normal.xyz);
#	endif
#	if defined(BINORMAL_ATTRIBUTE)
		float3 worldBinormal = mul(rotation, binormal.xyz);
#	endif
		float3 worldBitangent = mul(rotation, bitangent);
#	if defined(COLOR_REMAP)
		color.rgb *= transform.Params.x;
#	endif
#	if defined(POSITION_AS32BITS)
		Vertices.Store4(vertex, asuint(float4(worldPosition, worldBitangent.x)));
#	else
		uint3 halves = f32tof16(worldPosition);
		Vertices.Store2(vertex, uint2(halves.x + (halves.y << 16), halves.z + (f32tof16(worldBitangent.x) << 16)));
#	endif
#	if defined(NORMAL_ATTRIBUTE)
		Vertices.Store(vertex + NormalOutputOffset, PackNormal(float4(worldNormal, worldBitangent.y)));
#	endif
#	if defined(BINORMAL_ATTRIBUTE)
		Vertices.Store(vertex + BinormalOutputOffset, PackNormal(float4(worldBinormal, worldBitangent.z)));
#	endif
#	if defined(COLOR_REMAP)
		Vertices.Store(vertex + ColorOutputOffset, PackColor(color));
#	endif
	}
}
#endif
