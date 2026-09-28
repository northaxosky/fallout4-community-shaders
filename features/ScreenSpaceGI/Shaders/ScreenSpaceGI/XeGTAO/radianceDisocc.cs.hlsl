// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) Skyrim Community Shaders contributors
// Ported from Skyrim Community Shaders d330bf12d.
// Gathers diffuse radiance and reprojects the previous indirect result.

#include "../Common/Color.hlsli"
#include "../Common/Math.hlsli"
#include "common.hlsli"

// Rebuilds the composite's diffuse term; FO4 has no diffuse colour target.
Texture2D<float3> srcDiffuseLightA : register(t0);
Texture2D<float3> srcDiffuseLightB : register(t1);
Texture2D<float> srcCurrDepth : register(t2);
Texture2D<float2> srcMotionVec : register(t3);
Texture2D<float3> srcPrevGeo : register(t4);
Texture2D<unorm float> srcAccumFrames : register(t5);
Texture2D<float4> srcPrevIlY : register(t6);
Texture2D<float2> srcPrevIlCoCg : register(t7);
Texture2D<float4> srcAlbedo : register(t8);
Texture2D<float4> srcEmissive : register(t9);

RWTexture2D<float3> outRadianceDisocc : register(u0);
RWTexture2D<unorm float> outAccumFrames : register(u1);
RWTexture2D<float4> outRemappedIlY : register(u2);
RWTexture2D<float2> outRemappedIlCoCg : register(u3);

#if defined(TEMPORAL_DENOISER)
#	define REPROJECTION
#endif

void readHistory(
	float curr_depth, float3 curr_pos, int2 pixCoord, float bilinear_weight,
	inout float4 prev_y, inout float2 prev_co_cg, inout float accum_frames, inout float wsum)
{
	// Previous extents under dynamic resolution.
	const float2 uv = (pixCoord + .5) * RcpPrevFrameDim;
	const float2 screen_pos = uv;
	if (any(screen_pos < 0) || any(screen_pos > 1))
		return;

	const float3 prev_geo = srcPrevGeo[pixCoord];
	const float prev_depth = prev_geo.x;

	// Cheap wide reject before reconstruction.
	if (abs(curr_depth - prev_depth) > curr_depth * DepthDisocclusion * 3)
		return;

	float3 prev_pos = PreviousScreenToViewPosition(screen_pos, prev_depth);
	prev_pos = ViewToWorldPosition(prev_pos, PrevViewToWorld, PrevCameraOrigin.xyz);

	float3 delta_pos = curr_pos - prev_pos;

	const float movement_thres = curr_depth * DepthDisocclusion;

	bool depth_pass = dot(delta_pos, delta_pos) < movement_thres * movement_thres;
	if (depth_pass) {
#ifdef TEMPORAL_DENOISER
		prev_y += srcPrevIlY[pixCoord] * bilinear_weight;
		prev_co_cg += srcPrevIlCoCg[pixCoord] * bilinear_weight;
		accum_frames += srcAccumFrames[pixCoord] * bilinear_weight;
#endif
		wsum += bilinear_weight;
	}
}

[numthreads(8, 8, 1)]
void main(const uint2 pixCoord : SV_DispatchThreadID)
{
	if (any(pixCoord >= uint2(OUT_FRAME_DIM)))
		return;

	const float2 uv = (pixCoord + .5) * RCP_OUT_FRAME_DIM;
	const float2 screen_pos = uv;

	float2 prev_screen_pos = screen_pos;
#ifdef REPROJECTION
	prev_screen_pos += srcMotionVec[pixCoord];
#endif
	float2 prev_uv = prev_screen_pos;

	float4 prev_y = 0;
	float2 prev_co_cg = 0;
	float accum_frames = 0;
	float wsum = 0;

	const float curr_depth = READ_DEPTH(srcCurrDepth, pixCoord);

	if (curr_depth < FP_Z) {
		outRadianceDisocc[pixCoord] = 0;
		outAccumFrames[pixCoord] = 1.0 / 255.0;
		outRemappedIlY[pixCoord] = 0;
		outRemappedIlCoCg[pixCoord] = 0;
		return;
	}

#ifdef REPROJECTION
	if (HistoryValid() && (curr_depth <= DepthFadeRange.y) && !(any(prev_screen_pos < 0) || any(prev_screen_pos > 1))) {
		float3 curr_pos = ScreenToViewPosition(screen_pos, curr_depth);
		curr_pos = ViewToWorldPosition(curr_pos, ViewToWorld, CameraOrigin.xyz);

		float2 prev_px_coord = prev_uv * PrevFrameDim;
		int2 prev_px_lu = floor(prev_px_coord - 0.5);
		float2 bilinear_weights = prev_px_coord - 0.5 - prev_px_lu;

		readHistory(curr_depth, curr_pos,
			prev_px_lu, (1 - bilinear_weights.x) * (1 - bilinear_weights.y),
			prev_y, prev_co_cg, accum_frames, wsum);
		readHistory(curr_depth, curr_pos,
			prev_px_lu + int2(1, 0), bilinear_weights.x * (1 - bilinear_weights.y),
			prev_y, prev_co_cg, accum_frames, wsum);
		readHistory(curr_depth, curr_pos,
			prev_px_lu + int2(0, 1), (1 - bilinear_weights.x) * bilinear_weights.y,
			prev_y, prev_co_cg, accum_frames, wsum);
		readHistory(curr_depth, curr_pos,
			prev_px_lu + int2(1, 1), bilinear_weights.x * bilinear_weights.y,
			prev_y, prev_co_cg, accum_frames, wsum);

		if (wsum > 1e-2) {
			float rcpWsum = rcp(wsum + EPSILON_WEIGHT_SUM);
			prev_y *= rcpWsum;
			prev_co_cg *= rcpWsum;
			accum_frames *= rcpWsum;
		}
	}
#endif

	float3 diffuseLight = srcDiffuseLightA[pixCoord];
	if (IncludeSourceB())
		diffuseLight += srcDiffuseLightB[pixCoord];
	float3 diffuseColor = srcAlbedo[pixCoord].rgb * diffuseLight * 3.0 + srcEmissive[pixCoord].rgb;

	float3 radiance = Color::RadianceToLinear(diffuseColor * GIStrength);
	radiance = filterNaN(radiance);
	radiance = filterInf(radiance);
	outRadianceDisocc[pixCoord] = radiance;

#ifdef TEMPORAL_DENOISER
	// Halve on disocclusion to soften the flash.
	float prevAccum = accum_frames * 255;
	if (wsum < 1e-2)
		prevAccum = prevAccum * 0.5;

	// Fast motion makes history less trustworthy, so cap accumulation with it.
	float2 motionVec = prev_screen_pos - screen_pos;
	float motionLen = length(motionVec);
	float motionMaxAccum = lerp(MaxAccumFrames, max(MaxAccumFrames * 0.25, 4), saturate(motionLen * 20));

	accum_frames = max(1, min(prevAccum + 1, motionMaxAccum));
	outAccumFrames[pixCoord] = accum_frames / 255.0;
	outRemappedIlY[pixCoord] = prev_y;
	outRemappedIlCoCg[pixCoord] = prev_co_cg;
#endif
}
