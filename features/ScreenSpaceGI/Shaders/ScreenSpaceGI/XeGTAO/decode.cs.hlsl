// SPDX-License-Identifier: GPL-3.0-or-later
// FO4 g-buffer decode for XeGTAO view-space inputs.

cbuffer DecodeCB : register(b0)
{
	row_major float4x4 InvProj;  // untransposed
	float2 RcpFrameDim;
	float2 FrameDim;
};

Texture2D<float>  srcRawDepth  : register(t0);
Texture2D<float2> srcRawNormal : register(t1);  // RT20 R16G16_UNORM

RWTexture2D<float>  outViewDepth  : register(u0);
RWTexture2D<float4> outViewNormal : register(u1);

// FO4 RT20 sphere-map decode.
float3 DecodeViewNormal(float2 enc)
{
	float2 e = enc * 4.0 - 2.0;
	float e2 = dot(e, e);
	float2 xy = e * sqrt(max(0.0, 1.0 - e2 * 0.25));
	float z = -(1.0 - e2 * 0.5);
	return normalize(float3(xy, z));
}

[numthreads(8, 8, 1)]
void main(uint2 dtid : SV_DispatchThreadID)
{
	if (dtid.x >= (uint)FrameDim.x || dtid.y >= (uint)FrameDim.y)
		return;

	int3 px = int3(dtid, 0);
	float rawDepth = srcRawDepth.Load(px);
	float2 encNormal = srcRawNormal.Load(px);

	float viewZ;
	// The first-person partition lacks a matching inverse projection.
	if (rawDepth < 0.01)
	{
		viewZ = 0.0;
	}
	else
	{
		float localZ = (rawDepth - 0.01) / 0.99;  // world partition to NDC depth
		float2 uv = (float2(dtid) + 0.5) * RcpFrameDim;
		float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);  // top-left UV to NDC
		float4 h = mul(float4(ndc, localZ, 1.0), InvProj);  // row-vector convention
		viewZ = h.z / h.w;
	}

	outViewDepth[dtid] = viewZ;
	outViewNormal[dtid] = float4(DecodeViewNormal(encNormal), 0.0);
}
