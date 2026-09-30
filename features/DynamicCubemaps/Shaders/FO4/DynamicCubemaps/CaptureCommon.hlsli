#ifndef DYNAMIC_CUBEMAPS_CAPTURE_COMMON_HLSLI
#define DYNAMIC_CUBEMAPS_CAPTURE_COMMON_HLSLI

#include "DynamicCubemaps/CubemapCommon.hlsli"
#include "FO4/Depth.hlsli"
#include "FO4/FO4ShaderData.hlsli"

RWTexture2DArray<float4> DynamicCubemap : register(u0);
RWTexture2DArray<float4> DynamicCubemapRaw : register(u1);
RWTexture2DArray<float4> DynamicCubemapPosition : register(u2);

struct CaptureLightingState
{
	float ReferenceLuminance;
	uint PendingResetMask;
	uint Reset;
	uint Initialized;
};

RWStructuredBuffer<CaptureLightingState> LightingState : register(u3);

Texture2D<float> DepthTexture : register(t0);
Texture2D<float4> ColorTexture : register(t1);
// FO4 has no diffuse-only main target, so geometry radiance is rebuilt from the composite's diffuse inputs.
Texture2D<float4> AlbedoTexture : register(t2);
Texture2D<float4> DiffuseTexture : register(t3);
Texture2D<float4> TiledDiffuseTexture : register(t4);
Texture2D<float4> EmissiveTexture : register(t5);
SamplerState LinearSampler : register(s0);

cbuffer UpdateData : register(b0)
{
	float3 CameraPreviousPosAdjust2;
	uint CaptureIndex;
	float CaptureDeltaTime;
	uint ResetCapture;
	uint2 UpdatePadding;
	// FO4 host camera: validated b12 world camera and world-scene inverse projection.
	float4 CameraPosAdjust;
	float4 ViewToWorld[3];
	row_major float4x4 InvProj;
}

static const float CaptureHistoryLifetime = 30.0;

float GetCaptureTime()
{
	return floor(fmod(SharedData::Timer, 64.0) * 16.0) / 16.0;
}

bool CaptureHistoryExpired(float lastSeen)
{
	return CaptureDeltaTime >= CaptureHistoryLifetime || fmod(GetCaptureTime() - lastSeen + 64.0, 64.0) >= CaptureHistoryLifetime;
}

float3 GetSamplingVector(uint3 texel)
{
	uint width, height, faces;
	DynamicCubemap.GetDimensions(width, height, faces);
	float2 st = (texel.xy + 0.5) / float2(width, height);
	float2 uv = 2.0 * float2(st.x, 1.0 - st.y) - 1.0;
	return DynamicCubemaps::CubeFaceDirection(texel.z, uv);
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

float2 ViewToUV(float3 viewDirection)
{
	return FrameBuffer::ViewToUV(viewDirection);
}

bool IsOutsideFrame(float2 uv)
{
	return any(uv < 0.0) || any(uv > 1.0);
}

bool SampleCapture(uint3 texel, out float3 position, out float3 color, out float2 uv)
{
	position = 0.0;
	color = 0.0;
	// FO4 views down +Z, so upstream's (-s, z < 0) test becomes (s, z > 0); both select the same screen texel.
	float3 viewDirection = WorldToViewDirection(GetSamplingVector(texel));
	uv = ViewToUV(viewDirection);
	if (viewDirection.z <= 0.0 || !all(isfinite(uv)) || IsOutsideFrame(uv))
		return false;

	float2 sampleUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(uv);
	float depth = DepthTexture.SampleLevel(LinearSampler, sampleUV, 0);
#if defined(REFLECTIONS)
	if (FO4Depth::IsFirstPerson(depth))
#else
	if (depth == 1.0 || FO4Depth::IsFirstPerson(depth))
#endif
		return false;

	float3 positionView;
	// FO4's world projection has an infinite far plane, so sky depth 1.0 is placed on the camera far plane as Skyrim's finite one does.
	if (depth >= 1.0) {
		positionView = viewDirection / viewDirection.z * SharedData::CameraData.x;
	} else {
		float4 positionCS = mul(float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), FO4Depth::ProjectionDepth(depth, false), 1.0), InvProj);
		positionView = positionCS.xyz / positionCS.w;
	}
	if (positionView.z <= 16.5)
		return false;

	position = ViewToWorldDirection(positionView) * 0.001;
	float3 radiance = ColorTexture.SampleLevel(LinearSampler, sampleUV, 0).rgb;
	if (depth < 1.0) {
		float3 diffuse = DiffuseTexture.SampleLevel(LinearSampler, sampleUV, 0).rgb + TiledDiffuseTexture.SampleLevel(LinearSampler, sampleUV, 0).rgb;
		radiance = 3.0 * AlbedoTexture.SampleLevel(LinearSampler, sampleUV, 0).rgb * diffuse + EmissiveTexture.SampleLevel(LinearSampler, sampleUV, 0).rgb;
	}
	color = DynamicCubemaps::IrradianceToLinear(radiance);
	if (!all(isfinite(position)) || !all(isfinite(color)))
		return false;
	color = clamp(color, 0.0, 65504.0);
	return true;
}

float3 AdjustCapturePosition(float3 position)
{
	return position + (CameraPreviousPosAdjust2 - CameraPosAdjust.xyz) * 0.001;
}

bool CaptureGeometryMatches(float3 previousPosition, float3 position)
{
	return length(previousPosition - position) <= max(0.032, length(position) * 0.03);
}

#endif
