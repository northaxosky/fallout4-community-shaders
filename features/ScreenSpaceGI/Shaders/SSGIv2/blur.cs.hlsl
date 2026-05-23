// SSGI v2 blur.cs - 8-tap Poisson bilateral over IL Y/CoCg.
// Port of upstream Skyrim CS @ bb6460db, FO4 reduction: no TEMPORAL_DENOISER,
// no VR, mono Stereo/FrameBuffer stubs.
//
// Reference: NVIDIA Fast Denoising with Self-Stabilising Recurrent Blurs
//   https://developer.download.nvidia.com/video/gputechconf/gtc/2020/presentations/s22699-fast-denoising-with-self-stabilizing-recurrent-blurs.pdf

#include "Common.hlsli"

Texture2D<half>   srcDepth          : register(t0);
Texture2D<half4>  srcNormalRoughness: register(t1);  // FO4: octahedral xy from prefilterNormal
Texture2D<unorm float> srcAccumFrames : register(t2);
Texture2D<float4> srcIlY            : register(t3);
Texture2D<float2> srcIlCoCg         : register(t4);

RWTexture2D<unorm float> outAccumFrames : register(u0);
RWTexture2D<float4>      outIlY         : register(u1);
RWTexture2D<float2>      outIlCoCg      : register(u2);

// Poisson 8 samples, min-distance 0.5, average-on-radius 2.
static const float3 g_Poisson8[8] = {
	float3(-0.4706069, -0.4427112, +0.6461146),
	float3(-0.9057375, +0.3003471, +0.9542373),
	float3(-0.3487388, +0.4037880, +0.5335386),
	float3(+0.1023042, +0.6439373, +0.6520134),
	float3(+0.5699277, +0.3513750, +0.6695386),
	float3(+0.2939128, -0.1131226, +0.3149309),
	float3(+0.7836658, -0.4208784, +0.8895339),
	float3(+0.1564120, -0.8198990, +0.8346850)
};

float GaussianWeight(float r) { return exp(-0.66 * r * r); }

// Reynolds orthonormal basis - http://marc-b-reynolds.github.io/quaternions/2016/07/06/Orthonormal.html
float3x3 getBasis(float3 N)
{
	float sz = sign(N.z);
	float a = 1.0 / (sz + N.z);
	float ya = N.y * a;
	float b = N.x * ya;
	float c = N.x * sz;
	float3 T = float3(c * N.x * a - 1.0, sz * b, c);
	float3 B = float3(b, N.y * ya - sz, N.y);
	return float3x3(T, B, N);
}

float2x3 getKernelBasis(float3 D, float3 N, float roughness = 1.0, float anisoFade = 1.0)
{
	float3x3 basis = getBasis(N);
	float3 T = basis[0];
	float3 B = basis[1];

	float NoD = dot(N, D);
	if (NoD < 0.999) {
		float3 R = reflect(-D, N);
		T = normalize(cross(N, R));
		B = cross(R, T);

		float skewFactor = lerp(0.5 + 0.5 * roughness, 1.0, NoD);
		skewFactor = lerp(skewFactor, 1.0, anisoFade);
		B /= skewFactor;
	}
	return float2x3(T, B);
}

[numthreads(8, 8, 1)] void main(const uint2 dtid : SV_DispatchThreadID)
{
	if (any(dtid >= uint2(OUT_FRAME_DIM)))
		return;

	const float2 frameScale = FrameDim * RcpTexDim;

	float radius = BlurRadius;
	const uint numSamples = 8;

	const float2 uv = (dtid + 0.5) * RCP_OUT_FRAME_DIM;
	uint eyeIndex = 0;
	const float2 screenPos = uv;

	float  depth  = READ_DEPTH(srcDepth, dtid);
	float3 pos    = ScreenToViewPosition(screenPos, depth, eyeIndex);
	float3 normal = GBuffer::DecodeNormal(FULLRES_LOAD(srcNormalRoughness, dtid, uv, samplerLinearClamp).xy);

	const float2 pixelDirRBViewspaceSizeAtCenterZ = depth.xx * NDCToViewMul.xy * RCP_OUT_FRAME_DIM;
	const float  worldRadius = radius * pixelDirRBViewspaceSizeAtCenterZ.x;
	float2x3     TvBv = getKernelBasis(normal, normal);
	float        halfAngle = Math::HALF_PI;

	TvBv[0] *= worldRadius;
	TvBv[1] *= worldRadius;

	const float4 ilY    = srcIlY[dtid];
	const float2 ilCoCg = srcIlCoCg[dtid];

	float4 ySum    = ilY;
	float2 coCgSum = ilCoCg;
	float  wSum    = 1;

	for (uint i = 0; i < numSamples; i++) {
		float w = GaussianWeight(g_Poisson8[i].z);

		float2 poissonOffset = g_Poisson8[i].xy;

		float3 viewSamplePos = pos + TvBv[0] * poissonOffset.x + TvBv[1] * poissonOffset.y;
		float2 screenPosSample = FrameBuffer::ViewToUV(viewSamplePos, true, eyeIndex);

		if (any(screenPosSample < 0) || any(screenPosSample > 1))
			continue;

		float2 uvSample = screenPosSample;
		uvSample = (floor(uvSample * OUT_FRAME_DIM) + 0.5) * RCP_OUT_FRAME_DIM;

		float  depthSample  = srcDepth.SampleLevel(samplerPointClamp, uvSample * frameScale, RES_MIP);
		float3 posSample    = ScreenToViewPosition(screenPosSample, depthSample, eyeIndex);

		float4 normalRoughnessSample = srcNormalRoughness.SampleLevel(samplerPointClamp, uvSample * frameScale, 0);
		float3 normalSample = GBuffer::DecodeNormal(normalRoughnessSample.xy);

		// geometry weight
		w *= saturate(1 - abs(dot(normal, posSample - pos)) * DistanceNormalisation);
		// normal weight
		w *= 1 - saturate(FastMath::acosFast4(saturate(dot(normalSample, normal))) / halfAngle);

		w = max(w, 0.01);

		if (w > 1e-8) {
			ySum    += srcIlY.SampleLevel   (samplerPointClamp, uvSample * OUT_FRAME_SCALE, 0) * w;
			coCgSum += srcIlCoCg.SampleLevel(samplerPointClamp, uvSample * OUT_FRAME_SCALE, 0) * w;
			wSum    += w;
		}
	}

	outIlY[dtid]    = ySum    / wSum;
	outIlCoCg[dtid] = coCgSum / wSum;
}
