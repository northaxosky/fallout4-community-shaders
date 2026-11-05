// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef CSHADER
cbuffer DismemberConstants : register(b0)
{
	float4 Rotation[3];
	float4 PlanePoint;
	float4 PlaneNormal;
	uint VertexStride;
	uint NormalOffset;
	uint BinormalOffset;
	uint TexCoord0Offset;
	uint TexCoord1Offset;
	uint ColorOffset;
	uint SkinningOffset;
	uint EyeDataOffset;
	uint OutputVertexStride;
	uint3 Pad0;
	uint ThreadCount;
	uint SegmentCount;
	uint IndexCount;
	uint Pad1;
	uint4 Segments[12];
};

struct BoneMaskData
{
	uint Bits[8];
};

struct DismemberResult
{
	float4 BoundMin;
	float4 BoundMax;
	uint4 IndexCounts;
	uint4 SideCounts;
	uint BoneMask[8];
	uint4 VertexCount;
};

ByteAddressBuffer SourceIndices : register(t0);
ByteAddressBuffer SourceVertices : register(t1);
RWStructuredBuffer<DismemberResult> Results : register(u0);
RWByteAddressBuffer Vertices : register(u1);
RWByteAddressBuffer Indices : register(u2);
RWByteAddressBuffer SplitIndices0 : register(u3);
RWByteAddressBuffer DiscardedIndices : register(u4);
RWByteAddressBuffer SplitIndices1 : register(u5);
RWByteAddressBuffer PositiveIndices : register(u6);
RWByteAddressBuffer NegativeIndices : register(u7);

struct VertexData
{
	float3 Position;
	float3 Bitangent;
	float3 Normal;
	float3 Binormal;
	float2 TexCoord;
	uint4 Color;
	float4 ColorF;
	float4 Weights;
	uint4 Bones;
	float Distance;
};

uint LoadIndex(uint index)
{
	uint odd = index & 1;
	uint packed = SourceIndices.Load((index / 2) * 4);
	return (packed >> 16) * odd + (packed & 0xffff) * (1 - odd);
}

void ClearIndex(uint index)
{
	uint odd = index & 1;
	uint address = (index / 2) * 4;
	uint packed = Indices.Load(address);
	Indices.Store(address, (packed & 0xffff) * odd + (packed & 0xffff0000) * (1 - odd));
}

void WriteIndex(RWByteAddressBuffer buffer, uint position, uint value)
{
	uint address = (position / 2) * 4;
	uint packed = buffer.Load(address);
	uint odd = position & 1;
	buffer.Store(address, ((packed & 0xffff) | (value << 16)) * odd + ((packed & 0xffff0000) | (value & 0xffff)) * (1 - odd));
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

float4 UnpackColorF(uint packed)
{
	float4 color;
	color.x = (packed & 0xff) / 255.0;
	color.y = ((packed >> 8) & 0xff) / 255.0;
	color.z = ((packed >> 16) & 0xff) / 255.0;
	color.w = (packed >> 24) / 255.0;
	return color;
}

uint4 UnpackBytes(uint packed)
{
	return uint4(packed & 0xff, (packed >> 8) & 0xff, (packed >> 16) & 0xff, packed >> 24);
}

uint PackBytes(uint4 bytes)
{
	uint4 shifted = bytes << uint4(0, 8, 16, 24);
	return shifted.x | shifted.y | shifted.z | shifted.w;
}

uint PackHalf2(float2 value)
{
	return f32tof16(min(value.x, 2000.0)) | (f32tof16(min(value.y, 2000.0)) << 16);
}

float3 LoadPosition(uint index)
{
	uint packedXY = SourceVertices.Load(index * VertexStride);
	float3 position;
	position.x = f16tof32(packedXY);
	position.y = f16tof32(packedXY >> 16);
	position.z = f16tof32(SourceVertices.Load(index * VertexStride + 4));
	return position;
}

void LoadSkinning(uint index, out float4 weights, out uint4 bones)
{
	uint skinning = index * VertexStride + SkinningOffset;
	uint packedWeights = SourceVertices.Load(skinning);
	weights.x = f16tof32(packedWeights);
	weights.y = f16tof32(packedWeights >> 16);
	uint2 packedSkinning = SourceVertices.Load2(skinning + 4);
	weights.z = f16tof32(packedSkinning.x);
	weights.w = f16tof32(packedSkinning.x >> 16);
	bones = uint4(packedSkinning.y & 0xff, (packedSkinning.y >> 8) & 0xff, (packedSkinning.y >> 16) & 0xff, packedSkinning.y >> 24);
}

VertexData LoadVertex(uint index)
{
	VertexData vertex;
	uint packedXY = SourceVertices.Load(index * VertexStride);
	uint packedZW = SourceVertices.Load(index * VertexStride + 4);
	vertex.Position = float3(f16tof32(packedXY), f16tof32(packedXY >> 16), f16tof32(packedZW));
	vertex.Bitangent.x = f16tof32(packedZW >> 16);
	float4 normal = UnpackNormal(SourceVertices.Load(index * VertexStride + NormalOffset));
	vertex.Normal = normal.xyz;
	vertex.Bitangent.y = normal.w;
	float4 binormal = UnpackNormal(SourceVertices.Load(index * VertexStride + BinormalOffset));
	vertex.Binormal = binormal.xyz;
	vertex.Bitangent.z = binormal.w;
	uint packedTexCoord = SourceVertices.Load(index * VertexStride + TexCoord0Offset);
	vertex.TexCoord = float2(f16tof32(packedTexCoord), f16tof32(packedTexCoord >> 16));
	uint packedColor = SourceVertices.Load(index * VertexStride + ColorOffset);
	vertex.ColorF = UnpackColorF(packedColor);
	vertex.Color = UnpackBytes(packedColor);
	LoadSkinning(index, vertex.Weights, vertex.Bones);
	vertex.Distance = 0.0;
	return vertex;
}

void StoreVertex(uint index, VertexData vertex, bool interpolated)
{
	uint packedXY = PackHalf2(vertex.Position.xy);
	Vertices.Store2(index * OutputVertexStride, uint2(packedXY, PackHalf2(float2(vertex.Position.z, vertex.Bitangent.x))));
	uint packedNormal = PackNormal(float4(vertex.Normal, vertex.Bitangent.y));
	Vertices.Store(index * OutputVertexStride + NormalOffset, packedNormal);
	uint packedBinormal = PackNormal(float4(vertex.Binormal, vertex.Bitangent.z));
	Vertices.Store(index * OutputVertexStride + BinormalOffset, packedBinormal);
	uint packedTexCoord = PackHalf2(vertex.TexCoord);
	Vertices.Store(index * OutputVertexStride + TexCoord0Offset, packedTexCoord);
	if (ColorOffset) {
		Vertices.Store(index * OutputVertexStride + ColorOffset, (interpolated ? PackBytes((uint4)(vertex.ColorF * 255.0)) : PackBytes(vertex.Color)));
	}
	Vertices.Store(index * OutputVertexStride + EyeDataOffset, asuint(vertex.Distance));
	uint3 packedSkinning = uint3(PackHalf2(vertex.Weights.xy), PackHalf2(vertex.Weights.zw), vertex.Bones.x | (vertex.Bones.y << 8) | (vertex.Bones.z << 16) | (vertex.Bones.w << 24));
	Vertices.Store3(index * OutputVertexStride + SkinningOffset, packedSkinning);
}

void StoreTriVertex(VertexData vertex, inout uint index, inout uint vertexCount, bool interpolated)
{
	if (index == 0xffff) {
		index = vertexCount++;
		StoreVertex(index, vertex, interpolated);
	}
}

void StoreSplitVertex(VertexData vertex, inout uint index, inout uint vertexCount, inout uint2 splitCounts, inout uint2 sideCounts, bool interpolated)
{
	StoreTriVertex(vertex, index, vertexCount, interpolated);
	WriteIndex(SplitIndices0, splitCounts.x, index);
	WriteIndex(SplitIndices1, splitCounts.y, index);
	splitCounts += 1;
	WriteIndex(PositiveIndices, sideCounts.x, index);
	WriteIndex(NegativeIndices, sideCounts.y, index);
	sideCounts += 1;
}

void WritePositiveIndex(inout uint2 sideCounts, uint value)
{
	WriteIndex(PositiveIndices, sideCounts.x, value);
	sideCounts.x++;
}

void WriteNegativeIndex(inout uint2 sideCounts, uint value)
{
	WriteIndex(NegativeIndices, sideCounts.y, value);
	sideCounts.y++;
}

float PlaneDistance(float3 position)
{
	return dot(position - PlanePoint.xyz, PlaneNormal.xyz);
}

float EdgeParameter(float3 a, float3 b, float planeOffset)
{
	float3 edge = b - a;
	float denominator = dot(PlaneNormal.xyz, edge);
	bool parallel = abs(denominator) < 1.192092896e-07;
	float t = clamp(dot(PlaneNormal.xyz, PlaneNormal.xyz * planeOffset - a) * rcp(denominator), -5.0, 5.0);
	return parallel ? 0.5 : t;
}

VertexData EdgePlaneIntersect(VertexData a, VertexData b, float t)
{
	float s = (t < 0.0 || t > 1.0) ? 0.5 : t;
	VertexData result;
	result.Position = lerp(a.Position, b.Position, s);
	result.Normal = normalize(lerp(a.Normal, b.Normal, s));
	result.Binormal = normalize(lerp(a.Binormal, b.Binormal, s));
	result.Bitangent = normalize(lerp(a.Bitangent, b.Bitangent, s));
	result.TexCoord = lerp(a.TexCoord, b.TexCoord, s);
	result.Color = 0;
	result.ColorF = lerp(a.ColorF, b.ColorF, s);
	result.Weights = t < 0.5 ? a.Weights : b.Weights;
	result.Bones = t < 0.5 ? a.Bones : b.Bones;
	result.Distance = 0.0;
	return result;
}

void MarkBone(inout BoneMaskData boneMask, uint bone)
{
	uint index = bone >> 5;
	uint word = boneMask.Bits[index] | (1u << (bone & 31));
	uint select = 1u << index;
	[unroll] for (uint i = 0; i < 8; ++i)
	{
		boneMask.Bits[i] |= word * (uint)((select & (1u << i)) != 0);
	}
}

void MarkBones(inout BoneMaskData boneMask, float4 weights, uint4 bones)
{
	if (weights.x > 0.0)
		MarkBone(boneMask, bones.x);
	if (weights.y > 0.0)
		MarkBone(boneMask, bones.y);
	if (weights.z > 0.0)
		MarkBone(boneMask, bones.z);
	if (weights.w > 0.0)
		MarkBone(boneMask, bones.w);
}

[numthreads(1, 1, 1)] void main(uint3 threadId : SV_DispatchThreadID) {
	if (threadId.x < ThreadCount) {
		DismemberResult result = Results[threadId.x];
		for (uint block = 0; block < (IndexCount + 7) / 8; ++block) {
			Indices.Store4(block * 16, SourceIndices.Load4(block * 16));
		}
		float planeOffset = dot(PlaneNormal.xyz, PlanePoint.xyz);
		for (uint segment = 0; segment < SegmentCount; ++segment) {
			float4 boundMin = result.BoundMin;
			float4 boundMax = result.BoundMax;
			uint4 indexCounts = result.IndexCounts;
			uint4 sides = result.SideCounts;
			uint keptCount = indexCounts.x;
			uint discardedCount = indexCounts.z;
			uint2 splitCounts = indexCounts.yw;
			uint2 sideCounts = sides.xy;
			uint cutSegment = sides.w;
			BoneMaskData boneMask;
			boneMask.Bits = result.BoneMask;
			uint4 vertexCountRow = result.VertexCount;
			uint vertexCount = vertexCountRow.x;
			uint start = Segments[segment].x >> 16;
			uint end = (Segments[segment].x & 0xffff) * 3 + start;
			bool belowRange = Segments[segment].z == 1 && Segments[segment].w != 1;
			bool aboveRange = Segments[segment].w == 1 && Segments[segment].z != 1;
			bool segmentHasPositive = false;
			for (uint i = start; i < end; i += 3) {
				uint i0 = LoadIndex(i);
				uint i1 = LoadIndex(i + 1);
				uint i2 = LoadIndex(i + 2);
				bool anyPositive;
				bool anyNegative;
				if (!belowRange && !aboveRange) {
					float3 p0 = LoadPosition(i0);
					float3 p1 = LoadPosition(i1);
					float3 p2 = LoadPosition(i2);
					float3 distance = float3(PlaneDistance(p0), PlaneDistance(p1), PlaneDistance(p2));
					anyPositive = any(distance > 0.0);
					anyNegative = any(distance < 0.0);
					bool nearPositive = any(distance < 5.0);
					bool nearNegative = any(distance > -5.0);
					if (!Segments[segment].y) {
						if (all(abs(distance) < 0.0001) || (anyPositive && anyNegative)) {
							float3 r0 = float3(dot(Rotation[0].xyz, p0), dot(Rotation[1].xyz, p0), dot(Rotation[2].xyz, p0));
							float3 r1 = float3(dot(Rotation[0].xyz, p1), dot(Rotation[1].xyz, p1), dot(Rotation[2].xyz, p1));
							float3 r2 = float3(dot(Rotation[0].xyz, p2), dot(Rotation[1].xyz, p2), dot(Rotation[2].xyz, p2));
							boundMin.xyz = min(boundMin.xyz, min(r0, min(r1, r2)));
							boundMax.xyz = max(boundMax.xyz, max(r0, max(r1, r2)));
							VertexData vertices[6];
							vertices[0] = LoadVertex(i0);
							vertices[1] = LoadVertex(i1);
							vertices[2] = LoadVertex(i2);
							float t01 = EdgeParameter(vertices[0].Position, vertices[1].Position, planeOffset);
							float t12 = EdgeParameter(vertices[1].Position, vertices[2].Position, planeOffset);
							float t20 = EdgeParameter(vertices[2].Position, vertices[0].Position, planeOffset);
							vertices[3] = EdgePlaneIntersect(vertices[0], vertices[1], t01);
							vertices[4] = EdgePlaneIntersect(vertices[1], vertices[2], t12);
							vertices[5] = EdgePlaneIntersect(vertices[2], vertices[0], t20);
							vertices[0].Distance = distance.x;
							vertices[1].Distance = distance.y;
							vertices[2].Distance = distance.z;
							vertices[3].Distance = PlaneDistance(vertices[3].Position);
							vertices[4].Distance = PlaneDistance(vertices[4].Position);
							vertices[5].Distance = PlaneDistance(vertices[5].Position);
							if (abs(vertices[3].Distance) < 0.0001)
								vertices[3].Distance = abs(vertices[3].Distance);
							if (abs(vertices[4].Distance) < 0.0001)
								vertices[4].Distance = abs(vertices[4].Distance);
							if (abs(vertices[5].Distance) < 0.0001)
								vertices[5].Distance = abs(vertices[5].Distance);
							uint cache[6] = { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff };
							StoreSplitVertex(vertices[0], cache[0], vertexCount, splitCounts, sideCounts, false);
							StoreSplitVertex(vertices[3], cache[3], vertexCount, splitCounts, sideCounts, true);
							StoreSplitVertex(vertices[5], cache[5], vertexCount, splitCounts, sideCounts, true);
							StoreSplitVertex(vertices[3], cache[3], vertexCount, splitCounts, sideCounts, true);
							StoreSplitVertex(vertices[1], cache[1], vertexCount, splitCounts, sideCounts, false);
							StoreSplitVertex(vertices[4], cache[4], vertexCount, splitCounts, sideCounts, true);
							StoreSplitVertex(vertices[5], cache[5], vertexCount, splitCounts, sideCounts, true);
							StoreSplitVertex(vertices[4], cache[4], vertexCount, splitCounts, sideCounts, true);
							StoreSplitVertex(vertices[2], cache[2], vertexCount, splitCounts, sideCounts, false);
							StoreSplitVertex(vertices[3], cache[3], vertexCount, splitCounts, sideCounts, true);
							StoreSplitVertex(vertices[4], cache[4], vertexCount, splitCounts, sideCounts, true);
							StoreSplitVertex(vertices[5], cache[5], vertexCount, splitCounts, sideCounts, true);
							MarkBones(boneMask, vertices[0].Weights, vertices[0].Bones);
							MarkBones(boneMask, vertices[1].Weights, vertices[1].Bones);
							MarkBones(boneMask, vertices[2].Weights, vertices[2].Bones);
							ClearIndex(i);
							ClearIndex(i + 1);
							ClearIndex(i + 2);
							cutSegment = segment;
						} else if ((anyPositive && nearPositive) || (anyNegative && nearNegative)) {
							VertexData vertices[3];
							vertices[0] = LoadVertex(i0);
							vertices[1] = LoadVertex(i1);
							vertices[2] = LoadVertex(i2);
							vertices[0].Distance = distance.x;
							vertices[1].Distance = distance.y;
							vertices[2].Distance = distance.z;
							uint cache[3] = { 0xffff, 0xffff, 0xffff };
							if (anyPositive) {
								StoreTriVertex(vertices[0], cache[0], vertexCount, false);
								WritePositiveIndex(sideCounts, cache[0]);
								StoreTriVertex(vertices[1], cache[1], vertexCount, false);
								WritePositiveIndex(sideCounts, cache[1]);
								StoreTriVertex(vertices[2], cache[2], vertexCount, false);
								WritePositiveIndex(sideCounts, cache[2]);
								MarkBones(boneMask, vertices[0].Weights, vertices[0].Bones);
								MarkBones(boneMask, vertices[1].Weights, vertices[1].Bones);
								MarkBones(boneMask, vertices[2].Weights, vertices[2].Bones);
							} else {
								StoreTriVertex(vertices[0], cache[0], vertexCount, false);
								WriteNegativeIndex(sideCounts, cache[0]);
								StoreTriVertex(vertices[1], cache[1], vertexCount, false);
								WriteNegativeIndex(sideCounts, cache[1]);
								StoreTriVertex(vertices[2], cache[2], vertexCount, false);
								WriteNegativeIndex(sideCounts, cache[2]);
							}
						}
					}
				} else {
					anyPositive = belowRange;
					anyNegative = aboveRange;
				}
				if (anyPositive && !anyNegative) {
					keptCount += 3;
				}
				if (!anyPositive && anyNegative) {
					WriteIndex(DiscardedIndices, discardedCount++, i0);
					WriteIndex(DiscardedIndices, discardedCount++, i1);
					WriteIndex(DiscardedIndices, discardedCount++, i2);
					float4 weights;
					uint4 bones;
					LoadSkinning(i0, weights, bones);
					MarkBones(boneMask, weights, bones);
					LoadSkinning(i1, weights, bones);
					MarkBones(boneMask, weights, bones);
					LoadSkinning(i2, weights, bones);
					MarkBones(boneMask, weights, bones);
					ClearIndex(i);
					ClearIndex(i + 1);
					ClearIndex(i + 2);
				}
				segmentHasPositive = segmentHasPositive || anyPositive;
			}
			result.BoundMin = boundMin;
			result.BoundMax = boundMax;
			result.IndexCounts = uint4(keptCount, splitCounts.x, discardedCount, splitCounts.y);
			result.SideCounts = uint4(sideCounts, sides.z | ((uint)!segmentHasPositive << segment), cutSegment);
			result.BoneMask = boneMask.Bits;
			vertexCountRow.x = vertexCount;
			result.VertexCount = vertexCountRow;
		}
		Results[threadId.x] = result;
	}
}
#endif
