// SSGI v2 Common helpers (consolidated, self-contained).
//
// Ports the upstream Skyrim CS @ bb6460db SSGI common header plus the minimum
// subset of Common/Math.hlsli, Common/GBuffer.hlsli, and Common/SH.hlsli that
// the v2 compute chain (prefilterDepths/Normal/Radiance, gi, blur, upsample)
// touches.
//
// Upstream relies on shared engine constant buffers (SharedData::CameraData,
// FrameBuffer:: per-view matrices, etc.). FO4 doesn't expose an equivalent
// binding here, so we fold the few quantities we actually need (CameraData,
// NDCToView*, view-projection helpers) into our SSGICB at register b0.
//
// MIT licensed (Intel XeGTAO core); SH helpers MIT (SebH).

#ifndef SSGI_V2_COMMON
#define SSGI_V2_COMMON

cbuffer SSGICB : register(b0)
{
	float4x4 PrevInvViewMat;
	float2 NDCToViewMul;
	float2 NDCToViewAdd;

	float2 TexDim;
	float2 RcpTexDim;
	float2 FrameDim;
	float2 RcpFrameDim;

	uint   FrameIndex;
	uint   NumSlices;
	uint   NumSteps;
	float  MinScreenRadius;

	float  AORadius;
	float  GIRadius;
	float  EffectRadius;
	float  Thickness;

	float2 DepthFadeRange;
	float  DepthFadeScaleConst;
	float  GISaturation;

	float  GIDistanceCompensation;
	float  GICompensationMaxDist;
	float  _Pad1;
	float  AOPower;

	float  GIStrength;
	float  DepthDisocclusion;
	float  NormalDisocclusion;
	uint   MaxAccumFrames;

	float  BlurRadius;
	float  DistanceNormalisation;
	float2 _Pad2;

	// CameraData mirrors upstream SharedData::CameraData:
	// (1/W_render, 1/H_render, -near/(far-near), -(far*near)/(far-near)).
	// Used by ScreenToViewDepth to recover viewspace Z from reversed-Z depth.
	float4 CameraData;

	// Current frame camera view-inverse. Used by gi.cs to project sample horizon
	// vectors from view-space into world-space before SH evaluation, so SHs don't
	// rotate with the camera. Pushed from BSGraphics::State::cameraState every
	// frame; identity if cameraState unavailable.
	float4x4 CameraViewInverse;
};

SamplerState samplerPointClamp  : register(s0);
SamplerState samplerLinearClamp : register(s1);

///////////////////////////////////////////////////////////////////////////////
// Math constants

namespace Math
{
	static const float PI      = 3.1415926535897932384626433832795f;
	static const float HALF_PI = PI * 0.5f;
	static const float TAU     = PI * 2.0f;
	static const float INV_PI  = 1.0f / PI;
}

// Shared epsilon constants used across gi.cs / radianceDisocc.cs / blur.cs.
static const float EPSILON_LENGTH_SQ   = 1e-8;
static const float EPSILON_WEIGHT_SUM  = 1e-5;
static const float FP_Z = 18.0;

///////////////////////////////////////////////////////////////////////////////
// NaN/Inf scrubbers (XeGTAO original)

#define ISNAN(x) (!(x < 0.f || x > 0.f || x == 0.f))
float filterNaN(float v)  { return ISNAN(v) ? 0 : v; }
float2 filterNaN(float2 v){ return float2(filterNaN(v.x), filterNaN(v.y)); }
float3 filterNaN(float3 v){ return float3(filterNaN(v.x), filterNaN(v.y), filterNaN(v.z)); }
float4 filterNaN(float4 v){ return float4(filterNaN(v.x), filterNaN(v.y), filterNaN(v.z), filterNaN(v.w)); }

float filterInf(float v)  { return isinf(v) ? 0 : v; }
float2 filterInf(float2 v){ return float2(filterInf(v.x), filterInf(v.y)); }
float3 filterInf(float3 v){ return float3(filterInf(v.x), filterInf(v.y), filterInf(v.z)); }
float4 filterInf(float4 v){ return float4(filterInf(v.x), filterInf(v.y), filterInf(v.z), filterInf(v.w)); }

///////////////////////////////////////////////////////////////////////////////
// Resolution macros: switch behaviour for HALF_RES / QUARTER_RES permutations.

#ifdef HALF_RES
#	define RES_MIP 1
#	define READ_DEPTH(tex, px) tex.Load(int3(px, RES_MIP))
#	define FULLRES_LOAD(tex, px, texCoord, samp) tex.SampleLevel(samp, texCoord, 0)
#	define OUT_FRAME_DIM (FrameDim * 0.5)
#	define RCP_OUT_FRAME_DIM (RcpFrameDim * 2)
#	define OUT_FRAME_SCALE (frameScale * 0.5)
#elif defined(QUARTER_RES)
#	define RES_MIP 2
#	define READ_DEPTH(tex, px) tex.Load(int3(px, RES_MIP))
#	define FULLRES_LOAD(tex, px, texCoord, samp) tex.SampleLevel(samp, texCoord, 0)
#	define OUT_FRAME_DIM (FrameDim * 0.25)
#	define RCP_OUT_FRAME_DIM (RcpFrameDim * 4)
#	define OUT_FRAME_SCALE (frameScale * 0.25)
#else
#	define RES_MIP 0
#	define READ_DEPTH(tex, px) tex[px]
#	define FULLRES_LOAD(tex, px, texCoord, samp) tex[px]
#	define OUT_FRAME_DIM FrameDim
#	define RCP_OUT_FRAME_DIM RcpFrameDim
#	define OUT_FRAME_SCALE frameScale
#endif

///////////////////////////////////////////////////////////////////////////////
// View / projection helpers

float3 ScreenToViewPosition(const float2 screenPos, const float viewspaceDepth)
{
	float3 ret;
	ret.xy = (NDCToViewMul * screenPos.xy + NDCToViewAdd) * viewspaceDepth;
	ret.z = viewspaceDepth;
	return ret;
}

// eyeIndex overload kept for ports from upstream stereo-aware shaders; FO4 is
// mono so eyeIndex is always 0 and we always pick the .xy half of NDCToView*.
float3 ScreenToViewPosition(const float2 screenPos, const float viewspaceDepth, const uint eyeIndex)
{
	return ScreenToViewPosition(screenPos, viewspaceDepth);
}

// Recover viewspace depth from reversed-Z screen depth. CameraData layout matches
// upstream SharedData::CameraData; in FO4 we pack it into SSGICB.
float ScreenToViewDepth(const float screenDepth)
{
	return (CameraData.w / (-screenDepth * CameraData.z + CameraData.x));
}

float3 ViewToWorldPosition(const float3 pos, const float4x4 invView)
{
	float4 worldpos = mul(invView, float4(pos, 1));
	return worldpos.xyz / worldpos.w;
}

float3 ViewToWorldVector(const float3 vec, const float4x4 invView)
{
	return mul((float3x3)invView, vec);
}

///////////////////////////////////////////////////////////////////////////////
// Stereo / FrameBuffer stubs (FO4 is single-eye / mono)
//
// Upstream Skyrim CS expects Stereo:: and FrameBuffer:: helpers from its
// SharedData/VR system. FO4 has no equivalent (mono only), so we provide
// trivial pass-through versions that satisfy the upstream call shape.

namespace Stereo
{
	uint  GetEyeIndexFromTexCoord(float2 uv) { return 0u; }
	float2 ConvertFromStereoUV(float2 uv, uint eyeIndex) { return uv; }
	float2 ConvertToStereoUV  (float2 uv, uint eyeIndex) { return uv; }
}

namespace FrameBuffer
{
	// FO4 is mono; ports from upstream Skyrim CS that index FrameBuffer::CameraViewInverse[eyeIndex]
	// must be rewritten to use the plain CameraViewInverse SSGICB field on FO4.

	// Project view-space position to screen-space UV. eyeIndex unused on mono.
	float2 ViewToUV(float3 pos, bool isPosition, uint eyeIndex)
	{
		// Invert ScreenToViewPosition: screen = (view.xy / view.z - add) / mul, in [0,1].
		float2 ndc = (pos.xy / pos.z - NDCToViewAdd) / NDCToViewMul;
		return ndc;
	}
}

///////////////////////////////////////////////////////////////////////////////
// FastMath wrappers - upstream Skyrim CS calls these via FastMath::ACos /
// FastMath::acosFast4. Provide the namespaced spellings here on top of the
// free-function FastACos defined further down.

float _FastSqrtImpl(float x);
float _FastACosImpl(float inX);

namespace FastMath
{
	// XeGTAO low-precision ACos (~0.156583x + pi/2) * sqrt(1-x), wrapped to
	// match the upstream Skyrim CS namespace spelling.
	float ACos(float x) { return _FastACosImpl(x); }
	// blur.cs.hlsl calls acosFast4 which is the same approximation in upstream.
	float acosFast4(float x) { return _FastACosImpl(x); }
}

///////////////////////////////////////////////////////////////////////////////
// Octahedral normal encode/decode (Common/GBuffer.hlsli upstream)
// https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/

namespace GBuffer
{
	half2 OctWrap(half2 v)
	{
		return (1.0h - abs(v.yx)) * (v.xy >= 0.0h ? 1.0h : -1.0h);
	}

	half2 EncodeNormal(half3 n)
	{
		n = -n;
		n /= (abs(n.x) + abs(n.y) + abs(n.z));
		n.xy = n.z >= 0.0h ? n.xy : OctWrap(n.xy);
		n.xy = n.xy * 0.5h + 0.5h;
		return n.xy;
	}

	half3 DecodeNormal(half2 f)
	{
		f = f * 2.0h - 1.0h;
		half3 n = half3(f.x, f.y, 1.0h - abs(f.x) - abs(f.y));
		half t = saturate(-n.z);
		n.xy += n.xy >= 0.0h ? -t : t;
		return -normalize(n);
	}
}

///////////////////////////////////////////////////////////////////////////////
// Rotation helpers (XeGTAO)

float3x3 RotFromToMatrix(float3 from, float3 to)
{
	const float e = dot(from, to);
	const float f = abs(e);

	if (f > float(1.0 - 0.0003))
		return float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1);

	const float3 v = cross(from, to);
	const float h = (1.0) / (1.0 + e);
	const float hvx = h * v.x;
	const float hvz = h * v.z;
	const float hvxy = hvx * v.y;
	const float hvxz = hvx * v.z;
	const float hvyz = hvz * v.y;

	float3x3 mtx;
	mtx[0][0] = e + hvx * v.x;
	mtx[0][1] = hvxy - v.z;
	mtx[0][2] = hvxz + v.y;

	mtx[1][0] = hvxy + v.z;
	mtx[1][1] = e + h * v.y * v.y;
	mtx[1][2] = hvyz - v.x;

	mtx[2][0] = hvxz - v.y;
	mtx[2][1] = hvyz + v.x;
	mtx[2][2] = e + hvz * v.z;

	return mtx;
}

///////////////////////////////////////////////////////////////////////////////
// Spherical Harmonics (SebH HLSL-Spherical-Harmonics, MIT)
// Minimum subset used by gi/radianceDisocc/prefilterRadiance/blur.

#define sh2 float4

namespace SphericalHarmonics
{
	sh2 Zero() { return float4(0, 0, 0, 0); }

	sh2 Evaluate(float3 dir)
	{
		sh2 result;
		result.x =  0.28209479177387814347f;
		result.y = -0.48860251190291992158f * dir.y;
		result.z =  0.48860251190291992158f * dir.z;
		result.w = -0.48860251190291992158f * dir.x;
		return result;
	}

	float Unproject(sh2 functionSh, float3 dir)
	{
		sh2 sh = Evaluate(dir);
		return dot(functionSh, sh);
	}

	float3 Unproject(sh2 functionShX, sh2 functionShY, sh2 functionShZ, float3 dir)
	{
		sh2 sh = Evaluate(dir);
		return float3(dot(functionShX, sh), dot(functionShY, sh), dot(functionShZ, sh));
	}

	sh2 EvaluateCosineLobe(float3 dir)
	{
		sh2 result;
		result.x =  0.8862269254527580137f;
		result.y = -1.0233267079464884885f * dir.y;
		result.z =  1.0233267079464884885f * dir.z;
		result.w = -1.0233267079464884885f * dir.x;
		return result;
	}

	sh2 Add(sh2 shL, sh2 shR) { return shL + shR; }
	sh2 Scale(sh2 sh, float v) { return sh * v; }
	float FuncProductIntegral(sh2 shL, sh2 shR) { return dot(shL, shR); }

	sh2 DiffuseConvolution(sh2 sh)
	{
		sh2 result = sh;
		result.x *= Math::PI;
		result.yzw *= 2.0943951023931954923f;
		return result;
	}

	// http://torust.me/ZH3.pdf (ProfJack adaptation)
	float SHHallucinateZH3Irradiance(sh2 inSH, float3 direction)
	{
		float3 zonalAxis = normalize(float3(inSH.w, inSH.y, inSH.z));
		float ratio = abs(dot(float3(-inSH.w, -inSH.y, inSH.z), zonalAxis));
		ratio /= inSH.x;
		float zonalL2Coeff = inSH.x * (0.08f * ratio + 0.6f * ratio * ratio);
		float fZ = dot(zonalAxis, direction);
		float zhDir = sqrt(5.0f / (16.0f * Math::PI)) * (3.0f * fZ * fZ - 1.0f);
		float result = SphericalHarmonics::FuncProductIntegral(inSH, SphericalHarmonics::EvaluateCosineLobe(direction));
		result += 0.25f * zonalL2Coeff * zhDir;
		return max(0, result);
	}
}

///////////////////////////////////////////////////////////////////////////////
// YCoCg colour space (used by gi/radianceDisocc to compress IL chrominance)

namespace Color
{
	float3 RGBToYCoCg(float3 rgb)
	{
		float y  = dot(rgb, float3(0.25,  0.5,  0.25));
		float co = dot(rgb, float3(0.5,   0.0, -0.5));
		float cg = dot(rgb, float3(-0.25, 0.5, -0.25));
		return float3(y, co, cg);
	}

	float3 YCoCgToRGB(float3 ycocg)
	{
		float y  = ycocg.x;
		float co = ycocg.y;
		float cg = ycocg.z;
		return float3(y + co - cg, y + cg, y - co - cg);
	}

	// FO4's deferred lit-colour buffer is already linear HDR (FP16), so no
	// gamma conversion is needed here. Identity wrapper preserves the upstream
	// call shape from radianceDisocc.cs.hlsl line 146.
	float3 RadianceToLinear(float3 c) { return c; }
}

///////////////////////////////////////////////////////////////////////////////
// FastMath helpers (XeGTAO subset)
// Approximate, branchless, hardware-friendly variants used by gi.cs.hlsl.

float _FastSqrtImpl(float x)
{
	return asfloat(0x1fbd1df5 + (asint(x) >> 1));
}

float _FastACosImpl(float inX)
{
	const float PI_F = 3.141593f;
	const float HALF_PI_F = 1.570796f;
	float x = abs(inX);
	float res = -0.156583f * x + HALF_PI_F;
	res *= _FastSqrtImpl(1.0f - x);
	return (inX >= 0) ? res : PI_F - res;
}

// Free-function aliases retained for shaders that call FastSqrt/FastACos directly.
float FastSqrt(float x) { return _FastSqrtImpl(x); }
float FastACos(float inX) { return _FastACosImpl(inX); }

#endif  // SSGI_V2_COMMON
