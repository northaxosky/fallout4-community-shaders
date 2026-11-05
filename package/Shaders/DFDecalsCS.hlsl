// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef CSHADER
cbuffer DecalsConstants : register(b0)
{
	row_major float4x4 ViewMat;
	float4 PosAdjust;
	uint DecalCount;
};

struct ComputeShaderDecalsInput
{
	row_major float4x4 DynamicObjectWorld;
	float4 Projection[3];
	float3 Origin;
	float Width;
	float Height;
	float Depth;
	float Alpha;
	uint StencilRef;
	uint SubTex;
	float3 LightDirection;
};

struct ComputeShaderDecalsOutput
{
	row_major float4x4 ViewToAtlas;
	row_major float4x4 World;
	row_major float4x4 ViewAxes;
	float4 AtlasRect;
	float3 LightDirection;
	float Alpha;
};

StructuredBuffer<ComputeShaderDecalsInput> DecalsInput : register(t0);
RWStructuredBuffer<ComputeShaderDecalsOutput> DecalsOutput : register(u0);

[numthreads(64, 1, 1)] void main(uint3 threadId : SV_DispatchThreadID) {
	if (threadId.x < DecalCount) {
		ComputeShaderDecalsInput decal = DecalsInput[threadId.x];
		float4 axisX = decal.Projection[0];
		float4 axisY = decal.Projection[1];
		float4 axisZ = decal.Projection[2];
		float3 origin = decal.Origin;
		if (decal.StencilRef > 0) {
			axisX = mul(axisX, decal.DynamicObjectWorld);
			axisY = mul(axisY, decal.DynamicObjectWorld);
			axisZ = mul(axisZ, decal.DynamicObjectWorld);
			origin = mul(float4(origin, 1.0), decal.DynamicObjectWorld).xyz;
		}
		float3 lightDirection = -float3(
			dot(axisX, float4(decal.LightDirection, 1.0)),
			dot(axisY, float4(decal.LightDirection, 1.0)),
			dot(axisZ, float4(decal.LightDirection, 1.0)));
		float4x4 world = float4x4(
			axisX * decal.Width,
			axisY * decal.Height,
			axisZ * decal.Depth,
			float4(origin - PosAdjust.xyz, 1.0 - PosAdjust.w));

		float4 atlasRect;
		float2 atlasCenter;
		switch (decal.SubTex) {
		case 0:
			atlasRect = float4(0.0, 0.0, 0.5, 0.5);
			atlasCenter = float2(0.25, 0.25);
			break;
		case 1:
			atlasRect = float4(0.5, 0.0, 1.0, 0.5);
			atlasCenter = float2(0.75, 0.25);
			break;
		case 2:
			atlasRect = float4(0.0, 0.5, 0.5, 1.0);
			atlasCenter = float2(0.25, 0.75);
			break;
		case 3:
			atlasRect = float4(0.5, 0.5, 1.0, 1.0);
			atlasCenter = float2(0.75, 0.75);
			break;
		default:
			atlasRect = float4(0.0, 0.0, 1.0, 1.0);
			atlasCenter = float2(0.5, 0.5);
			break;
		}

		float4 offset = float4(-origin, 1.0);
		float4x4 worldToLocal = float4x4(
			axisX.x, axisY.x, axisZ.x, 0.0,
			axisX.y, axisY.y, axisZ.y, 0.0,
			axisX.z, axisY.z, axisZ.z, 0.0,
			dot(offset, axisX), dot(offset, axisY), dot(offset, axisZ), 1.0);
		float3 inverseSize = 1.0 / float3(decal.Width, decal.Height, decal.Depth);
		float2 scale = decal.SubTex != 255 ? inverseSize.xy * 0.5 : inverseSize.xy;
		float4x4 localToAtlas = float4x4(
			scale.x, 0.0, 0.0, 0.0,
			0.0, scale.y, 0.0, 0.0,
			0.0, 0.0, inverseSize.z, 0.0,
			atlasCenter.x, atlasCenter.y, 0.5, 1.0);
		float4x4 worldToAtlas = mul(worldToLocal, localToAtlas);

		float3 rebase = worldToAtlas[0].xyz * PosAdjust.x;
		rebase = PosAdjust.y * worldToAtlas[1].xyz + rebase;
		rebase = PosAdjust.z * worldToAtlas[2].xyz + rebase;
		worldToAtlas[3].xyz += rebase;

		float4x4 axes = float4x4(-axisX, -axisY, -axisZ, float4(0.0, 0.0, 0.0, 1.0));
		float4x4 viewToAtlas = mul(transpose(ViewMat), worldToAtlas);

		ComputeShaderDecalsOutput output;
		output.ViewToAtlas = transpose(viewToAtlas);
		output.World = transpose(world);
		output.ViewAxes = mul(axes, ViewMat);
		output.AtlasRect = atlasRect;
		output.LightDirection = lightDirection;
		output.Alpha = decal.Alpha;
		DecalsOutput[threadId.x] = output;
	}
}
#endif
