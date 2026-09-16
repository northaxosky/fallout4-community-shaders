#if defined(VLS_SLICE_SCATTER_RAY)

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

#elif defined(VLS_SLICE_SCATTER_INTERP)

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
