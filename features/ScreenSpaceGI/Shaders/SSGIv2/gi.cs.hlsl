// SSGI v2 gi.cs - port of upstream Skyrim CS @ bb6460db.
//
// XeGTAO + Visibility Bitmask + SH2-YCoCg main compute. FO4 reduction:
//   - GI define always on, GI_SPECULAR optional (no TEMPORAL_DENOISER, no VR).
//   - Stereo:: / FrameBuffer:: replaced with mono no-ops.
//   - FrameBuffer::CameraViewInverse[eye] -> plain SSGICB CameraViewInverse.
//
// MIT licensed (Intel XeGTAO + ProfJack additions); SH helpers MIT (SebH).

#include "Common.hlsli"

#define GI 1

Texture2D<float>        srcWorkingDepth   : register(t0);
Texture2D<float4>       srcNormalRoughness: register(t1);
Texture2D<float3>       srcRadiance       : register(t2);
Texture2D<unorm float2> srcNoise          : register(t3);
Texture2D<unorm float>  srcAccumFrames    : register(t4);
Texture2D<float4>       srcPrevY          : register(t5);
Texture2D<float2>       srcPrevCoCg       : register(t6);
Texture2D<float4>       srcPrevGISpecular : register(t7);
Texture2D<float2>       srcNormal         : register(t8);
#ifdef GI_SPECULAR
Texture2D<float4>       srcGbufferMaterial : register(t9);
#endif

RWTexture2D<unorm float> outAo         : register(u0);
RWTexture2D<float4>      outY          : register(u1);
RWTexture2D<float2>      outCoCg       : register(u2);
RWTexture2D<float4>      outGISpecular : register(u3);
RWTexture2D<half3>       outPrevGeo    : register(u4);

float GetDepthFade(float depth)
{
	return saturate((depth - DepthFadeRange.x) * DepthFadeScaleConst);
}

#ifdef GI_SPECULAR
// Walter et al. 2007, "Microfacet models for refraction through rough surfaces".
float GetNormalDistributionFunctionGGX(float roughness, float NdotH)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float d = max((NdotH * a2 - NdotH) * NdotH + 1, 1e-5);
	return a2 / (Math::PI * d * d);
}

// Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs".
float GetVisibilityFunctionSmithJointApprox(float roughness, float NdotV, float NdotL)
{
	float a = roughness * roughness;
	float visSmithV = NdotL * (NdotV * (1 - a) + a);
	float visSmithL = NdotV * (NdotL * (1 - a) + a);
	float vis = visSmithV + visSmithL;
	return vis > 0 ? (0.5 / vis) : 0;
}

float specularLobeHalfAngle(float roughness)
{
	float roughness2 = roughness * roughness;
	return clamp(4.1679 * roughness2 * roughness2 - 9.0127 * roughness2 * roughness + 4.6161 * roughness2 + 1.7048 * roughness + 0.1, 0, Math::HALF_PI);
}

float3 getSpecularDominantDirection(float3 N, float3 V, float roughness)
{
	float f = (1 - roughness) * (sqrt(1 - roughness) + roughness);
	float3 R = reflect(-V, N);
	float3 D = lerp(N, R, f);
	return normalize(D);
}
#endif

// 128x128x64 EA FastNoise (MIT).
float2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)
{
	uint2 noiseCoord = (pixCoord % 128) + uint2(0, (temporalIndex % 64) * 128);
	return srcNoise.Load(uint3(noiseCoord, 0));
}

void CalculateGI(
	uint2 dtid, float2 uv, float viewspaceZ, float3 viewspaceNormal,
	out float o_ao, out sh2 o_currY, out float2 o_currCoCg, out float4 o_currGIAOSpecular)
{
	const float2 frameScale = FrameDim * RcpTexDim;

	uint eyeIndex = 0;
	float2 normalizedScreenPos = uv;

	const float rcpNumSlices = rcp((float)NumSlices);
	const float rcpNumSteps  = rcp((float)NumSteps);

	const float pixelTooCloseThreshold = 1.3;
	const float2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ.xx * NDCToViewMul.xy * RCP_OUT_FRAME_DIM;

	float screenspaceRadius = EffectRadius / pixelDirRBViewspaceSizeAtCenterZ.x;
	screenspaceRadius = max(MinScreenRadius, screenspaceRadius);
	const float minS = pixelTooCloseThreshold / screenspaceRadius;

	uint2 noiseCoord = uint2(normalizedScreenPos * OUT_FRAME_DIM);
	const float2 localNoise = SpatioTemporalNoise(noiseCoord, FrameIndex);
	const float noiseSlice = localNoise.x;
	const float noiseStep  = localNoise.y;

	const float3 pixCenterPos = ScreenToViewPosition(normalizedScreenPos, viewspaceZ, eyeIndex);
	const float3 viewVec = normalize(-pixCenterPos);
#ifdef GI_SPECULAR
	const float NoV = clamp(dot(viewVec, viewspaceNormal), 1e-5, 1);
#endif

	// flip foliage normal
	if (dot(viewVec, pixCenterPos) > 0)
		viewspaceNormal = -viewspaceNormal;

	float  visibility   = 0;
	float4 radianceY    = 0;
	float2 radianceCoCg = 0;
#ifdef GI_SPECULAR
	float visibilitySpecular = 0;
	float3 radianceSpecular = 0;
	// FO4 stores glossiness in kGbufferMaterial.x; convert to roughness for the upstream BRDF fit.
	const float roughness = max(0.2, saturate(1 - FULLRES_LOAD(srcGbufferMaterial, dtid, uv * frameScale, samplerLinearClamp).x));
#endif

	for (uint slice = 0; slice < NumSlices; slice++) {
		float phi = (Math::PI * rcpNumSlices) * (slice + noiseSlice);
		float3 directionVec = 0;
		sincos(phi, directionVec.y, directionVec.x);

		float2 omega = float2(directionVec.x, -directionVec.y) * screenspaceRadius;
		const float logLenOmega = 0.5 * log2(max(dot(omega, omega), EPSILON_LENGTH_SQ));

		const float3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);
		const float3 axisVec = normalize(cross(orthoDirectionVec, viewVec));

		float3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);
		float rcpProjectedNormalVecLength = rsqrt(max(dot(projectedNormalVec, projectedNormalVec), EPSILON_LENGTH_SQ));
		float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));
		float cosNorm  = saturate(dot(projectedNormalVec, viewVec) * rcpProjectedNormalVecLength);

		float n = signNorm * FastMath::ACos(cosNorm);

		uint bitmask   = 0;
		uint bitmaskGI = 0;
#ifdef GI_SPECULAR
		uint bitmaskGISpecular = 0;
		float3 domVec = getSpecularDominantDirection(viewspaceNormal, viewVec, roughness);
		float3 projectedDomVec = normalize(domVec - axisVec * dot(domVec, axisVec));
		float nDom = sign(dot(orthoDirectionVec, projectedDomVec)) * FastMath::ACos(saturate(dot(projectedDomVec, viewVec)));
#endif

		// R1 sequence for per-slice step phase randomisation.
		float stepNoise = frac(noiseStep + slice * 0.6180339887498948482);

		[unroll] for (int sideSign = -1; sideSign <= 1; sideSign += 2)
		{
			[loop] for (uint step = 0; step < NumSteps; step++)
			{
				float s = (step + stepNoise) * rcpNumSteps;
				s *= s;
				s += minS;

				float2 sampleOffset = s * omega;

				float2 samplePxCoord = dtid + 0.5 + sampleOffset * sideSign;
				float2 sampleUV = samplePxCoord * RCP_OUT_FRAME_DIM;

				float2 sampleScreenPos = sampleUV;
				[branch] if (any(sampleScreenPos > 1.0) || any(sampleScreenPos < 0.0)) continue;

				// Mip level grows with pixel-space distance from the centre.
				float mipLevel = clamp(log2(s) + logLenOmega - 3.3, 0, 5);
				float mipLevelRadiance = max(mipLevel, 1);

				float SZ = srcWorkingDepth.SampleLevel(samplerPointClamp, sampleUV * frameScale, mipLevel);

				float3 samplePos = ScreenToViewPosition(sampleScreenPos, SZ, eyeIndex);
				float3 sampleDelta = samplePos - pixCenterPos;
				float3 sampleHorizonVec     = normalize(sampleDelta);
				float3 sampleBackHorizonVec = normalize(sampleDelta - viewVec * Thickness);

				float angleFront = FastMath::ACos(dot(sampleHorizonVec,     viewVec));
				float angleBack  = FastMath::ACos(dot(sampleBackHorizonVec, viewVec));
				float2 angleRange = -sideSign * (sideSign == -1 ? float2(angleFront, angleBack) : float2(angleBack, angleFront));
				angleRange = smoothstep(0, 1, (angleRange + n) * Math::INV_PI + 0.5);

				uint2 bitsRange  = uint2(round(angleRange.x * 32u), round((angleRange.y - angleRange.x) * 32u));
				uint  maskedBits = s < AORadius ? ((1 << bitsRange.y) - 1) << bitsRange.x : 0;

				// GI: GI-radius horizon vector for thickness-tolerant sampling
				float3 sampleBackHorizonVecGI = normalize(sampleDelta - viewVec * 300);
				float  angleBackGI = FastMath::ACos(dot(sampleBackHorizonVecGI, viewVec));
				float2 angleRangeGI = -sideSign * (sideSign == -1 ? float2(angleFront, angleBackGI) : float2(angleBackGI, angleFront));

#ifdef GI_SPECULAR
				float coneHalfAngles = max(5e-2, specularLobeHalfAngle(roughness));
				float2 angleRangeSpecular = clamp((angleRangeGI + nDom) * 0.5 / coneHalfAngles, -1, 1) * 0.5 + 0.5;
				uint2 bitsRangeGISpecular = uint2(round(angleRangeSpecular.x * 32u), round((angleRangeSpecular.y - angleRangeSpecular.x) * 32u));
				uint maskedBitsGISpecular = s < GIRadius ? ((1 << bitsRangeGISpecular.y) - 1) << bitsRangeGISpecular.x : 0;
#endif

				angleRangeGI = smoothstep(0, 1, (angleRangeGI + n) * Math::INV_PI + 0.5);
				uint2 bitsRangeGI  = uint2(round(angleRangeGI.x * 32u), round((angleRangeGI.y - angleRangeGI.x) * 32u));
				uint  maskedBitsGI = s < GIRadius ? ((1 << bitsRangeGI.y) - 1) << bitsRangeGI.x : 0;

				uint validBits = maskedBitsGI & ~bitmaskGI;
				bool checkGI = validBits;
#ifdef GI_SPECULAR
				uint overlappedBitsSpecular = maskedBitsGISpecular & ~bitmaskGISpecular;
				checkGI = checkGI || overlappedBitsSpecular;
#endif
				if (checkGI) {
					float giBoost = 4.0 * Math::PI * (1 + GIDistanceCompensation * smoothstep(0, GICompensationMaxDist, s * EffectRadius));

					float3 normalSample = GBuffer::DecodeNormal(srcNormal.SampleLevel(samplerPointClamp, sampleUV * OUT_FRAME_SCALE, mipLevelRadiance));
					if (dot(samplePos, normalSample) > 0)
						normalSample = -normalSample;
					float frontBackMult = -dot(normalSample, sampleHorizonVec);
					frontBackMult = frontBackMult < 0 ? 0.0 : frontBackMult;

					if (frontBackMult > 0.f) {
						// View-space horizon vec -> world-space for SH evaluation.
						float3 sampleHorizonVecWS = normalize(mul(CameraViewInverse, float4(sampleHorizonVec, 0)).xyz);

						float3 sampleRadiance = srcRadiance.SampleLevel(samplerPointClamp, sampleUV * OUT_FRAME_SCALE, mipLevelRadiance).rgb
												* frontBackMult * giBoost * countbits(validBits) * 0.03125;
						sampleRadiance = max(sampleRadiance, 0);
						float3 sampleRadianceYCoCg = Color::RGBToYCoCg(sampleRadiance);

						radianceY    += sampleRadianceYCoCg.r * SphericalHarmonics::Evaluate(sampleHorizonVecWS);
						radianceCoCg += sampleRadianceYCoCg.gb;

#ifdef GI_SPECULAR
						float NoH = clamp(dot(viewspaceNormal, normalize(viewVec + sampleHorizonVec)), 1e-2, 1);
						float NoL = clamp(dot(viewspaceNormal, sampleHorizonVec), 1e-2, 1);

						float3 specularRadiance = sampleRadiance * countbits(overlappedBitsSpecular) * 0.03125;
						specularRadiance *= GetNormalDistributionFunctionGGX(roughness, NoH) * GetVisibilityFunctionSmithJointApprox(roughness, NoV, NoL);
						specularRadiance = max(0, specularRadiance);
						radianceSpecular += specularRadiance;
#endif
					}
				}

				bitmask   |= maskedBits;
				bitmaskGI |= maskedBitsGI;
#ifdef GI_SPECULAR
				bitmaskGISpecular |= maskedBitsGISpecular;
#endif
			}
		}

		visibility += countbits(bitmask) * 0.03125;
#ifdef GI_SPECULAR
		visibilitySpecular += countbits(bitmaskGISpecular) * 0.03125;
#endif
	}

	float depthFade = GetDepthFade(viewspaceZ);

	visibility *= rcpNumSlices;
	visibility  = lerp(saturate(visibility), 0, depthFade);
	visibility  = 1 - pow(abs(1 - visibility), AOPower);

	radianceY    *= rcpNumSlices;
	radianceY    = lerp(radianceY, 0, depthFade);

	radianceCoCg *= rcpNumSlices * GISaturation;

#ifdef GI_SPECULAR
	radianceSpecular *= rcpNumSlices;
	radianceSpecular = lerp(radianceSpecular, 0, depthFade);
	visibilitySpecular *= rcpNumSlices;
	// FIXME: mirrors upstream Screen Space GI gi.cs.hlsl:334 verbatim; suspected upstream copy-paste (should be saturate(visibilitySpecular)). Dormant today (texGiSpecular unconsumed). Fix when upstream does or when wiring the consumer.
	visibilitySpecular = lerp(saturate(visibility), 0, depthFade);
#endif

	o_ao = visibility;
	o_currY = radianceY;
	o_currCoCg = radianceCoCg;
#ifdef GI_SPECULAR
	o_currGIAOSpecular = float4(radianceSpecular, visibilitySpecular);
#else
	o_currGIAOSpecular = float4(0, 0, 0, 0);
#endif
}

[numthreads(8, 8, 1)] void main(const uint2 dtid : SV_DispatchThreadID)
{
	if (any(dtid >= uint2(OUT_FRAME_DIM)))
		return;
	const float2 frameScale = FrameDim * RcpTexDim;

	uint2 pxCoord = dtid;
	float2 uv = (pxCoord + 0.5) * RCP_OUT_FRAME_DIM;
	uint eyeIndex = 0;

	float viewspaceZ = READ_DEPTH(srcWorkingDepth, pxCoord);

	float2 normalSample = FULLRES_LOAD(srcNormal, pxCoord, uv * OUT_FRAME_SCALE, samplerLinearClamp);
	float3 viewspaceNormal = GBuffer::DecodeNormal(normalSample);

	half2 encodedWorldNormal = GBuffer::EncodeNormal(ViewToWorldVector(viewspaceNormal, CameraViewInverse));
	outPrevGeo[pxCoord] = half3(viewspaceZ, encodedWorldNormal);

	// Pull centre pixel slightly towards camera to avoid FP16-depth imprecision.
	viewspaceZ *= 0.99920h;

	float currAo = 0;
	float4 currY = 0;
	float2 currCoCg = 0;
	float4 currGIAOSpecular = float4(0, 0, 0, 0);

	bool needGI = viewspaceZ > FP_Z && viewspaceZ < DepthFadeRange.y;
	if (needGI) {
		CalculateGI(pxCoord, uv, viewspaceZ, viewspaceNormal, currAo, currY, currCoCg, currGIAOSpecular);
	}

	currY = filterNaN(currY);
	currCoCg = filterNaN(currCoCg);
#ifdef GI_SPECULAR
	currGIAOSpecular = filterNaN(currGIAOSpecular);
#endif

	outAo[pxCoord]   = currAo;
	outY[pxCoord]    = currY;
	outCoCg[pxCoord] = currCoCg;
#ifdef GI_SPECULAR
	outGISpecular[pxCoord] = currGIAOSpecular;
#endif
}
