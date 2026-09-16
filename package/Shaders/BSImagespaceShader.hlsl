#if defined(IMAGESPACE_PASSTHROUGH_VS_SOURCE)
struct VS_INPUT
{
	float4 Position : POSITION0;
	float2 TexCoord : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
// Unwritten by design: this axis widens the output signature and nothing else.
#ifdef IMAGESPACE_PASSTHROUGH_TEXCOORD1
	float4 TexCoord1 : TEXCOORD1;
#endif
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	vsout.Position = float4(input.Position.xyz, 1.0);
	vsout.TexCoord = input.TexCoord;
	return vsout;
}
#elif defined(IMAGESPACE_XYQUAD_VS_SOURCE)
struct VS_INPUT
{
	float4 Position : POSITION0;
#ifdef IMAGESPACE_XYQUAD_PACKED
	float4 Color : COLOR0;
#endif
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
#ifdef IMAGESPACE_XYQUAD_PACKED
	float3 TexCoord : TEXCOORD0;
#endif
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	vsout.Position = float4(input.Position.xy, 0.0, 1.0);
#ifdef IMAGESPACE_XYQUAD_PACKED
	vsout.TexCoord = float3(input.Position.zw, 1.0);
#endif
	return vsout;
}
#elif defined(IMAGESPACE_TAPARRAY_PS_SOURCE)
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
#elif defined(IMAGESPACE_GAMMA_PS_SOURCE)
// Imagespace gamma-curve pixel transform: one sampler-bound tap of one view, an optional constant coordinate scale, then one power curve over the three color lanes.
// Axes UV_PRE_TRANSFORM identity or constant_scale and CURVE_EXPONENT as a measured decimal literal; names are authored because the containers ship no reflection chunk.

// An absent axis define would otherwise read as zero and silently drop the coordinate scale or flatten the curve.
#if !defined(IMAGESPACE_GAMMA_UV_PRE_TRANSFORM) || IMAGESPACE_GAMMA_UV_PRE_TRANSFORM < 0 || IMAGESPACE_GAMMA_UV_PRE_TRANSFORM > 1 || !defined(IMAGESPACE_GAMMA_CURVE_EXPONENT)
#error "define IMAGESPACE_GAMMA_UV_PRE_TRANSFORM as zero or one and IMAGESPACE_GAMMA_CURVE_EXPONENT as the measured decimal curve literal"
#endif

#if IMAGESPACE_GAMMA_UV_PRE_TRANSFORM
cbuffer CoordinateParameters : register(b2)
{
	float4 CoordinateScale; // Scale in .xy; .zw are unread by every measured cell and only fill the single declared vector.
};
#endif

Texture2D<float4> Source : register(t0);
SamplerState SourceSampler : register(s0);

float4 main(float4 position : SV_POSITION, float2 texCoord : TEXCOORD0) : SV_Target
{
#if IMAGESPACE_GAMMA_UV_PRE_TRANSFORM
	texCoord *= CoordinateScale.xy;
#endif
	float4 tap = Source.Sample(SourceSampler, texCoord);
	// The curve covers the three color lanes only; the fetched alpha reaches the output as a plain move.
	return float4(pow(tap.rgb, IMAGESPACE_GAMMA_CURVE_EXPONENT), tap.a);
}
#elif defined(IMAGESPACE_INDEXREBASE_CS_SOURCE)
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
#elif defined(VLS_SLICE_INTERP_SOURCE) || defined(VLS_SLICE_COORD_SOURCE)
#if defined(VLS_SLICE_INTERP_SOURCE)

cbuffer PerGeometry : register(b2)
{
	float4 Unused0;
	float4 SliceDimensions;
};

Texture2D<float> SliceCoordinates : register(t3);

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

uint2 main(PS_INPUT input) : SV_Target0
{
	uint2 texel = uint2(input.TexCoord.yx * SliceDimensions.zw);
	uint row = texel.x;
	uint block = texel.y >> 4;
	uint base = texel.y & ~15u;
	uint selected = texel.y & 15u;

	float coordinates[16];
	[unroll]
	for (uint i = 0; i < 16; ++i) {
		coordinates[i] = SliceCoordinates.Load(int3(block * 16 + i, row, 0));
	}

	uint lower = selected;
	[loop]
	while (lower > 0) {
		if (abs(coordinates[lower - 1] - coordinates[lower]) > 4.0) {
			break;
		}
		--lower;
	}

	uint upper = selected;
	[loop]
	while (upper < 15) {
		if (abs(coordinates[upper] - coordinates[upper + 1]) > 4.0) {
			break;
		}
		++upper;
	}

	uint2 range = uint2(lower, upper);
	if (lower == selected || upper == selected) {
		range = selected;
	}
	return range + base;
}

#elif defined(VLS_SLICE_COORD_SOURCE)

cbuffer PerGeometry : register(b2)
{
	float4 InterpParams;
	float4 SliceParams;
};

Texture2D<float4> SceneDepth : register(t1);
SamplerState SceneDepthSampler : register(s1);

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

void main(
	PS_INPUT input,
	out float4 outputCoordinate : SV_Target0,
	out float4 outputDepth : SV_Target1)
{
	float slice = saturate(input.TexCoord.y - 0.5 / SliceParams.z) * 4.0;
	uint side = (uint)min(floor(slice), 3.0);
	float alongSide = frac(slice) * 2.0 - 1.0;
	float4 oneHot = (float4)(side == uint4(0, 1, 2, 3));
	float4 basisX = float4(-1.0, alongSide, 1.0, -alongSide);
	float4 basisY = float4(-alongSide, -1.0, alongSide, 1.0);
	float2 boundary = float2(dot(basisX, oneHot), dot(basisY, oneHot));

	bool useCenter = InterpParams.x > 0.0;
	float2 fromCenter = boundary - SliceParams.xy;
	float distanceToBoundary = length(fromCenter);
	float2 direction = fromCenter / distanceToBoundary;
	bool4 nonParallel = abs(direction.xyxy) > 0.00001;
	float4 edge =
		float4(-1.0, -1.0, 1.0, 1.0) - SliceParams.xyxy;
	float4 denominator = direction.xyxy + (nonParallel ? 0.0 : 1.0);
	float4 edgeDistance = edge / denominator;
	bool4 beforeBoundary =
		nonParallel && edgeDistance < (distanceToBoundary - 0.0001);
	float4 intersections =
		(float4)beforeBoundary * edgeDistance -
		(float4)!beforeBoundary * 3.402823466e+38;
	float extension = max(
		max(max(max(intersections.x, 0.0), intersections.y), intersections.z),
		intersections.w);
	float2 coordinate = useCenter
		? SliceParams.xy
		: direction * extension + SliceParams.xy;
	if (any(abs(coordinate) > 1.0001)) {
		discard;
	}

	float halfPixel = 0.5 / SliceParams.w;
	float interpolation = input.TexCoord.x - halfPixel;
	float lastPixelScale = SliceParams.w / (SliceParams.w - 1.0);
	interpolation = saturate(interpolation * lastPixelScale);
	float2 resultDelta = boundary - coordinate;
	float4 result =
		float4(coordinate, 0.0, 0.0) +
		interpolation * float4(resultDelta, 0.0, 0.0);

	if (any(abs(result.xy) > 1.0001)) {
		discard;
	}

	float2 sceneUv = result.xy * float2(0.5, -0.5) + 0.5;
	float sceneDepth = SceneDepth.Sample(SceneDepthSampler, sceneUv).x;
	float depthScale = InterpParams.z / (InterpParams.z - InterpParams.y);
	float depthNumerator = -depthScale * InterpParams.y;
	outputDepth = float4(
		depthNumerator / (sceneDepth - depthScale),
		0.0,
		0.0,
		0.0);
	outputCoordinate = result;
}

#else
#error Select one VLS slice-scatter pixel route.
#endif
#else
#error "Select one shader family stage or kernel."
#endif
