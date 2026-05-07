// FO4 port of Skyrim Community Shaders' RaymarchCS.
// Bend SSS dispatch shell. The actual ray-march kernel lives in bend_sss_gpu.hlsli (vendored, Apache 2.0).
// Drops the upstream TERRAIN_BLENDING / VR compile paths since FO4 has no equivalent.

#include "bend_sss_gpu.hlsli"

Texture2D<float>         DepthTexture       : register(t0);  // FO4's main depth SRV; permissive type covers both R24_UNORM and R32_FLOAT.
RWTexture2D<unorm float> OutputTexture      : register(u0);  // R8_UNORM screen-space shadow mask
SamplerState             PointBorderSampler : register(s0);  // Border-clamp, BorderColor = FarDepthValue

cbuffer PerFrame : register(b1)
{
	float4 LightCoordinate;       // DispatchList::LightCoordinate_Shader
	int2   WaveOffset;            // DispatchData::WaveOffset_Shader (per-dispatch)
	float  FarDepthValue;
	float  NearDepthValue;
	float2 InvDepthTextureSize;
	float2 DynamicRes;
	float  SurfaceThickness;
	float  BilinearThreshold;
	float  ShadowContrast;
};

[numthreads(WAVE_SIZE, 1, 1)] void main(int3 groupID : SV_GroupID, int groupThreadID : SV_GroupThreadID)
{
	DispatchParameters parameters;
	parameters.SetDefaults();

	parameters.LightCoordinate = LightCoordinate;
	parameters.WaveOffset = WaveOffset;
	parameters.FarDepthValue = FarDepthValue;
	parameters.NearDepthValue = NearDepthValue;
	parameters.InvDepthTextureSize = InvDepthTextureSize;
	parameters.DepthTexture = DepthTexture;
	parameters.OutputTexture = OutputTexture;
	parameters.PointBorderSampler = PointBorderSampler;

	parameters.SurfaceThickness = SurfaceThickness;
	parameters.BilinearThreshold = BilinearThreshold;
	parameters.ShadowContrast = ShadowContrast;

	parameters.DynamicRes = DynamicRes;
	parameters.UsePrecisionOffset = true;

	WriteScreenSpaceShadow(parameters, groupID, groupThreadID);
}
