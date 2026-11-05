// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef CSHADER
cbuffer MergeInstancedVertexBufferCopyConstants : register(b0)
{
	uint VertexCount;
	uint DestinationOffset;
	uint SourceStride;
	uint DestinationStride;
};

ByteAddressBuffer SourceVertices : register(t0);
RWByteAddressBuffer DestinationVertices : register(u0);

[numthreads(GROUP_DIM_X, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
	if (threadId.x < VertexCount)
	{
		uint destination = threadId.x * DestinationStride + DestinationOffset;
		uint offset = 0;
		for (uint block = 0; block < SourceStride >> 4; ++block)
		{
			uint4 data = SourceVertices.Load4(threadId.x * SourceStride + offset);
			DestinationVertices.Store4(destination + offset, data);
			offset += 16;
		}
		for (; offset < SourceStride; offset += 4)
		{
			uint data = SourceVertices.Load(threadId.x * SourceStride + offset);
			DestinationVertices.Store(destination + offset, data);
		}
#	if defined(ADD_COLOR_ATTRIBUTE)
		DestinationVertices.Store(destination + SourceStride, 0xffffffff);
#	endif
	}
}
#endif
