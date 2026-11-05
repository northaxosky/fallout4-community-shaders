// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef CSHADER
cbuffer SkinnedDecalConstants : register(b0)
{
	uint VertexStride;
	uint NormalOffset;
	uint BinormalOffset;
	uint TexCoord0Offset;
	uint TexCoord1Offset;
	uint ColorOffset;
	uint SkinningOffset;
	uint EyeDataOffset;
	uint VertexCount;
	uint IndexCount;
	float4 PosAdjust;
	float4x4 Projection;
	float4 FacingVector;
};

cbuffer PerSkin : register(b10)
{
	float4 Bones[180];
};

ByteAddressBuffer IndexData : register(t0);
ByteAddressBuffer VertexData : register(t1);

struct DecalVertex
{
	float4 Position;
	float4 Projected;
};

struct VertexCopy
{
	uint Data[9];
};

RWStructuredBuffer<DecalVertex> DecalVertices : register(u0);
RWStructuredBuffer<uint> DecalIndices : register(u1);
RWStructuredBuffer<VertexCopy> Vertices : register(u2);

float3x4 BoneMatrix(uint row)
{
	return float3x4(
		Bones[row] - float4(0.0, 0.0, 0.0, PosAdjust.x),
		Bones[row + 1] - float4(0.0, 0.0, 0.0, PosAdjust.y),
		Bones[row + 2] - float4(0.0, 0.0, 0.0, PosAdjust.z));
}

uint LoadIndex(uint index)
{
	uint odd = index & 1;
	uint packed = IndexData.Load((index / 2) * 4);
	return (packed >> 16) * odd + (packed & 0xffff) * (1 - odd);
}

[numthreads(16, 1, 1)] void main(uint3 threadId : SV_DispatchThreadID) {
	if (threadId.x < VertexCount) {
		uint vertex = threadId.x * VertexStride;
		for (uint i = 0; i < (VertexStride + 3) / 4; ++i) {
			Vertices[threadId.x].Data[i] = VertexData.Load(threadId.x * VertexStride + i * 4);
		}

		uint packedXY = VertexData.Load(vertex);
		float3 position;
		position.x = f16tof32(packedXY);
		position.y = f16tof32(packedXY >> 16);
		position.z = f16tof32(VertexData.Load(threadId.x * VertexStride + 4));

		uint packedNormal = VertexData.Load(threadId.x * VertexStride + NormalOffset);
		float3 normal;
		normal.x = (packedNormal & 0xff) * (2.0 / 255.0) - 1.0;
		normal.y = ((packedNormal >> 8) & 0xff) * (2.0 / 255.0) - 1.0;
		normal.z = ((packedNormal >> 16) & 0xff) * (2.0 / 255.0) - 1.0;

		float3 weights;
		uint skinning = threadId.x * VertexStride + SkinningOffset;
		uint packedWeights = VertexData.Load(skinning);
		weights.x = f16tof32(packedWeights);
		weights.y = f16tof32(packedWeights >> 16);
		uint2 packedSkinning = VertexData.Load2(skinning + 4);
		weights.z = f16tof32(packedSkinning.x);
		uint4 rows = uint4(
						 packedSkinning.y & 0xff,
						 (packedSkinning.y >> 8) & 0xff,
						 (packedSkinning.y >> 16) & 0xff,
						 packedSkinning.y >> 24) *
		             3;

		float3x4 bone0 = BoneMatrix(rows.x);
		float3x4 bone1 = BoneMatrix(rows.y);
		float3x4 bone2 = BoneMatrix(rows.z);
		float3x4 bone3 = BoneMatrix(rows.w);
		float weight3 = 1.0 - saturate(weights.x + weights.y + weights.z);
		float3x4 boneTransform = weights.x * bone0 + weights.y * bone1 + weights.z * bone2 + weight3 * bone3;

		float4 skinnedPosition = float4(
			dot(float4(position, 1.0), boneTransform[0]),
			dot(float4(position, 1.0), boneTransform[1]),
			dot(float4(position, 1.0), boneTransform[2]),
			1.0);
		float3 skinnedNormal = normalize(float3(
			dot(normal, boneTransform[0].xyz),
			dot(normal, boneTransform[1].xyz),
			dot(normal, boneTransform[2].xyz)));
		float4 projected = mul(Projection, skinnedPosition);
		projected.z = dot(skinnedNormal, FacingVector.xyz);
		DecalVertices[threadId.x].Position = skinnedPosition;
		DecalVertices[threadId.x].Projected = projected;
	}

	uint first = threadId.x * 4;
	if (first + 4 <= IndexCount) {
		DecalIndices[first] = LoadIndex(first);
		DecalIndices[first + 1] = LoadIndex(first + 1);
		DecalIndices[first + 2] = LoadIndex(first + 2);
		DecalIndices[first + 3] = LoadIndex(first + 3);
	} else if (threadId.x < IndexCount) {
		for (uint index = first; index < first + IndexCount % 4; ++index) {
			DecalIndices[index] = LoadIndex(index);
		}
	}
}
#endif
