// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) Skyrim Community Shaders contributors
// Ported from Skyrim Community Shaders.

#include "bend_sss_gpu.hlsli"

Texture2D<unorm float> DepthTexture : register(t0);
RWTexture2D<unorm float> OutputTexture : register(u0);
SamplerState PointBorderSampler : register(s0);

cbuffer PerFrame : register(b1)
{
	float4 LightCoordinate;
	int2 WaveOffset;
	float FarDepthValue;
	float NearDepthValue;
	float2 InvDepthTextureSize;
	float2 DynamicRes;
	float SurfaceThickness;
	float BilinearThreshold;
	float ShadowContrast;
};

[numthreads(WAVE_SIZE, 1, 1)] void main(
	int3 groupID : SV_GroupID,
	int groupThreadID : SV_GroupThreadID)
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
	parameters.DynamicRes = DynamicRes;
	parameters.SurfaceThickness = SurfaceThickness;
	parameters.BilinearThreshold = BilinearThreshold;
	parameters.ShadowContrast = ShadowContrast;
	parameters.UsePrecisionOffset = true;

	WriteScreenSpaceShadow(parameters, groupID, groupThreadID);
}
