// SSGI v2 radianceDisocc.cs - port of upstream Skyrim CS @ bb6460db.
//
// Reduced FO4 variant: defines GI only (no TEMPORAL_DENOISER, no HALF_RATE,
// no VR). Skips the temporal-reprojection branch entirely since:
//   1. FO4 doesn't yet have stable per-frame camera matrices on the GPU side
//      (PrevInvViewMat/CameraViewInverse populated as identity until tracked).
//   2. Lit-colour source RT is unconfirmed pending Fallout4RE prompt 3 - we
//      use kDiffuseBuffer as a best-guess (sRGB-linear deferred output).
//
// Phase 2c.1 baseline: produces fresh radiance + initialised history every
// frame, lets prefilterRadiance + gi continue the chain.

#include "Common.hlsli"

Texture2D<half4> srcDiffuse     : register(t0);
Texture2D<half>  srcCurrDepth   : register(t1);
Texture2D<half4> srcCurrNormal  : register(t2);
Texture2D<half3> srcPrevGeo     : register(t3);
Texture2D<float4> srcMotionVec  : register(t4);
Texture2D<unorm float> srcAccumFrames : register(t5);
Texture2D<half>  srcPrevAo      : register(t6);
Texture2D<half4> srcPrevIlY     : register(t7);
Texture2D<half2> srcPrevIlCoCg  : register(t8);
Texture2D<half4> srcPrevGISpecular : register(t9);

RWTexture2D<float3> outRadianceDisocc        : register(u0);
RWTexture2D<unorm float> outAccumFrames      : register(u1);
RWTexture2D<float>  outRemappedAo            : register(u2);
RWTexture2D<float4> outRemappedIlY           : register(u3);
RWTexture2D<float2> outRemappedIlCoCg        : register(u4);
RWTexture2D<float4> outRemappedPrevGISpecular: register(u5);

[numthreads(8, 8, 1)] void main(uint2 pixCoord : SV_DispatchThreadID)
{
	if (any(pixCoord >= uint2(OUT_FRAME_DIM)))
		return;

	const float2 frameScale = FrameDim * RcpTexDim;
	const float2 uv = (pixCoord + 0.5) * RCP_OUT_FRAME_DIM;

	const float curr_depth = READ_DEPTH(srcCurrDepth, pixCoord);

	// Sky / invalid depth: clear all outputs to safe defaults.
	if (curr_depth < FP_Z) {
		outRadianceDisocc[pixCoord] = 0;
		outAccumFrames[pixCoord]    = 1.0 / 255.0;
		outRemappedAo[pixCoord]     = 1;
		outRemappedIlY[pixCoord]    = 0;
		outRemappedIlCoCg[pixCoord] = 0;
		outRemappedPrevGISpecular[pixCoord] = 0;
		return;
	}

	// Reduced variant: no temporal reprojection. History is always cold-started
	// to identity so downstream gi.cs (built without TEMPORAL_DENOISER) gets
	// clean per-frame state instead of stale ping-pong data.
	half3 radiance = Color::RadianceToLinear(
		FULLRES_LOAD(srcDiffuse, pixCoord, uv * frameScale, samplerLinearClamp).rgb * GIStrength);
	radiance = filterNaN(radiance);
	radiance = filterInf(radiance);

	outRadianceDisocc[pixCoord]         = radiance;
	outAccumFrames[pixCoord]            = 1.0 / 255.0;
	outRemappedAo[pixCoord]             = 1;
	outRemappedIlY[pixCoord]            = 0;
	outRemappedIlCoCg[pixCoord]         = 0;
	outRemappedPrevGISpecular[pixCoord] = 0;
}
