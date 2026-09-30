#include "DynamicCubemaps/CubemapCommon.hlsli"
#include "FO4/FO4ShaderData.hlsli"

TextureCube<float4> EnvCaptureTexture : register(t0);
TextureCube<float4> ReflectionsTexture : register(t1);
TextureCube<float4> DefaultCubemap : register(t2);

RWTexture2DArray<float4> EnvInferredTexture : register(u0);

SamplerState LinearSampler : register(s0);

float3 GetSamplingVector(uint3 ThreadID, in RWTexture2DArray<float4> OutputTexture)
{
	float width = 0.0f;
	float height = 0.0f;
	float depth = 0.0f;
	OutputTexture.GetDimensions(width, height, depth);

	float2 st = ThreadID.xy / float2(width, height);
	float2 uv = 2.0 * float2(st.x, 1.0 - st.y) - 1.0;
	return DynamicCubemaps::CubeFaceDirection(ThreadID.z, uv);
}

[numthreads(8, 8, 1)] void main(uint3 ThreadID : SV_DispatchThreadID) {
	float3 uv = GetSamplingVector(ThreadID, EnvInferredTexture);
	float4 color = EnvCaptureTexture.SampleLevel(LinearSampler, uv, 0);

	float mipLevel = 0.0;

#if !defined(REFLECTIONS)
	float k = 1.5;
	float brightness = k;
#endif

	while (color.w < 1.0 && mipLevel <= 9) {
		mipLevel++;

		float4 tempColor = 0.0;
		if (mipLevel < 9) {
			tempColor = EnvCaptureTexture.SampleLevel(LinearSampler, uv, mipLevel);
		} else {
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(-1.0, 0.0, 0.0), 10);
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(1.0, 0.0, 0.0), 10);
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(0.0, -1.0, 0.0), 10);
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(0.0, 1.0, 0.0), 10);
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(0.0, 0.0, -1.0), 10);
			tempColor += EnvCaptureTexture.SampleLevel(LinearSampler, float3(0.0, 0.0, 1.0), 10);
		}

#if !defined(REFLECTIONS)
		tempColor *= brightness;
		brightness *= k;
#endif

		if ((color.w + tempColor.w) > 1.0) {
			mipLevel -= color.w;
			float alphaDiff = 1.0 - color.w;
			tempColor.xyzw *= alphaDiff / tempColor.w;
			color.xyzw += tempColor;
			break;
		} else {
			color.xyzw += tempColor;
		}
	}

#if defined(REFLECTIONS)
	color.rgb = lerp(color.rgb, DynamicCubemaps::IrradianceToLinear(ReflectionsTexture.SampleLevel(LinearSampler, uv, 0.0).rgb), saturate(mipLevel / 8.0));
#else
	if (color.a <= 0.0001)
		// FO4 cubemap inference consumes linear DALC.
		color.rgb = DynamicCubemaps::IrradianceToLinear(FO4SharedData::GetAmbientLinear(uv));
	color.rgb = lerp(color.rgb, color.rgb * DefaultCubemap.SampleLevel(LinearSampler, uv, 0.0).xyz, saturate(mipLevel / 8.0));
#endif

	color.rgb = DynamicCubemaps::IrradianceToGamma(color.rgb);
	EnvInferredTexture[ThreadID] = max(0, color);
}