// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef CSHADER
cbuffer IBOffsetConstantBuffer : register(b0)
{
	uint4 Offsets;
};

ByteAddressBuffer SourceIndices : register(t0);
RWByteAddressBuffer DestinationIndices : register(u0);

[numthreads(GROUP_DIM_X, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
	uint index = threadId.x;
	if (index < Offsets.x)
	{
		uint bias = Offsets.w;
#	if defined(DST_NOT_ALIGNED) == defined(SRC_NOT_ALIGNED)
		// Both offsets start at the same half of a dword, so indices copy in pairs.
		uint loadBase = Offsets.y;
		uint storeBase = Offsets.z;
#	if defined(DST_NOT_ALIGNED)
		loadBase &= ~3u;
		storeBase &= ~3u;
		// The first destination dword keeps its lower index.
		if (index == 0)
		{
			uint headTarget = DestinationIndices.Load(storeBase);
			uint headHalf = SourceIndices.Load(loadBase) >> 16;
			DestinationIndices.Store(storeBase, (headTarget & 0xffffu) | ((headHalf + bias) << 16));
		}
		else
#	endif
		{
			uint loadAddress = loadBase + index * 4;
			uint storeAddress = storeBase + index * 4;
			uint packedValue = SourceIndices.Load(loadAddress);
			uint lowerHalf = packedValue & 0xffffu;
			uint upperHalf = packedValue & ~0xffffu;
			DestinationIndices.Store(storeAddress, (lowerHalf + bias) | (upperHalf + (bias << 16)));
		}
#	elif defined(DST_NOT_ALIGNED)
		// The destination starts at an upper half: dword i takes the upper index
		// of source dword i - 1 and the lower index of source dword i.
		uint storeBase = Offsets.z & ~3u;
		// The first destination dword keeps its lower index.
		if (index == 0)
		{
			uint headTarget = DestinationIndices.Load(storeBase);
			uint headHalf = SourceIndices.Load(Offsets.y) & 0xffffu;
			DestinationIndices.Store(storeBase, (headTarget & 0xffffu) | ((headHalf + bias) << 16));
		}
		else
		{
			uint storeAddress = storeBase + index * 4;
			uint value;
#	if !defined(EVEN_NUMBER_OF_INDICES)
			// The last destination dword keeps its upper index.
			[branch] if (index == Offsets.x - 1)
			{
				uint window = Offsets.y + index * 4 - 4;
				uint tailHalf = (SourceIndices.Load(window) >> 16) + bias;
				value = tailHalf | (DestinationIndices.Load(storeAddress) & ~0xffffu);
			}
			else
#	endif
			{
				uint window = Offsets.y + index * 4 - 4;
				uint lowerHalf = SourceIndices.Load(window) >> 16;
				uint upperHalf = SourceIndices.Load(window + 4) & 0xffffu;
				lowerHalf += bias;
				upperHalf += bias;
				value = lowerHalf | (upperHalf << 16);
			}
			DestinationIndices.Store(storeAddress, value);
		}
#	else
		// The source starts at an upper half: dword i takes the upper index of
		// source dword i and the lower index of source dword i + 1.
		uint storeAddress = Offsets.z + index * 4;
		uint loadBase = Offsets.y & ~3u;
		uint value;
#	if defined(EVEN_NUMBER_OF_INDICES)
		// The last destination dword keeps its upper index.
		[branch] if (index == Offsets.x - 1)
		{
			uint window = loadBase + index * 4;
			uint tailHalf = (SourceIndices.Load(window) >> 16) + bias;
			value = (DestinationIndices.Load(storeAddress) & ~0xffffu) | tailHalf;
		}
		else
#	endif
		{
			uint window = loadBase + index * 4;
			uint lowerHalf = SourceIndices.Load(window) >> 16;
			uint upperHalf = SourceIndices.Load(window + 4) & 0xffffu;
			lowerHalf += bias;
			upperHalf += bias;
			value = lowerHalf | (upperHalf << 16);
		}
		DestinationIndices.Store(storeAddress, value);
#	endif
	}
}
#endif
