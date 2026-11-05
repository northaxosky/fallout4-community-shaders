// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer PerFrame : register(b12)
{
	row_major float4x4 WorldToView;
	row_major float4x4 ViewToClip;
	row_major float4x4 WorldToClip;
	row_major float4x4 ViewToWorld;
	row_major float4x4 ClipToWorld;
	row_major float4x4 ClipToView;
};

cbuffer ISVLSConstants : register(b2)
{
	float4 lightcenter;
	float4 lightposition;
	float4 globalvieworigin;
	float4 lightrightdir;
	float4 lightspecularscale;
	float4 lightspotdir;
	float4 vls_numsamples_zfar_znear_zscale;
	float4 localtolights;
	float4 localtolightt;
	float4 localtolightr;
	float4 localtolightq;
	float4 vlsattenuationscaleunknown;
	float4 worldtolights;
	float4 worldtolightt;
	float4 worldtolightr;
	float4 worldtolightq;
};

Texture2DArray<float4> ShadowMap : register(t1);
SamplerState ShadowMapSampler : register(s1);
Texture2D<float4> Depth : register(t2);
SamplerState DepthSampler : register(s2);
Texture2D<float4> slice_coord : register(t3);

typedef VS_OUTPUT PS_INPUT;

#	define FLT_MAX 3.402823466e+30
#	define PI 3.1415926

float4 LightTransform(float3 position)
{
	float4 p = float4(position, 1.0);
	return float4(dot(localtolights, p), dot(localtolightt, p),
		dot(localtolightr, p), dot(localtolightq, p));
}

float2 RayConeIntersect(float3 coneApex, float3 coneAxis, float cosAngle,
	float3 rayStart, float3 rayDir)
{
	rayStart -= coneApex;
	float a = dot(rayDir, coneAxis);
	float b = dot(rayDir, rayDir);
	float c = dot(rayStart, coneAxis);
	float d = dot(rayStart, rayDir);
	float e = dot(rayStart, rayStart);
	cosAngle *= cosAngle;
	float A = a * a - b * cosAngle;
	float B = 2 * (c * a - d * cosAngle);
	float C = c * c - e * cosAngle;
	float D = B * B - 4 * A * C;
	float2 t = -FLT_MAX;
	if (D > 0) {
		D = sqrt(D);
		t = (-B + sign(A) * float2(-D, +D)) / (2 * A);
		bool2 isCorrect = c + a * t > 0;
		t = t * isCorrect + !isCorrect * (-FLT_MAX);
	}
	return t;
}

void TruncateEyeRayToLightCone(float3 eyeVector, inout float3 rayStart,
	inout float3 rayEnd, inout float traceLength, out float startDistance,
	bool cameraInsideCone, float2 coneIsecs)
{
	if (cameraInsideCone) {
		rayStart = rayStart;
		startDistance = 0;
		if (coneIsecs.x > 0)
			traceLength = min(coneIsecs.x, traceLength);
		else if (coneIsecs.y > 0)
			traceLength = min(coneIsecs.y, traceLength);
		rayEnd = rayStart + traceLength * eyeVector;
	} else if (all(coneIsecs > 0)) {
		rayEnd = rayStart + min(coneIsecs.y, traceLength) * eyeVector;
		traceLength = min(coneIsecs.y, traceLength) - coneIsecs.x;
		startDistance = coneIsecs.x;
		rayStart = rayStart + coneIsecs.x * eyeVector;
	} else if (coneIsecs.y > 0) {
		traceLength = traceLength - coneIsecs.y;
		startDistance = coneIsecs.y;
		rayStart = rayStart + coneIsecs.y * eyeVector;
	} else {
		traceLength = 0;
		startDistance = 0;
		rayStart = rayStart;
		rayEnd = rayStart;
	}
	traceLength = max(traceLength, 0);
}

float ComputeScattering(float2 uv)
{
	float depth = Depth.SampleGrad(DepthSampler, uv, ddx(uv.x), ddy(uv.y)).x;
	float2 ndc = float2(uv.x, 1.0 - uv.y) * 2.0 - 1.0;
	float4 viewPosition = mul(ClipToView, float4(ndc, depth, 1.0));
	float3 rayEnd = viewPosition.xyz / viewPosition.w;
	float traceLength = length(rayEnd);
	float3 eyeVector = rayEnd / traceLength;
	float3 rayStart = 0.0;

	float3 lightPosition = mul((float3x3)WorldToView, lightcenter.xyz);
	float lightDistance = length(lightPosition);
	float3 lightDirection = lightPosition / lightDistance;
	float3 spotAxis = mul((float3x3)WorldToView, lightspotdir.xyz);
	bool cameraInsideCone = dot(-lightDirection, spotAxis) > lightrightdir.w;

	if (dot(lightDirection, eyeVector) > 0.999999) {
		float4 cameraLight = LightTransform(rayStart);
		float3 lightUVZ = cameraLight.xyz / cameraLight.w;
		lightUVZ.xy = lightUVZ.xy * 0.5 + 0.5;
		float intensity = worldtolightq.x * worldtolightq.w;
		float shadow = ShadowMap.SampleGrad(ShadowMapSampler,
									float3(lightUVZ.xy, 0), 0, 0)
		                   .x;
		float lit = shadow < lightUVZ.z ? 0.0 : 1.0;
		lit = cameraInsideCone ? lit : 1.0;
		float visible = cameraInsideCone || lightDistance < traceLength;
		return intensity * lit * visible;
	}

	float2 coneIsecs = RayConeIntersect(lightPosition, spotAxis,
		lightrightdir.w, rayStart, eyeVector);
	float startDistance;
	TruncateEyeRayToLightCone(eyeVector, rayStart, rayEnd, traceLength,
		startDistance, cameraInsideCone, coneIsecs);

	float inscattering = 0.0;
	if (traceLength >= 1.0) {
		float4 startLight = LightTransform(rayStart);
		float startRcpW = 1.0 / startLight.w;
		startLight.xyz /= startLight.w;
		float2 uv = startLight.xy * 0.5 + 0.5;
		float4 endLight = LightTransform(rayEnd);
		float endRcpW = 1.0 / endLight.w;
		endLight.xyz /= endLight.w;
		float3 startUVZ =
			startLight.xyz * float3(0.5, 0.5, 1.0) + float3(0.5, 0.5, 0.0);
		float3 endUVZ =
			endLight.xyz * float3(0.5, 0.5, 1.0) + float3(0.5, 0.5, 0.0);
		float3 traceDir = endUVZ - startUVZ;
		float traceLenUV = length(traceDir.xy);
		traceLenUV = clamp(traceLenUV, 1e-6, sqrt(2.0));
		traceDir /= traceLenUV;
		float width, height, elements;
		ShadowMap.GetDimensions(width, height, elements);
		float stepLen = (1.0 / width) / max(abs(traceDir.x), abs(traceDir.y));
		stepLen = max(traceLenUV / 256.0, stepLen);
		float marched = 0;
		float3 currUVZ = float3(uv, startLight.z);
		float prevDist = startDistance;
		[loop] while (marched < traceLenUV)
		{
			float lit = max(currUVZ.z, 1e-7) > ShadowMap.SampleGrad(
															ShadowMapSampler, float3(currUVZ.xy, 0), 0, 0)
			                                       .x ?
			                0.0 :
			                1.0;
			currUVZ += traceDir * stepLen;
			marched += stepLen;
			float f = saturate(marched / traceLenUV);
			float rcpW = lerp(startRcpW, endRcpW, f);
			float distOverW = lerp(startDistance * startRcpW,
				(startDistance + traceLength) * endRcpW, f);
			float dist = distOverW / rcpW;
			float3 toLight = dist * eyeVector - lightPosition;
			float d2 = max(dot(toLight, toLight), 1e-6);
			float lightDist = sqrt(d2);
			float density = worldtolightq.w == 0 ? 1.0 : worldtolightq.w;
			float phase = density * (50.0 / (4.0 * PI));
			float extinction = exp(density * -(dist + lightDist) * 0.00005);
			float scattering = phase * extinction;
			float contribution = scattering * ((dist - prevDist) / d2) * lit;
			contribution *= worldtolightq.x;
			float2 lightNdc = currUVZ.xy * float2(2.0, -2.0) + float2(-1.0, 1.0);
			float r = min(length(lightNdc), 1.0);
			float falloff = 1.0 - pow(r, 4);
			if (vls_numsamples_zfar_znear_zscale.z < lightDist)
				inscattering += contribution * falloff;
			prevDist = dist;
		}
	}
	return inscattering;
}

float main(PS_INPUT input) : SV_Target0
{
	float2 coordinate = slice_coord.Load(uint3(
											 input.TexCoord * worldtolightr.yx, 0))
	                        .xy;
	float2 uv = coordinate * float2(0.5, -0.5) + 0.5;
	return ComputeScattering(uv);
}
#endif
