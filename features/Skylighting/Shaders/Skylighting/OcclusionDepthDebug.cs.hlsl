struct DepthRange
{
    uint minimum;
    uint maximum;
};

Texture2D<float> OcclusionDepth : register(t0);

#if defined(REDUCE_DEPTH_RANGE)

RWStructuredBuffer<DepthRange> OutputRange : register(u0);

groupshared uint GroupMinimum;
groupshared uint GroupMaximum;

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    if (groupIndex == 0)
    {
        GroupMinimum = 0xFFFFFFFF;
        GroupMaximum = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint width;
    uint height;
    OcclusionDepth.GetDimensions(width, height);
    if (dispatchThreadID.x < width && dispatchThreadID.y < height)
    {
        const uint encodedDepth = asuint(OcclusionDepth[dispatchThreadID.xy]);
        InterlockedMin(GroupMinimum, encodedDepth);
        InterlockedMax(GroupMaximum, encodedDepth);
    }
    GroupMemoryBarrierWithGroupSync();

    if (groupIndex == 0)
    {
        InterlockedMin(OutputRange[0].minimum, GroupMinimum);
        InterlockedMax(OutputRange[0].maximum, GroupMaximum);
    }
}

#elif defined(NORMALIZE_DEPTH)

StructuredBuffer<DepthRange> InputRange : register(t1);
RWTexture2D<float> NormalizedDepth : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width;
    uint height;
    OcclusionDepth.GetDimensions(width, height);
    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
    {
        return;
    }

    const float minimum = asfloat(InputRange[0].minimum);
    const float maximum = asfloat(InputRange[0].maximum);
    const float range = maximum - minimum;
    const float depth = OcclusionDepth[dispatchThreadID.xy];
    NormalizedDepth[dispatchThreadID.xy] =
        range > 0.0 ? saturate((depth - minimum) / range) : 0.0;
}

#else
#error Define one occlusion depth debug pass.
#endif
