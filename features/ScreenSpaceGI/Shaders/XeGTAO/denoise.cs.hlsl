#include "../Common/FastMath.hlsli"
#include "../Common/Math.hlsli"
#include "common.hlsli"

Texture2D<float> srcWorkingDepth : register(t0);
Texture2D<float3> srcNormal : register(t1);
Texture2D<unorm float> srcAO : register(t2);
#ifdef SSGI_BOUNCE
Texture2D<float4> srcBounceSH : register(t3);
Texture2D<float2> srcBounceCoCg : register(t4);
#endif
RWTexture2D<unorm float> outAO : register(u0);
#ifdef SSGI_BOUNCE
RWTexture2D<float4> outBounceSH : register(u1);
RWTexture2D<float2> outBounceCoCg : register(u2);
#endif

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

float3x3 getBasis(float3 N)
{
	float sz = (N.z >= 0.0) ? 1.0 : -1.0;
	float a = 1.0 / (sz + N.z);
	float ya = N.y * a;
	float b = N.x * ya;
	float c = N.x * sz;
	float3 T = float3(c * N.x * a - 1.0, sz * b, c);
	float3 B = float3(b, N.y * ya - sz, N.y);
	return float3x3(T, B, N);
}

bool ValidDepth(float z) { return z > FP_Z && z < DepthFadeRange.y && isfinite(z); }

[numthreads(8, 8, 1)]
void main(const uint2 dtid : SV_DispatchThreadID)
{
	if (any(dtid >= uint2(OUT_FRAME_DIM))) return;

	const float2 frameScale = FrameDim * RcpTexDim;
	const float2 uv = (dtid + 0.5) * RCP_OUT_FRAME_DIM;

	float depth = READ_DEPTH(srcWorkingDepth, dtid);
	float centerAO = srcAO[dtid];
	if (!ValidDepth(depth)) {
		outAO[dtid] = 0.0;
#ifdef SSGI_BOUNCE
		outBounceSH[dtid] = 0;
		outBounceCoCg[dtid] = 0;
#endif
		return;
	}

	float3 pos = ScreenToViewPosition(uv, depth);
	float3 normal = normalize(srcNormal[dtid]);

	const float2 pixelDirRBViewspaceSizeAtCenterZ = depth.xx * NDCToViewMul.xy * RCP_OUT_FRAME_DIM;
	const float worldRadius = BlurRadius * pixelDirRBViewspaceSizeAtCenterZ.x;
	float3x3 basis = getBasis(normal);
	float3 T = basis[0] * worldRadius;
	float3 B = basis[1] * worldRadius;
	const float halfAngle = Math::HALF_PI;

	float aoSum = centerAO * CenterBeta;
	float wSum = CenterBeta;
#ifdef SSGI_BOUNCE
	float4 bounceSHSum = srcBounceSH[dtid] * CenterBeta;
	float2 bounceCoCgSum = srcBounceCoCg[dtid] * CenterBeta;
#endif

	[unroll] for (uint i = 0; i < 8; i++) {
		float w = GaussianWeight(g_Poisson8[i].z);
		float2 off = g_Poisson8[i].xy;
		float3 viewSamplePos = pos + T * off.x + B * off.y;
		float2 sampleUV = ViewToUV(viewSamplePos);
		if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) continue;
		sampleUV = (floor(sampleUV * OUT_FRAME_DIM) + 0.5) * RCP_OUT_FRAME_DIM;

		float depthSample = srcWorkingDepth.SampleLevel(samplerPointClamp, sampleUV * frameScale, 0);
		if (!ValidDepth(depthSample)) continue;
		float3 posSample = ScreenToViewPosition(sampleUV, depthSample);
		float3 normalSample = normalize(srcNormal.SampleLevel(samplerPointClamp, sampleUV * frameScale, 0));

		w *= saturate(1.0 - abs(dot(normal, posSample - pos)) * DistanceNormalisation);
		w *= 1.0 - saturate(FastMath::ACos(saturate(dot(normalSample, normal))) / halfAngle);

		if (w > 1e-8) {
			float aoSample = srcAO.SampleLevel(samplerPointClamp, sampleUV * frameScale, 0);
			aoSum += aoSample * w;
#ifdef SSGI_BOUNCE
			bounceSHSum += srcBounceSH.SampleLevel(samplerPointClamp, sampleUV * frameScale, 0) * w;
			bounceCoCgSum += srcBounceCoCg.SampleLevel(samplerPointClamp, sampleUV * frameScale, 0) * w;
#endif
			wSum += w;
		}
	}

	outAO[dtid] = aoSum / max(wSum, 1e-6);
#ifdef SSGI_BOUNCE
	outBounceSH[dtid] = bounceSHSum / max(wSum, 1e-6);
	outBounceCoCg[dtid] = bounceCoCgSum / max(wSum, 1e-6);
#endif
}
