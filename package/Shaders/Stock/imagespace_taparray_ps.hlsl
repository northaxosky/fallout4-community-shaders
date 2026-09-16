// Imagespace tap-array pixel filter: one forced dynamic loop over a cb2 tap array, one optional threshold source.
// Axes TAP_COUNT odd 3..15 and THRESHOLD_SOURCE off or on; names are authored because the containers ship no reflection chunk.

// An absent axis define would otherwise read as zero and silently drop the tap array or the second source.
#if !defined(IMAGESPACE_TAPARRAY_TAP_COUNT) || (IMAGESPACE_TAPARRAY_TAP_COUNT != 3 && IMAGESPACE_TAPARRAY_TAP_COUNT != 5 && IMAGESPACE_TAPARRAY_TAP_COUNT != 7 && IMAGESPACE_TAPARRAY_TAP_COUNT != 9 && IMAGESPACE_TAPARRAY_TAP_COUNT != 11 && IMAGESPACE_TAPARRAY_TAP_COUNT != 13 && IMAGESPACE_TAPARRAY_TAP_COUNT != 15) || !defined(IMAGESPACE_TAPARRAY_THRESHOLD_SOURCE) || IMAGESPACE_TAPARRAY_THRESHOLD_SOURCE < 0 || IMAGESPACE_TAPARRAY_THRESHOLD_SOURCE > 1
#error "define IMAGESPACE_TAPARRAY_TAP_COUNT odd within three to fifteen and IMAGESPACE_TAPARRAY_THRESHOLD_SOURCE as zero or one"
#endif

cbuffer TapParameters : register(b2)
{
	float4 ThresholdScale; // Threshold in .x and scale in .y, read only under the threshold axis.
	float4 Reserved; // Unread by every measured cell; it places the tap array at cb2[2].
	float4 Taps[IMAGESPACE_TAPARRAY_TAP_COUNT + 1]; // Offset in .xy and weight in .z per tap; the unread spare vector carries the declared count three past the tap count.
};

Texture2D<float4> Source : register(t0);
SamplerState SourceSampler : register(s0);
#if IMAGESPACE_TAPARRAY_THRESHOLD_SOURCE
Texture2D<float4> ThresholdSource : register(t1);
SamplerState ThresholdSampler : register(s1);
#endif

float4 main(float4 position : SV_POSITION, float2 texCoord : TEXCOORD0) : SV_Target
{
	float4 sum = 0.0;
#if IMAGESPACE_TAPARRAY_THRESHOLD_SOURCE
	// Alpha leaves the tap chain for the second source's Rec.709 luma, written ahead of the loop as the native order does.
	sum.a = dot(ThresholdSource.Sample(ThresholdSampler, texCoord).rgb, float3(0.2125, 0.7154, 0.0721));
#endif
	// The bound is a literal, so the measured dynamic loop has to be forced.
	[loop] for (int tap = 0; tap < IMAGESPACE_TAPARRAY_TAP_COUNT; ++tap)
	{
#if IMAGESPACE_TAPARRAY_THRESHOLD_SOURCE
		sum.rgb += max(Source.Sample(SourceSampler, texCoord + Taps[tap].xy).rgb - ThresholdScale.x, 0.0) * ThresholdScale.y * Taps[tap].z;
#else
		sum += Source.Sample(SourceSampler, texCoord + Taps[tap].xy) * Taps[tap].z;
#endif
	}
	return sum;
}
