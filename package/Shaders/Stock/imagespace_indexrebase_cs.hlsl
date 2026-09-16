// Imagespace index-rebase compute: one lane per destination dword, two sixteen-bit halves a lane, one rebase addend on each half.
// Axes: DST_ODD and SRC_ODD are the two base parities; only their disagreement shifts the source window, and only then does COUNT_ODD open the last-index branch.
// ABI: b0 one vector, raw t0 source, raw u0 destination, a 64x1x1 group, one thread index bounded before any access; names are authored, the containers carry no reflection chunk.

#if !defined(IMAGESPACE_INDEXREBASE_DST_ODD) || !defined(IMAGESPACE_INDEXREBASE_SRC_ODD) || !defined(IMAGESPACE_INDEXREBASE_COUNT_ODD)
#error "the three index-rebase parity axes must all be defined"
#endif

cbuffer IndexRebaseParameters : register(b0)
{
	// .x bounds the thread index, .y is the load base, .z the store base, .w the rebase addend.
	uint4 RebaseParameters;
};
ByteAddressBuffer SourceIndices : register(t0);
RWByteAddressBuffer DestinationIndices : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
	uint index = threadId.x;
	if (index < RebaseParameters.x)
	{
		uint bias = RebaseParameters.w;
		uint loadBase = RebaseParameters.y;
		uint storeBase = RebaseParameters.z;
		// An odd base sits one half inside its dword, so that raw address drops to the dword the half lies in.
#if IMAGESPACE_INDEXREBASE_DST_ODD
		storeBase &= ~3u;
#endif
		uint storeAddress = storeBase + index * 4;
#if IMAGESPACE_INDEXREBASE_SRC_ODD
		loadBase &= ~3u;
#endif
		uint loadAddress = loadBase + index * 4;
#if IMAGESPACE_INDEXREBASE_DST_ODD
		// An odd store base leaves the first dword half covered, so lane zero merges one biased half into its upper half and keeps the lower one.
		if (index == 0)
		{
			uint headTarget = DestinationIndices.Load(storeBase);
			uint headHalf = SourceIndices.Load(loadBase) & (IMAGESPACE_INDEXREBASE_SRC_ODD ? ~0u : 0xffffu);
#if IMAGESPACE_INDEXREBASE_SRC_ODD
			headHalf >>= 16;
#endif
			DestinationIndices.Store(storeBase, (headTarget & 0xffffu) | ((headHalf + bias) << 16));
		}
		else
#endif
		{
			uint value;
#if IMAGESPACE_INDEXREBASE_DST_ODD != IMAGESPACE_INDEXREBASE_SRC_ODD
			// Disagreeing parities put the pair one half out of step with its dword, so the copy reads a two dword window.
#if IMAGESPACE_INDEXREBASE_DST_ODD != IMAGESPACE_INDEXREBASE_COUNT_ODD
			// The last dword then carries one half only, so its upper half is kept and only a new lower half is merged.
			[branch] if (index == RebaseParameters.x - 1)
			{
				uint window = loadBase + index * 4 - IMAGESPACE_INDEXREBASE_DST_ODD * 4;
				uint tailHalf = (SourceIndices.Load(window) >> 16) + bias;
				value = (DestinationIndices.Load(storeAddress) & ~0xffffu) | tailHalf;
			}
			else
#endif
			{
				uint window = loadBase + index * 4 - IMAGESPACE_INDEXREBASE_DST_ODD * 4;
				uint lowerHalf = SourceIndices.Load(window) >> 16;
				uint upperHalf = SourceIndices.Load(window + 4) & 0xffffu;
				lowerHalf += bias;
				upperHalf += bias;
				value = lowerHalf | (upperHalf << 16);
			}
#else
			uint packedValue = SourceIndices.Load(loadAddress);
			uint lowerHalf = packedValue & 0xffffu;
			uint upperHalf = packedValue & ~0xffffu;
			value = (lowerHalf + bias) | (upperHalf + (bias << 16));
#endif
			DestinationIndices.Store(storeAddress, value);
		}
	}
}
