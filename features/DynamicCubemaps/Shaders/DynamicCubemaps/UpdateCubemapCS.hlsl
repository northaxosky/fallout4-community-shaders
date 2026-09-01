RWTexture2DArray<float4> DynamicCubemap : register(u0);
RWTexture2DArray<float4> DynamicCubemapRaw : register(u1);
RWTexture2DArray<float4> DynamicCubemapPosition : register(u2);

Texture2D<float> DepthTexture : register(t0);
Texture2D<float4> ColorTexture : register(t1);

SamplerState LinearSampler : register(s0);

cbuffer UpdateData : register(b0)
{
	float4 ViewToWorld[3];
	float4 CameraOrigin;
	float4 CameraPreviousOrigin;
	float4 NDCToViewMul;
	float4 NDCToViewAdd;
	row_major float4x4 InvProj;
	float4 ActiveRatioAndExtent;
}

float3 IrradianceToLinear(float3 color)
{
	return pow(abs(color), 1.6);
}

float3 GetSamplingVector(uint3 threadID)
{
	uint width;
	uint height;
	uint depth;
	DynamicCubemap.GetDimensions(width, height, depth);
	float2 st = (float2(threadID.xy) + 0.5) / float2(width, height);
	float2 uv = 2.0 * float2(st.x, 1.0 - st.y) - 1.0;
	float3 result = 0.0;
	switch (threadID.z)
	{
	case 0: result = float3(1.0, uv.y, -uv.x); break;
	case 1: result = float3(-1.0, uv.y, uv.x); break;
	case 2: result = float3(uv.x, 1.0, -uv.y); break;
	case 3: result = float3(uv.x, -1.0, uv.y); break;
	case 4: result = float3(uv.x, uv.y, 1.0); break;
	case 5: result = float3(-uv.x, uv.y, -1.0); break;
	}
	return normalize(result);
}

float3 WorldToViewDirection(float3 direction)
{
	return float3(
		dot(float3(ViewToWorld[0].x, ViewToWorld[1].x, ViewToWorld[2].x), direction),
		dot(float3(ViewToWorld[0].y, ViewToWorld[1].y, ViewToWorld[2].y), direction),
		dot(float3(ViewToWorld[0].z, ViewToWorld[1].z, ViewToWorld[2].z), direction));
}

float3 ViewToWorldDirection(float3 direction)
{
	return float3(
		dot(ViewToWorld[0].xyz, direction),
		dot(ViewToWorld[1].xyz, direction),
		dot(ViewToWorld[2].xyz, direction));
}

float2 ViewToUV(float3 viewPosition)
{
	return ((viewPosition.xy / viewPosition.z) - NDCToViewAdd.xy) /
		NDCToViewMul.xy;
}

float SmoothBumpStep(float edge0, float edge1, float value)
{
	value = 1.0 -
		abs(saturate((value - edge0) / (edge1 - edge0)) - 0.5) * 2.0;
	return value * value * (3.0 - value - value);
}

[numthreads(8, 8, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
	float3 captureDirection = GetSamplingVector(threadID);
	float3 viewDirection = WorldToViewDirection(captureDirection);
	float2 logicalUV = ViewToUV(viewDirection);
	bool inFrame = all(logicalUV >= 0.0) && all(logicalUV <= 1.0);

	if (inFrame && viewDirection.z > 0.0)
	{
		float2 textureUV = logicalUV * ActiveRatioAndExtent.xy;
		uint2 activeExtent = uint2(ActiveRatioAndExtent.zw);
		uint2 depthPixel = min(
			uint2(logicalUV * ActiveRatioAndExtent.zw),
			activeExtent - 1);
		float depth = DepthTexture.Load(int3(depthPixel, 0));
		bool isWorldPartition = depth > 0.01;
#if !defined(REFLECTIONS)
		isWorldPartition = isWorldPartition && depth != 1.0;
#endif
		if (isWorldPartition)
		{
			float localDepth = (depth - 0.01) / 0.99;
			float2 ndc = float2(
				logicalUV.x * 2.0 - 1.0,
				1.0 - logicalUV.y * 2.0);
			float4 positionH = mul(float4(ndc, localDepth, 1.0), InvProj);
			float3 positionView = positionH.xyz / positionH.w;
			if (positionView.z > 16.5)
			{
				float3 position = ViewToWorldDirection(positionView);
				float3 color =
					ColorTexture.SampleLevel(LinearSampler, textureUV, 0).rgb;
				float4 positionFinal = float4(
					position * 0.001,
					length(position) < (4096.0 * 2.5));
				float4 colorFinal = float4(IrradianceToLinear(color), 1.0);
				const float lerpFactor = 0.5;
				DynamicCubemapPosition[threadID] = lerp(
					DynamicCubemapPosition[threadID],
					positionFinal,
					lerpFactor);
				DynamicCubemapRaw[threadID] = max(
					0.0,
					lerp(
						DynamicCubemapRaw[threadID],
						colorFinal,
						lerpFactor));
				colorFinal *= sqrt(saturate(0.5 * length(position)));
				DynamicCubemap[threadID] = max(
					0.0,
					lerp(
						DynamicCubemap[threadID],
						colorFinal,
						lerpFactor));
				return;
			}
		}
	}

	float4 position = DynamicCubemapPosition[threadID];
	position.xyz =
		position.xyz +
		CameraPreviousOrigin.xyz * 0.001 -
		CameraOrigin.xyz * 0.001;
	DynamicCubemapPosition[threadID] = position;

	float4 color = DynamicCubemapRaw[threadID];
	float distance = length(position.xyz);
	float distanceFactor = SmoothBumpStep(0.0, 2.0, distance);
	if (distance < 1.0)
		distanceFactor = sqrt(distanceFactor);
	DynamicCubemap[threadID] = max(0.0, color * distanceFactor);
}
