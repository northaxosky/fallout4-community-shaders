// SPDX-License-Identifier: GPL-3.0-or-later
// G-buffer decode prepass for ScreenSpaceGI's XeGTAO chain: converts the live FO4
// g-buffer (raw hyperbolic depth in .x, RT20 sphere-map view normals) into the
// linear view-space depth + view normals the validated prefilter/gi passes consume.
// Decode math is RE-pinned (depth viewport partition, invProj reproj, RT20 normal).

cbuffer DecodeCB : register(b0)
{
	row_major float4x4 InvProj;  // = TryGetCameraMatrices().invProj (raw, non-transposed)
	float2 RcpFrameDim;
	float2 FrameDim;
};

Texture2D<float>  srcRawDepth  : register(t0);  // scene depth SRV, channel .x
Texture2D<float2> srcRawNormal : register(t1);  // RT20 (kGbufferNormal), R16G16_UNORM

RWTexture2D<float>  outViewDepth  : register(u0);  // linear view-space Z (R32_FLOAT)
RWTexture2D<float4> outViewNormal : register(u1);  // view-space unit normal (RGBA16F)

// RT20 sphere-map decode -> view-space unit normal. RE-pinned: enc in [0,1]^2;
// e = enc*4-2; n = (e*sqrt(1-|e|^2/4), -(1-|e|^2/2)). Already view space.
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
	// First-person viewport partition (depth<0.01) uses a separate, unexposed near
	// reproj matrix; the world/far invProj is wrong there. Mask to 0 so XeGTAO's
	// FP_Z cull (viewspaceZ>18) drops these pixels. RE-pinned viewport split.
	if (rawDepth < 0.01)
	{
		viewZ = 0.0;
	}
	else
	{
		float localZ = (rawDepth - 0.01) / 0.99;  // exact world/far NDC z (vanilla approximates depth*1.01-0.01)
		float2 uv = (float2(dtid) + 0.5) * RcpFrameDim;
		float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);  // y-flip: uv top-left origin -> NDC
		float4 h = mul(float4(ndc, localZ, 1.0), InvProj);        // row-vector v*M convention
		viewZ = h.z / h.w;                                        // positive linear view-space distance
	}

	outViewDepth[dtid] = viewZ;
	outViewNormal[dtid] = float4(DecodeViewNormal(encNormal), 0.0);
}
