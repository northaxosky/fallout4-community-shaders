// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef CSHADER
cbuffer VBCopyConstantBuffer : register(b0)
{
	uint DestinationOffset;
	uint SourceStride;
	uint DestinationStride;
	uint ShapeIndex;
	uint FirstVertex;
	uint VertexCount;
};

ByteAddressBuffer SourceVertices : register(t0);
RWByteAddressBuffer DestinationVertices : register(u0);
#	if !defined(POSITION_AS32BITS)
RWStructuredBuffer<uint> ShapeIndices : register(u1);
#	endif

[numthreads(GROUP_DIM_X, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
	if (threadId.x < VertexCount)
	{
		uint2 offsets = uint2(SourceStride, DestinationStride) * threadId.x;
		uint source = offsets.x;
		uint destination = offsets.y + DestinationOffset;
#	if defined(POSITION_AS32BITS)
		DestinationVertices.Store(DestinationStride * FirstVertex + offsets.y, ShapeIndex);
#	else
		ShapeIndices[threadId.x + FirstVertex] = ShapeIndex;
#	endif
		uint blocks = SourceStride >> 4;
		uint words = (SourceStride >> 2) & 3;
		for (uint block = 0; block < blocks; ++block)
		{
			DestinationVertices.Store4(destination, SourceVertices.Load4(source));
			source += 16;
			destination += 16;
		}
		for (uint word = 0; word < words; ++word)
		{
			DestinationVertices.Store(destination, SourceVertices.Load(source));
			source += 4;
			destination += 4;
		}
	}
}
#endif
