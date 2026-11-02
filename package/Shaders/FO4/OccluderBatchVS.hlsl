// SPDX-License-Identifier: GPL-3.0-only
// Batched occlusion depth: the Utility VS math, vertices from the heaps.

cbuffer PerFrame : register(b12)
{
	row_major float4x4 CameraView;
	float4 PerFrameReserved4[4];
	row_major float4x4 CameraViewProj;
	float4 PerFrameReserved12[23];
	float3 CameraPosAdjust;
}

cbuffer Draw : register(b0)
{
	uint ChunkBase;
}

// Mirrors OccluderBatch.cpp Record.
struct Record
{
	float4 World[4];
	uint VertexOffset;
	uint IndexOffset;
	uint SlotOffset;
	uint Heaps;  // vertex | index << 4 | slot << 8 | half position << 12
	uint Stride;
	uint IndexCount;
	uint TrianglesPerSlot;
	uint InstanceBase;
};

struct MergedInstance
{
	float4 Row0;
	float4 Row1;
	float4 Row2;
	float4 PositionScale;
	float4 Unused;
};

ByteAddressBuffer Heap0 : register(t0);
ByteAddressBuffer Heap1 : register(t1);
ByteAddressBuffer Heap2 : register(t2);
ByteAddressBuffer Heap3 : register(t3);
ByteAddressBuffer Heap4 : register(t4);
ByteAddressBuffer Heap5 : register(t5);
ByteAddressBuffer Heap6 : register(t6);
ByteAddressBuffer Heap7 : register(t7);
StructuredBuffer<MergedInstance> Instances : register(t8);
StructuredBuffer<uint2> Chunks : register(t9);
StructuredBuffer<Record> Records : register(t10);

// SM 5.0 cannot index a resource array, so each load switches on the heap.
#define HEAP_SWITCH(call)  \
	switch (heap) {        \
	case 0:                \
		return Heap0.call; \
	case 1:                \
		return Heap1.call; \
	case 2:                \
		return Heap2.call; \
	case 3:                \
		return Heap3.call; \
	case 4:                \
		return Heap4.call; \
	case 5:                \
		return Heap5.call; \
	case 6:                \
		return Heap6.call; \
	default:               \
		return Heap7.call; \
	}

uint HeapLoad(uint heap, uint address){
	HEAP_SWITCH(Load(address))
}

uint2 HeapLoad2(uint heap, uint address){
	HEAP_SWITCH(Load2(address))
}

uint3 HeapLoad3(uint heap, uint address){
	HEAP_SWITCH(Load3(address))
}

// Two u16 per word, as the engine reads them.
uint LoadU16(uint heap, uint base, uint index)
{
	uint packed = HeapLoad(heap, base + 2 * index - 2 * (index & 1));
	return (index & 1) ? (packed >> 16) : (packed & 0xffff);
}

float4 main(uint vertex : SV_VertexID, uint instance : SV_InstanceID) : SV_Position
{
	uint2 chunk = Chunks[ChunkBase + instance];
	Record record = Records[chunk.x];
	uint index = chunk.y + vertex;
	// Whole triangles past the end collapse to one point and rasterize nothing.
	if (index >= record.IndexCount)
		return float4(0.0, 0.0, 0.0, 1.0);

	uint vertexHeap = record.Heaps & 0xf;
	uint indexHeap = (record.Heaps >> 4) & 0xf;

#ifdef MERGE_INSTANCED
	uint slotHeap = (record.Heaps >> 8) & 0xf;
	uint mergedVertex = LoadU16(indexHeap, record.IndexOffset, index);
	uint2 packedPosition = HeapLoad2(vertexHeap, record.VertexOffset + mergedVertex * record.Stride);
	float3 localPosition = float3(
		f16tof32(packedPosition.x),
		f16tof32(packedPosition.x >> 16),
		f16tof32(packedPosition.y));

	uint instanceSlot = index / (record.TrianglesPerSlot * 3);
	uint mergedInstance = LoadU16(slotHeap, record.SlotOffset, instanceSlot);

	MergedInstance instanceTransform = Instances[record.InstanceBase + mergedInstance];
	float3 instancePosition = float3(
		dot(localPosition, float3(instanceTransform.Row0.x, instanceTransform.Row1.x, instanceTransform.Row2.x)),
		dot(localPosition, float3(instanceTransform.Row0.y, instanceTransform.Row1.y, instanceTransform.Row2.y)),
		dot(localPosition, float3(instanceTransform.Row0.z, instanceTransform.Row1.z, instanceTransform.Row2.z)));
	float4 position = float4(
		instanceTransform.PositionScale.w * instancePosition + instanceTransform.PositionScale.xyz,
		1.0);
#else
	uint vertexIndex = LoadU16(indexHeap, record.IndexOffset, index);
	uint vertexAddress = record.VertexOffset + vertexIndex * record.Stride;
	float3 localPosition;
	if (record.Heaps & 0x1000) {
		uint2 packedPosition = HeapLoad2(vertexHeap, vertexAddress);
		localPosition = float3(
			f16tof32(packedPosition.x),
			f16tof32(packedPosition.x >> 16),
			f16tof32(packedPosition.y));
	} else {
		localPosition = asfloat(HeapLoad3(vertexHeap, vertexAddress));
	}
	float4 position = float4(localPosition, 1.0);
#endif

	float4x4 world = float4x4(record.World[0], record.World[1], record.World[2], record.World[3]);
	world[0].w -= CameraPosAdjust.x;
	world[1].w -= CameraPosAdjust.y;
	world[2].w -= CameraPosAdjust.z;

	float4x4 worldViewProj = mul(CameraViewProj, world);
	float4 clip = mul(worldViewProj, position);
	clip.z = max(clip.z, 0.0);
	return clip;
}
