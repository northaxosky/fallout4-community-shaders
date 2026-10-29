#if defined(BSDFPREPASS_PS_SOURCE)
// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// Exact AE 1.11.240 PrePass admitted pixel-stage permutations.

#	ifndef ALPHA_TEST
#		define ALPHA_TEST 0
#	endif

#	ifndef MODELSPACENORMALS
#		define MODELSPACENORMALS 0
#	endif

#	ifndef EARLYDEPTH
#		define EARLYDEPTH 0
#	endif

#	ifndef BLEND
#		define BLEND 0
#	endif

#	ifndef SKEW_SPECULAR_ALPHA
#		define SKEW_SPECULAR_ALPHA 0
#	endif

#	ifndef VC
#		define VC 0
#	endif

#	ifndef GLOWMAP
#		define GLOWMAP 0
#	endif

#	ifndef GRADIENT_REMAP
#		define GRADIENT_REMAP 0
#	endif

#	ifndef ADDITIONAL_ALPHA_MASK
#		define ADDITIONAL_ALPHA_MASK 0
#	endif

#	ifndef SKIN_TINT
#		define SKIN_TINT 0
#	endif

#	ifndef BONE_TINTING
#		define BONE_TINTING 0
#	endif

#	ifndef HAIR
#		define HAIR 0
#	endif

#	ifndef EYE
#		define EYE 0
#	endif

#	ifndef FACE
#		define FACE 0
#	endif

#	ifndef TESSELLATE_DISP_HEIGHT
#		define TESSELLATE_DISP_HEIGHT 0
#	endif

#	ifndef LOD_LANDSCAPE
#		define LOD_LANDSCAPE 0
#	endif

#	ifndef MENU_SCREEN
#		define MENU_SCREEN 0
#	endif

#	ifndef INSTANCED
#		define INSTANCED 0
#	endif

#	ifndef TEXTURE
#		define TEXTURE 1
#	endif

#	ifndef TREE_ANIM
#		define TREE_ANIM 0
#	endif

#	ifndef PIPBOY_SCREEN
#		define PIPBOY_SCREEN 0
#	endif

#	ifndef LANDSCAPE
#		define LANDSCAPE 0
#	endif

#	ifndef LAND_LOD_BLEND
#		define LAND_LOD_BLEND 0
#	endif

#	ifndef COMBINED
#		define COMBINED 0
#	endif

#	ifndef DISMEMBERMENT
#		define DISMEMBERMENT 0
#	endif

#	ifndef DISMEMBERMENT_MEATCUFF
#		define DISMEMBERMENT_MEATCUFF 0
#	endif

// MRT4 alpha carries 1 - vertexAO for SSGI and Skylighting (upstream Masks2.x).
#	if defined(SSGI) || defined(SKYLIGHTING)
#		define FO4_PREPASS_VERTEX_AO 1
#	endif

// FO4: loaded landscape replays after objects with alpha-blended writes.
#	if defined(TERRAIN_BLENDING) && LANDSCAPE && !BLEND && !INSTANCED
#		define FO4_TERRAIN_BLENDING
#	endif

#	if defined(OG)
// OG blends have no motion target or tessellated frame-position inputs.
#		define PREPASS_MOTION_VECTOR (!BLEND)
#	else
#		define PREPASS_MOTION_VECTOR (!BLEND || TESSELLATE_DISP_HEIGHT || DISMEMBERMENT_MEATCUFF)
#	endif

// FO4: PARALLAX/LANDSCAPE map to prepass variants that carry world position.
#	if defined(EXTENDED_MATERIALS) && !TESSELLATE_DISP_HEIGHT && !(defined(OG) && BLEND)
#		if TEXTURE && !LANDSCAPE && !MODELSPACENORMALS && !(LOD_LANDSCAPE || SKIN_TINT || HAIR || EYE || FACE || TREE_ANIM || GRASS || DISMEMBERMENT_MEATCUFF || PIPBOY_SCREEN || MENU_SCREEN)
#			define EMAT
#			define EMAT_OBJECT
#		elif LANDSCAPE && !INSTANCED
#			define EMAT
#			define EMAT_LANDSCAPE
#		endif
#	endif

#	if defined(LOD_BLENDING) || defined(TERRAIN_VARIATION) || defined(FO4_PREPASS_VERTEX_AO) || defined(EMAT) || defined(FO4_TERRAIN_BLENDING)
#		include "FO4/FO4ShaderData.hlsli"
#	endif

// FO4: Skyrim mesh exclusions as prepass variants; host rejects object LOD.
#	if defined(TERRAIN_VARIATION) && TEXTURE && !LANDSCAPE && !(LOD_LANDSCAPE || SKIN_TINT || HAIR || EYE || TREE_ANIM || DISMEMBERMENT_MEATCUFF)
#		define TERRAIN_VARIATION_MESH
#	endif

// FO4: the engine never draws instanced terrain, so it stays stock.
#	if defined(TERRAIN_VARIATION) && ((LANDSCAPE && LAND_LOD_BLEND && !INSTANCED) || (defined(LOD_BLENDING) && LOD_LANDSCAPE && !BONE_TINTING))
#		define TERRAIN_VARIATION_LOD
#	endif

#	if defined(TERRAIN_VARIATION) && ((LANDSCAPE && !INSTANCED) || defined(TERRAIN_VARIATION_LOD) || defined(TERRAIN_VARIATION_MESH))
#		include "Common/Random.hlsli"
#		include "TerrainVariation/TerrainVariation.hlsli"
#	endif

#	if defined(FO4_TERRAIN_BLENDING)
#		include "FO4/Depth.hlsli"
#		include "TerrainBlending/TerrainBlending.hlsli"
// FO4: b8 is bound only during landscape replay; unbound reads zero.
cbuffer TerrainBlendingDraw : register(b8)
{
	uint TerrainBlendingPass;  // 1 colour lerp, 2 data replace
	uint3 TerrainBlendingPad;
};
#	endif

#	if defined(TERRAIN_VARIATION) && LANDSCAPE && !INSTANCED
// FO4: live master toggle; off keeps the stock sample.
float4 SampleTerrainVariation(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets)
{
	float4 result;
	[branch] if (FO4SharedData::EnabledTerrainVariation != 0)
		result = StochasticEffect(tex, samp, uv, offsets);
	else result = tex.Sample(samp, uv);
	return result;
}
#	endif

cbuffer PerFrame_CB12 : register(b12)
{
	row_major float4x4 WorldToView;
	row_major float4x4 ViewToClip;
	row_major float4x4 WorldToClip;
	row_major float4x4 ViewToWorld;
	row_major float4x4 ClipToWorld;
	row_major float4x4 ClipToView;
	row_major float4x4 FirstPersonClipToView;
	float4 HairSpecularScaleAndPower;
	float4 HairSpecularShift;
	float4 WorldWetnessAndRainTransition;

	float4 PreviousWorldToClip_row0;
	float4 PreviousWorldToClip_row1;
	float4 PreviousWorldToClip_row2;
	float4 PreviousWorldToClip_row3;

	float4 PosAdjust;
	float4 PreviousPosAdjust;

	float4 UnjitteredWorldToClip_row0;
	float4 UnjitteredWorldToClip_row1;
	float4 UnjitteredWorldToClip_row2;
	float4 UnjitteredWorldToClip_row3;
};

#	if INSTANCED && !LANDSCAPE
struct InstanceRecord
{
	float4 rows[9];
};

cbuffer PerInstance_CB13 : register(b13)
{
	InstanceRecord cb13_instances[455];
};
#	endif

#	if PIPBOY_SCREEN
cbuffer PerPipboy_CB0 : register(b0)
{
	float4 PipboyParams;
};
#	elif LOD_LANDSCAPE && !BONE_TINTING
cbuffer PerLod_CB0 : register(b0)
{
	float4 LandLODParamsPS;  // LOD noise offset.
};
#	elif LANDSCAPE && LAND_LOD_BLEND
cbuffer PerLandLod_CB0 : register(b0)
{
	float4 LandLODParamsPS;
};
#	endif

cbuffer PerCall_CB2 : register(b2)
{
	float4 SpecularParam;       // Scroll anchor and alpha.
	float4 EmissiveColor_row0;  // Specular tint.
#	if BLEND
	float4 AlphaScale_row0;  // Blend parameters.
#	endif
#	if SKIN_TINT
	float4 TintColor;
#	endif
#	if GRADIENT_REMAP
	float4 TintColor_row0;  // Gradient-remap parameters.
#	elif HAIR
#		if BLEND
	float4 AlphaScale_row1;  // Hair parameters.
#		else
	float4 EmissiveColor_row1;  // Hair parameters.
#		endif
#	endif
#	if COMBINED
	float4 AlphaScale_row1;  // Scroll delta.
#		define SpecularParam2_row0 AlphaScale_row1
#	elif HAIR && GRADIENT_REMAP && !BLEND
	float4 TintColor_row1;  // Scroll delta.
#		define SpecularParam2_row0 TintColor_row1
#	elif HAIR && !BLEND
	float4 EmissiveColor_row2;  // Scroll delta.
#		define SpecularParam2_row0 EmissiveColor_row2
#	else
	float4 SpecularParam2_row0;  // Scroll delta.
#	endif
#	if LANDSCAPE
	float4 WetnessControl_Reflect;  // Landscape material gate.
#	else
	// The host locates this lane as the register before WetnessControl_Spec; keep the two adjacent (ShaderSubclassHooks.cpp).
	float4 PrepassFeatureLane;
#	endif
#	if LANDSCAPE
	float4 WetnessControl_Spec;   // Landscape flags.
	float4 WetnessControl_Spec1;  // Landscape flags.
	float4 WetnessControl_Spec2;  // Landscape flags.
	float4 WetnessControl_Spec3;  // Landscape flags.
#	else
	float4 WetnessControl_Spec;  // Material flags.
#	endif
#	if EYE && DISMEMBERMENT
	float4 cb2_cut_tangent;
	float4 WetnessControl_Spec2;       // Cut-plane bitangent.
	float4 WetnessControl_Spec3_row0;  // Cut-plane normal.
	float4 WetnessControl_Spec3_row1;
#	elif EYE && DISMEMBERMENT_MEATCUFF
	float4 cb2_cut_tangent;
	float4 cb2_cut_bitangent;
#	endif
#	if ADDITIONAL_ALPHA_MASK
	float4 cb2_alpha_mask_params;
#	endif
#	if BONE_TINTING
	float4 cb2_bone_tint_params;
#	endif
	float4 cb2_material_id_and_smoothness;
};

#	if EYE && COMBINED
// Combined eye draws take per-instance surface state from t4/t6, not PerCall_CB2.
struct EyeInstance
{
	float4 scroll;
	float3 specularTint;
	float4 alphaRefAndFlags;
	float fadeFlag;
	float4 pad_12_15;
	uint3 textureSlices;
	uint pad_19;
	uint gradientSlice;
	float gradientV;
	float smoothness;
};

struct EyeMaterialRecord
{
	float4 pad_0_23[6];
	float materialIndex;
};

StructuredBuffer<EyeInstance> g_bEyeInstances : register(t4);
StructuredBuffer<EyeMaterialRecord> g_bEyeMaterials : register(t6);

Texture2DArray<float4> g_tEyeAlbedo : register(t0);
Texture2DArray<float4> g_tEyeNormalMap : register(t1);
Texture2DArray<float4> g_tEyeMaterial : register(t2);

SamplerState g_sAlbedo : register(s0);
SamplerState g_sNormalMap : register(s1);
SamplerState g_sMaterial : register(s2);
#	elif TEXTURE && !LANDSCAPE
Texture2D<float4> g_tAlbedo : register(t0);
Texture2D<float4> g_tNormalMap : register(t1);
#		if HAIR && !BLEND
Texture2D<float4> g_tHairMaterial : register(t3);
#		else
Texture2D<float4> g_tMaterial : register(t2);
#		endif

SamplerState g_sAlbedo : register(s0);
SamplerState g_sNormalMap : register(s1);
#		if HAIR && !BLEND
SamplerState g_sHairMaterial : register(s3);
#		else
SamplerState g_sMaterial : register(s2);
#		endif
#	endif
#	if GLOWMAP && (!HAIR || BLEND)
Texture2D<float4> g_tGlowMap : register(t3);
SamplerState g_sGlowMap : register(s3);
#	endif
#	if GRADIENT_REMAP && EYE && COMBINED
Texture2DArray<float4> g_tEyeGradient : register(t5);
SamplerState g_sGradient : register(s5);
#	elif GRADIENT_REMAP
Texture2D<float4> g_tGradient : register(t5);
SamplerState g_sGradient : register(s5);
#	endif
#	if ADDITIONAL_ALPHA_MASK
Texture2D<float4> g_tAlphaMask : register(t12);
SamplerState g_sAlphaMask : register(s12);
Texture2D<float4> g_tDitherMask : register(t15);
#	endif
#	if BONE_TINTING
Texture2D<float4> g_tBoneIndex : register(t13);
SamplerState g_sBoneIndex : register(s13);
Texture2D<float4> g_tBoneTint : register(t14);
SamplerState g_sBoneTint : register(s14);
#	endif
#	if FACE
Texture2D<float4> g_tFaceDetail : register(t8);
SamplerState g_sFaceDetail : register(s8);
#	endif
#	if EYE && (DISMEMBERMENT || DISMEMBERMENT_MEATCUFF)
Texture2D<float4> g_tCutAlbedo : register(t9);
SamplerState g_sCutAlbedo : register(s9);
Texture2D<float4> g_tCutNormalMap : register(t10);
SamplerState g_sCutNormalMap : register(s10);
Texture2D<float4> g_tCutMaterial : register(t11);
SamplerState g_sCutMaterial : register(s11);
#	endif
#	if PIPBOY_SCREEN
Texture2D<float4> g_tPipboyScreen : register(t4);
SamplerState g_sPipboyScreen : register(s4);
#	elif MENU_SCREEN
Texture2D<float4> g_tMenuOverlay : register(t4);
SamplerState g_sMenuOverlay : register(s4);
#	endif
#	if LOD_LANDSCAPE && !BONE_TINTING
Texture2D<float4> g_tLodColorNoise : register(t13);
SamplerState g_sLodColorNoise : register(s13);
Texture2D<float4> g_tLodNormalNoise : register(t15);
SamplerState g_sLodNormalNoise : register(s15);
#	endif

#	if LANDSCAPE && INSTANCED
struct LandInstanceRecord
{
	float4 pad_0_3;
	float2 pad_4_5;
	int lodSlice;
	int layerSlices[12];
};

StructuredBuffer<LandInstanceRecord> g_bLandInstances : register(t4);

Texture2DArray<float4> g_tLandAlbedo : register(t0);
Texture2DArray<float4> g_tLandNormal : register(t1);
Texture2DArray<float4> g_tLandMaterial : register(t2);
SamplerState g_sLandAlbedo : register(s0);
SamplerState g_sLandNormal : register(s1);
SamplerState g_sLandMaterial : register(s2);
#	elif LANDSCAPE
Texture2D<float4> g_tLandAlbedo[4] : register(t0);
Texture2D<float4> g_tLandNormal[4] : register(t4);
Texture2D<float4> g_tLandMaterial[4] : register(t8);
SamplerState g_sLandAlbedo[4] : register(s0);
SamplerState g_sLandNormal[4] : register(s4);
SamplerState g_sLandMaterial[4] : register(s8);
#	endif

#	if LANDSCAPE && LAND_LOD_BLEND && INSTANCED
Texture2DArray<float4> g_tLandLodAlbedo : register(t3);
SamplerState g_sLandLodAlbedo : register(s3);
#	elif LANDSCAPE && LAND_LOD_BLEND
Texture2D<float4> g_tLandLodAlbedo : register(t14);
SamplerState g_sLandLodAlbedo : register(s14);
#	endif

#	if LANDSCAPE && LAND_LOD_BLEND
Texture2D<float4> g_tLandColorNoise : register(t13);
SamplerState g_sLandColorNoise : register(s13);
Texture2D<float4> g_tLandNormalNoise : register(t15);
SamplerState g_sLandNormalNoise : register(s15);
#	endif

#	if defined(EMAT)
#		include "Common/Random.hlsli"

// FO4: landscape spans 2x Skyrim's units per UV (uv/48 vs /96), so halve depth.
static const float FO4DepthScale = 0.5;

// FO4: no height marker; need a mipped _s whose alpha doesn't average 1.
float HeightScaleOf(Texture2D<float4> a_texture, SamplerState a_sampler)
{
	uint width, height, mips;
	a_texture.GetDimensions(0, width, height, mips);
	if ((max(width, height) >> (mips - 1)) > 4)
		return 0.0;
	return a_texture.SampleLevel(a_sampler, 0.5, mips - 1).a < 254.5 / 255.0 ? FO4DepthScale : 0.0;
}
#	endif
#	if defined(EMAT_OBJECT)
// FO4: LANDSCAPE is always defined as 0/1; the kernel tests defined().
#		undef LANDSCAPE
#		include "ExtendedMaterials/ExtendedMaterials.hlsli"
#		define LANDSCAPE 0
#	elif defined(EMAT_LANDSCAPE)
// FO4: four layers, height in each _s alpha; layers 5-6 weigh zero.
struct EMLandscapeInput
{
	float4 LandBlendWeights1;
	float4 LandBlendWeights2;
};
// FO4: no Terrain Helper displacement layers.
namespace Permutation
{
	static const uint ExtraFeatureDescriptor = 0;
	namespace ExtraFeatureFlags
	{
		static const int THLand0HasDisplacement = (1 << 0);
		static const int THLand1HasDisplacement = (1 << 1);
		static const int THLand2HasDisplacement = (1 << 2);
		static const int THLand3HasDisplacement = (1 << 3);
		static const int THLand4HasDisplacement = (1 << 4);
		static const int THLand5HasDisplacement = (1 << 5);
		static const int THLandHasDisplacement = (1 << 9);
	}
}
#		define PS_INPUT EMLandscapeInput
#		define TexColorSampler g_tLandMaterial[0]
#		define TexLandColor2Sampler g_tLandMaterial[1]
#		define TexLandColor3Sampler g_tLandMaterial[2]
#		define TexLandColor4Sampler g_tLandMaterial[3]
#		define TexLandColor5Sampler g_tLandMaterial[3]
#		define TexLandColor6Sampler g_tLandMaterial[3]
#		define TexLandTHDisp0Sampler g_tLandMaterial[0]
#		define TexLandTHDisp1Sampler g_tLandMaterial[1]
#		define TexLandTHDisp2Sampler g_tLandMaterial[2]
#		define TexLandTHDisp3Sampler g_tLandMaterial[3]
#		define TexLandTHDisp4Sampler g_tLandMaterial[3]
#		define TexLandTHDisp5Sampler g_tLandMaterial[3]
#		define SampTerrainParallaxSampler g_sLandMaterial[0]
#		include "ExtendedMaterials/ExtendedMaterials.hlsli"
#		undef PS_INPUT
#		undef TexColorSampler
#		undef TexLandColor2Sampler
#		undef TexLandColor3Sampler
#		undef TexLandColor4Sampler
#		undef TexLandColor5Sampler
#		undef TexLandColor6Sampler
#		undef TexLandTHDisp0Sampler
#		undef TexLandTHDisp1Sampler
#		undef TexLandTHDisp2Sampler
#		undef TexLandTHDisp3Sampler
#		undef TexLandTHDisp4Sampler
#		undef TexLandTHDisp5Sampler
#		undef SampTerrainParallaxSampler
#	endif

struct PS_INPUT
{
	float4 position: SV_POSITION;
#	if TESSELLATE_DISP_HEIGHT
	float2 uv: TEXCOORD0;
#	endif
#	if EYE && DISMEMBERMENT
	float2 cutCoord: TEXCOORD4;
#	endif
#	if TESSELLATE_DISP_HEIGHT
#		if VC
	float4 vertexColor: COLOR0;
#		endif
	float3 tangent: TEXCOORD1;
	float3 bitangent: TEXCOORD2;
	float3 normal: TEXCOORD3;
#	endif
#	if EYE && DISMEMBERMENT
	float2 dismemberWeight: TEXCOORD5;
#	endif
#	if TESSELLATE_DISP_HEIGHT
#		if PREPASS_MOTION_VECTOR
	float4 curr_pos_u: POSITION1;
	float4 tessellatedPosition: POSITION2;
#		endif
#	else
	float3 tangent: TEXCOORD0;
#		if FACE
	float faceMask: TEXCOORD6;
#		endif
	float3 bitangent: TEXCOORD1;
	float3 normal: TEXCOORD2;
	float4 curr_pos_u: TEXCOORD3;
	float4 prev_pos_v: TEXCOORD4;
#		if VC
	float4 vertexColor: COLOR0;
#		endif
#		if PIPBOY_SCREEN
	float3 pipboyView: TEXCOORD6;
#		endif
#		if DISMEMBERMENT_MEATCUFF
	float2 dismember: TEXCOORD6;
	float3 cuffRadial: TEXCOORD7;
	float3 cuffAxis: TEXCOORD8;
	float3 cuffNormal: TEXCOORD9;
#		endif
#	endif
#	if BONE_TINTING
	float4 boneTint: COLOR1;
#	endif
#	if INSTANCED || (EYE && COMBINED)
	nointerpolation uint instanceIndex: COLOR2;
#	endif
#	if LANDSCAPE
	float2 lodAlbedoUV: TEXCOORD6;
	float4 layerWeights: TEXCOORD7;
	float3 lodBlend: TEXCOORD8;
#	endif
#	if LOD_LANDSCAPE
	float2 lodUV: TEXCOORD9;
#	endif
	uint isFrontFace: SV_IsFrontFace;
};

struct PS_OUTPUT
{
	float4 albedo: SV_Target0;
#	if BLEND || defined(FO4_TERRAIN_BLENDING)
	float4 normalOct: SV_Target1;
#	else
	float2 normalOct: SV_Target1;
#	endif
	float4 material: SV_Target2;
	float4 auxA: SV_Target3;
#	if BLEND || defined(FO4_PREPASS_VERTEX_AO) || defined(FO4_TERRAIN_BLENDING)
	float4 specTint: SV_Target4;
#	else
	float3 specTint: SV_Target4;
#	endif
#	if PREPASS_MOTION_VECTOR
#		if defined(FO4_TERRAIN_BLENDING)
	float4 motionVec: SV_Target5;
#		else
	float2 motionVec: SV_Target5;
#		endif
#	endif
#	if defined(EXTENDED_MATERIALS)
	// FO4: upstream shades parallax shadows forward; DFLight reads them from here.
	float4 parallaxShadow: SV_Target6;
#	endif
#	if defined(SUBSURFACE_SCATTERING)
	// FO4: upstream marks the Human profile in the lighting mask; SSS reads it from here.
	float4 sssProfile: SV_Target7;
#	endif
};

// FO4: early depth would write depth for pixels the blend clip discards.
#	if EARLYDEPTH && !TREE_ANIM && !DISMEMBERMENT_MEATCUFF && !defined(FO4_TERRAIN_BLENDING)
[earlydepthstencil]
#	endif
	PS_OUTPUT main(PS_INPUT input) {
#	if defined(FO4_TERRAIN_BLENDING)
		PS_OUTPUT output = (PS_OUTPUT)0;
		float terrainBlendFactor = 1.0;
		[branch] if (TerrainBlendingPass != 0)
		{
			const float depthSampled = TerrainBlending::TerrainBlendingMaskTexture[uint2(input.position.xy)].x;
			const float depthSampledLinear = SharedData::GetScreenDepth(FO4Depth::ProjectionDepth(depthSampled));
			const float depthPixelLinear = SharedData::GetScreenDepth(FO4Depth::ProjectionDepth(input.position.z));
			terrainBlendFactor = saturate((depthSampledLinear - depthPixelLinear) / 10.0);
			if (input.position.z == depthSampled)
				terrainBlendFactor = 1;
			// The data draw owns pixels where terrain is at least half in.
			clip(terrainBlendFactor - (TerrainBlendingPass == 1 ? 1.0 / 255.0 : 0.5));
		}
#	else
	PS_OUTPUT output;
#	endif
#	if defined(EXTENDED_MATERIALS)
		float parallaxShadow = 1.0;
#	endif
#	if defined(TERRAIN_VARIATION_LOD) || defined(EMAT)
		float screenNoise = Random::InterleavedGradientNoise(input.position.xy, SharedData::FrameCount);
#	endif
#	if defined(LOD_BLENDING) && (LANDSCAPE || GRASS) && VC
		// FO4: vertex colors moved from Lighting/RunGrass; off keeps stock colors.
		if (FO4SharedData::EnabledLODBlending != 0) {
			if (SharedData::lodBlendingSettings.DisableTerrainVertexColors)
				input.vertexColor.xyz = 1;
#		if LANDSCAPE
			else
				input.vertexColor.xyz /= max(max(max(input.vertexColor.x, input.vertexColor.y), input.vertexColor.z), EPSILON_DIVISION);
#		endif
		}
#	endif

#	if GRASS && VC && defined(FO4_PREPASS_VERTEX_AO)
		// FO4: RunGrass:467-470 split; stock albedo while no consumer is on.
		float grassVertexAO = max(max(input.vertexColor.x, input.vertexColor.y), input.vertexColor.z);
		[branch] if (FO4SharedData::EnabledSkylighting != 0 || FO4SharedData::EnabledScreenSpaceGI != 0)
			input.vertexColor.xyz /= max(grassVertexAO, EPSILON_DIVISION);
#	endif

#	if TESSELLATE_DISP_HEIGHT
		float2 uv = input.uv;
#	else
	float2 uv = float2(input.curr_pos_u.w, input.prev_pos_v.w);
#	endif
#	if ADDITIONAL_ALPHA_MASK
		if (cb2_alpha_mask_params.y) {
			int2 ditherTexel = (int2)(input.position.xy % 4.0);
			float ditherValue = g_tDitherMask.Load(int3(ditherTexel, 0)).x;
			clip(cb2_alpha_mask_params.y * (0.5 - ditherValue) + cb2_alpha_mask_params.z - 0.5);
		}
		if (cb2_alpha_mask_params.w) {
			float maskAlpha = g_tAlphaMask.Sample(
											  g_sAlphaMask,
											  uv)
		                          .w;
			clip(cb2_alpha_mask_params.x - maskAlpha);
		}
#	endif
#	if LOD_LANDSCAPE && !BONE_TINTING
		float4 lodNoiseUV = (input.lodUV.xyxy + LandLODParamsPS.zwzw) * float4(0.000250, 0.000250, 0.000350, 0.000350);
		float3 lodColorNoise = g_tLodColorNoise.Sample(
												   g_sLodColorNoise, lodNoiseUV.xy)
	                               .xyz;
		float2 lodNormalNoiseXY = g_tLodNormalNoise.Sample(
													   g_sLodNormalNoise, lodNoiseUV.zw)
	                                  .xy;
		lodNormalNoiseXY = lodNormalNoiseXY * 2.0 - 1.0;
		lodColorNoise = lodColorNoise * (34.0 / 9.0) - 2.006;
#	endif
#	if DISMEMBERMENT_MEATCUFF && !GLOWMAP
		float3 tNorm = normalize(input.tangent);
		float3 bNorm = normalize(input.bitangent);
		float3 nGeom = normalize(input.normal);
#	endif
#	if defined(TERRAIN_VARIATION_MESH)
		// Lattice cell comes from the geometric UV, before the parallax block below rewrites uv.
		// FO4: the host flags eligible meshes in PrepassFeatureLane.y.
		const bool applyMeshTV = PrepassFeatureLane.y != 0.0 && FO4SharedData::EnabledTerrainVariation != 0 && SharedData::terrainVariationSettings.enableMeshSupport;
		StochasticOffsets meshOffset = (StochasticOffsets)0;
		[branch] if (applyMeshTV)
		{
			meshOffset = ComputeStochasticOffsetsMesh(uv, input.position.xy);
		}
#		define MESH_TV_SAMPLE(DEST, TEX, SAMP, UV)                 \
			float4 DEST;                                            \
			[branch] if (applyMeshTV)                               \
			{                                                       \
				DEST = StochasticEffect(TEX, SAMP, UV, meshOffset); \
			}                                                       \
			else                                                    \
			{                                                       \
				DEST = TEX.Sample(SAMP, UV);                        \
			}
#	else
#		if defined(EMAT_OBJECT)
	StochasticOffsets meshOffset = (StochasticOffsets)0;
	const bool applyMeshTV = false;
#		endif
#		define MESH_TV_SAMPLE(DEST, TEX, SAMP, UV) float4 DEST = TEX.Sample(SAMP, UV)
#	endif

#	if defined(EMAT)
		// FO4: tangent rows are view space; b12 c12-c14 hold view-to-world.
		float3x3 viewToWorld = (float3x3)ViewToWorld;
		float3 cameraPosition = ViewToWorld._14_24_34;
		float3 worldPosition = input.curr_pos_u.xyz;
		float3 viewPosition = mul(worldPosition - cameraPosition, viewToWorld);
		float3 viewDirection = normalize(cameraPosition - worldPosition);
		float3x3 tbnTr = transpose(mul(viewToWorld, float3x3(input.tangent, input.bitangent, input.normal)));
		tbnTr[0] = normalize(tbnTr[0]);
		tbnTr[1] = normalize(tbnTr[1]);
		tbnTr[2] = normalize(tbnTr[2]);

		float pixelOffset = 0;
#		if defined(EMAT_LANDSCAPE)
		DisplacementParams displacementParams[6];
		displacementParams[0].DisplacementScale = 1.f;
		displacementParams[0].DisplacementOffset = 0.f;
		displacementParams[0].HeightScale = 1;
		displacementParams[0].FlattenAmount = 0;
#		else
		float mipLevel = 0;
		DisplacementParams displacementParams;
		displacementParams.DisplacementScale = 1.f;
		displacementParams.DisplacementOffset = 0.f;
		displacementParams.HeightScale = 1;
		displacementParams.FlattenAmount = 0;
#		endif

		float curvature = 0;
		float normalSmoothness = 0;
		float3 vertexNormal = tbnTr[2];
		if (SharedData::extendedMaterialSettings.EnableParallaxWarpingFix
#		if defined(EMAT_LANDSCAPE)
			&& SharedData::extendedMaterialSettings.EnableTerrainParallax
#		else
			// FO4: only the marcher below reads this; upstream runs it on all EMAT draws.
			&& SharedData::extendedMaterialSettings.EnableParallax && PrepassFeatureLane.z != 0.0
#		endif
		) {
			float3 ndx = ddx(vertexNormal);
			float3 ndy = ddy(vertexNormal);
			float3 fdx = ddx(worldPosition);
			float3 fdy = ddy(worldPosition);
			float fragSize = rcp(length(max(abs(fdx), abs(fdy))));
			float normalDelta = length(max(abs(ndx), abs(ndy))) * fragSize;
			float3 flatWorldNormal = normalize(-cross(fdx, fdy));
			normalSmoothness = saturate(1.0 - dot(vertexNormal, flatWorldNormal));
#		if defined(EMAT_LANDSCAPE)
			curvature = sqrt(normalDelta);
			displacementParams[0].HeightScale = saturate(1.0 - curvature);
			displacementParams[0].FlattenAmount = (normalSmoothness + curvature);
#		else
			// Objects: keep HeightScale; flatten only grazing, bent faces.
			curvature = normalDelta;
			float facing = saturate(dot(vertexNormal, viewDirection));
			float grazing = 1.0 - facing;
			float bend = saturate(normalSmoothness + curvature);
			float silhouette = saturate(grazing - facing);
			// Cap FlattenAmount: 1/facing explodes at grazing angles.
			displacementParams.FlattenAmount = saturate(silhouette * bend * rcp(max(facing, 0.0625)));
#		endif
		}
#	endif

#	if defined(EMAT_OBJECT)
		// FO4: PrepassFeatureLane.z marks non-LOD meshes whose _s format stores alpha.
		[branch] if (SharedData::extendedMaterialSettings.EnableParallax && PrepassFeatureLane.z != 0.0)
		{
			displacementParams.HeightScale *= HeightScaleOf(g_tMaterial, g_sMaterial);
			[branch] if (displacementParams.HeightScale > 0.0)
			{
				mipLevel = ExtendedMaterials::GetMipLevel(uv, g_tMaterial);
				uv = ExtendedMaterials::GetParallaxCoords(viewPosition.z, uv, mipLevel, viewDirection, tbnTr, screenNoise, g_tMaterial, g_sMaterial, 3, displacementParams, applyMeshTV, meshOffset, pixelOffset);
				const float parallaxShadowQuality = viewPosition.z < ExtendedMaterials::ParallaxCheapDistance ? ExtendedMaterials::ParallaxNearShadowQuality : ExtendedMaterials::ParallaxFarShadowQuality;
				[branch] if (SharedData::extendedMaterialSettings.EnableShadows && parallaxShadowQuality > 0.0)
				{
					float sh0;
#		if defined(TERRAIN_VARIATION_MESH)
					[branch] if (applyMeshTV) sh0 = StochasticHeightChannel(g_tMaterial, g_sMaterial, uv, mipLevel, 3, meshOffset);
					else sh0 = g_tMaterial.SampleLevel(g_sMaterial, uv, mipLevel).a;
#		else
					sh0 = g_tMaterial.SampleLevel(g_sMaterial, uv, mipLevel).a;
#		endif
					const float3 dirLightDirectionTS = mul(tbnTr, SharedData::DirLightDirection.xyz);
					parallaxShadow = ExtendedMaterials::GetParallaxSoftShadowMultiplier(uv, mipLevel, dirLightDirectionTS, sh0, g_tMaterial, g_sMaterial, 3, parallaxShadowQuality, screenNoise, displacementParams, applyMeshTV, meshOffset);
				}
			}
		}
#	endif

#	if LANDSCAPE
#		if INSTANCED
		int landSlices[12] = g_bLandInstances[input.instanceIndex].layerSlices;
#		endif
#		if defined(TERRAIN_VARIATION) && !INSTANCED
		// FO4: lodAlbedoUV is layerUV/48, Skyrim's zw is /96.
		StochasticOffsets sharedOffset = (StochasticOffsets)0;
		[branch] if (FO4SharedData::EnabledTerrainVariation != 0)
			sharedOffset = ComputeStochasticOffsets(input.lodAlbedoUV * 0.5, input.position.xy);
#		elif defined(EMAT_LANDSCAPE)
		StochasticOffsets sharedOffset = (StochasticOffsets)0;
#		endif
#		if defined(EMAT_LANDSCAPE)
		// FO4: four layers, normalised as upstream does before the march.
		float landWeightSum = dot(input.layerWeights, 1.0);
		EMLandscapeInput landInput;
		landInput.LandBlendWeights1 = landWeightSum > 0.0 ? input.layerWeights / landWeightSum : input.layerWeights;
		landInput.LandBlendWeights2 = 0.0;
		[branch] if (SharedData::extendedMaterialSettings.EnableTerrainParallax)
		{
			float mipLevels[6];
			float terrainMaxTexDim = 0.0;
			ExtendedMaterials::InitializeTerrainMipLevels(uv, mipLevels, terrainMaxTexDim);

			displacementParams[1] = displacementParams[0];
			displacementParams[2] = displacementParams[0];
			displacementParams[3] = displacementParams[0];
			displacementParams[4] = displacementParams[0];
			displacementParams[5] = displacementParams[0];
			[unroll] for (uint heightLayer = 0; heightLayer < 4; ++heightLayer)
				displacementParams[heightLayer]
					.HeightScale *= HeightScaleOf(g_tLandMaterial[heightLayer], g_sLandMaterial[heightLayer]);

			float weights[6];
			weights[0] = weights[1] = weights[2] = weights[3] = weights[4] = weights[5] = 0.0;

			const bool doTerrainPom = ExtendedMaterials::TerrainHasAnyDisplacement() &&
		                              ExtendedMaterials::TerrainMaxWeightedHeightScale(landInput, displacementParams) > 0.01;
			[branch] if (doTerrainPom)
			{
				uv = ExtendedMaterials::GetParallaxCoords(landInput, viewPosition.z, uv, mipLevels, terrainMaxTexDim, viewDirection, tbnTr, screenNoise, displacementParams, sharedOffset, pixelOffset, weights);
			}
			else if (SharedData::extendedMaterialSettings.EnableHeightBlending)
			{
				float unusedHeight;
				unusedHeight = ExtendedMaterials::GetTerrainHeight(screenNoise, landInput, uv, mipLevels, displacementParams, 1.0, landInput.LandBlendWeights1, landInput.LandBlendWeights2.xy, sharedOffset, weights);
			}

			if (SharedData::extendedMaterialSettings.EnableHeightBlending) {
				input.layerWeights = float4(weights[0], weights[1], weights[2], weights[3]);
				landInput.LandBlendWeights1 = input.layerWeights;
			}

			const float terrainDirectionalShadowQuality = viewPosition.z < ExtendedMaterials::ParallaxCheapDistance ? ExtendedMaterials::ParallaxNearShadowQuality : ExtendedMaterials::ParallaxFarShadowQuality;
			const bool hasTerrainParallaxShadow =
				viewPosition.z < ExtendedMaterials::ParallaxCheapDistance &&
				ExtendedMaterials::TerrainHasAnyDisplacement() &&
				ExtendedMaterials::TerrainMaxWeightedHeightScale(landInput, displacementParams) > 0.01;
			// FO4: no G-buffer normal yet, so the geometric normal gates the sun angle.
			const bool sunFacing = dot(tbnTr[2], SharedData::DirLightDirection.xyz) > 0.0;
			[branch] if ((doTerrainPom || hasTerrainParallaxShadow) && sunFacing && SharedData::extendedMaterialSettings.EnableShadows && terrainDirectionalShadowQuality > 0.0)
			{
				float terrainShadowMipLevels[6];
				[unroll] for (uint terrainMipIndex = 0; terrainMipIndex < 6; terrainMipIndex++)
					terrainShadowMipLevels[terrainMipIndex] = min(mipLevels[terrainMipIndex], ExtendedMaterials::TerrainParallaxShadowMaxMipLevel);
				float sh0;
				if (ExtendedMaterials::ComputeTerrainParallaxShadowBaseHeight(landInput, uv, terrainShadowMipLevels, terrainDirectionalShadowQuality, screenNoise, displacementParams, sharedOffset, sh0)) {
					const float3 dirLightDirectionTS = mul(tbnTr, SharedData::DirLightDirection.xyz);
					parallaxShadow = ExtendedMaterials::EvaluateTerrainDirectionalParallaxShadowMultiplier(landInput, uv, terrainShadowMipLevels, dirLightDirectionTS, terrainDirectionalShadowQuality, screenNoise, displacementParams, sharedOffset, sh0);
				}
			}
		}
#		endif
#		if defined(TERRAIN_VARIATION) && !INSTANCED
#			define SampleTerrain(TEX, SAMP, UV, OFFSET) SampleTerrainVariation(TEX, SAMP, UV, OFFSET)
#		else
#			define SampleTerrain(TEX, SAMP, UV, OFFSET) TEX.Sample(SAMP, UV)
#		endif
		bool4 landActive = (input.layerWeights > 0.0);
		float3 landAlbedo = 0.0;
		float2 landMaterial = 0.0;
		float3 landNormal = 0.0;
		[unroll] for (int landLayer = 0; landLayer < 4; ++landLayer)
		{
			if (landActive[landLayer]) {
				float landWeight = input.layerWeights[landLayer];
#		if INSTANCED
				float3 landAlbedoUV = float3(uv, landSlices[landLayer * 3]);
				landAlbedo += landWeight * g_tLandAlbedo.Sample(
															g_sLandAlbedo, landAlbedoUV)
			                                   .xyz;
				float3 landNormalUV = float3(uv, landSlices[landLayer * 3 + 1]);
				float2 landNormalXY = g_tLandNormal.Sample(
													   g_sLandNormal, landNormalUV)
			                              .xy;
#		else
				landAlbedo += landWeight * SampleTerrain(g_tLandAlbedo[landLayer], g_sLandAlbedo[landLayer], uv, sharedOffset).xyz;
				float2 landNormalXY = SampleTerrain(g_tLandNormal[landLayer], g_sLandNormal[landLayer], uv, sharedOffset).xy;
#		endif
				landNormalXY = landNormalXY * 2.0 - 1.0;
				float landNormalZ = sqrt(
					1.0 - min(dot(landNormalXY, landNormalXY), 1.0));
				landNormal += landWeight * float3(landNormalXY, landNormalZ);
#		if INSTANCED
				float3 landMaterialUV = float3(uv, landSlices[landLayer * 3 + 2]);
				landMaterial += landWeight * g_tLandMaterial.Sample(
																g_sLandMaterial, landMaterialUV)
			                                     .xy;
#		else
				landMaterial += landWeight * SampleTerrain(g_tLandMaterial[landLayer], g_sLandMaterial[landLayer], uv, sharedOffset).xy;
#		endif
			}
		}
#		undef SampleTerrain
#		if LAND_LOD_BLEND && INSTANCED
		float3 landLodAlbedo = g_tLandLodAlbedo.Sample(
												   g_sLandLodAlbedo,
												   float3(input.lodAlbedoUV,
													   g_bLandInstances[input.instanceIndex].lodSlice))
	                               .xyz;
#		elif LAND_LOD_BLEND
#			if defined(TERRAIN_VARIATION)
		float3 landLodAlbedo;
		[branch] if (FO4SharedData::EnabledTerrainVariation == 0)
			landLodAlbedo = g_tLandLodAlbedo.Sample(g_sLandLodAlbedo, input.lodAlbedoUV).xyz;
		else[branch] if (SharedData::terrainVariationSettings.enableLODTerrainTilingFix)
		{
			// FO4: prepass albedo is raw, so the DiffuseToWorking result is unused.
			float4 sampledLodLandColor;
			StochasticSampleLOD(screenNoise, g_tLandLodAlbedo, g_sLandLodAlbedo, input.lodAlbedoUV, sampledLodLandColor);
			landLodAlbedo = sampledLodLandColor.xyz;
		}
		else landLodAlbedo = g_tLandLodAlbedo.SampleBias(g_sLandLodAlbedo, input.lodAlbedoUV, SharedData::MipBias).xyz;
#			else
		float3 landLodAlbedo = g_tLandLodAlbedo.Sample(
												   g_sLandLodAlbedo, input.lodAlbedoUV)
	                               .xyz;
#			endif
#		endif
#		if defined(LOD_BLENDING) && LAND_LOD_BLEND
		// FO4: applied to the raw sampled LOD albedo; there is no ColorToLinear*VanillaDiffuseColorMult step here.
		if (FO4SharedData::EnabledLODBlending != 0)
			landLodAlbedo = pow(abs(landLodAlbedo), SharedData::lodBlendingSettings.LODTerrainGamma) * SharedData::lodBlendingSettings.LODTerrainBrightness;
#		endif
#		if LAND_LOD_BLEND
		float4 landNoiseUV = (input.lodBlend.xyxy + LandLODParamsPS.zwzw) * float4(0.000250, 0.000250, 0.000350, 0.000350);
		float3 landColorNoise = g_tLandColorNoise.Sample(
													 g_sLandColorNoise, landNoiseUV.xy)
	                                .xyz;
		landColorNoise = landColorNoise * (34.0 / 9.0) - 2.006;
		float3 landLodTangentRaw = cross(
			landNormal, cross(landNormal, float3(1.0, 0.0, 0.0)));
		float3 landLodBitangentRaw = cross(landNormal, landLodTangentRaw);
		float3 landLodTangent = normalize(landLodTangentRaw);
		float3 landLodBitangent = normalize(landLodBitangentRaw);
		float3 landLodAxis = normalize(landNormal);
		float2 landNoiseXY = g_tLandNormalNoise.Sample(
												   g_sLandNormalNoise, landNoiseUV.zw)
	                             .xy;
		landNoiseXY = landNoiseXY * 2.0 - 1.0;
		float landNoiseZ = sqrt(
			1.0 - min(dot(landNoiseXY, landNoiseXY), 1.0));
		float3 landNoiseVector = float3(landNoiseXY, landNoiseZ);
		float3 landLodNormal;
		landLodNormal.x = dot(landLodTangent, landNoiseVector);
		landLodNormal.y = dot(landLodBitangent, landNoiseVector);
		landLodNormal.z = dot(landLodAxis, landNoiseVector);
		landAlbedo = lerp(
			landAlbedo, landLodAlbedo * landColorNoise, input.lodBlend.z);
		landNormal = normalize(
			lerp(landNormal, landLodNormal, input.lodBlend.z));
		landMaterial = lerp(
			landMaterial, LandLODParamsPS.yx, input.lodBlend.z);
#		endif
#	elif EYE && COMBINED
	float4 albedoSample = g_tEyeAlbedo.Sample(
		g_sAlbedo,
		float3(uv, g_bEyeInstances[input.instanceIndex].textureSlices.x));
#	elif DISMEMBERMENT_MEATCUFF
	float4 albedoSample = g_tAlbedo.Sample(g_sAlbedo, uv);
#	elif TEXTURE
	MESH_TV_SAMPLE(albedoSample, g_tAlbedo, g_sAlbedo, uv);
#	else
	float4 albedoSample = 1.0;
#	endif
#	if defined(LOD_BLENDING)
#		if LOD_LANDSCAPE && !BONE_TINTING
#			if defined(TERRAIN_VARIATION)
		// FO4: prepass albedo is raw, so the DiffuseToWorking result is unused.
		[branch] if (FO4SharedData::EnabledTerrainVariation != 0 && SharedData::terrainVariationSettings.enableLODTerrainTilingFix)
		{
			float4 sampledLodColor;
			StochasticSampleLOD(screenNoise, g_tAlbedo, g_sAlbedo, uv, sampledLodColor);
			albedoSample.xyz = sampledLodColor.rgb;
		}
#			endif
		// FO4: maps Skyrim's LODLANDSCAPE onto the prepass LOD terrain variant.
		if (FO4SharedData::EnabledLODBlending != 0)
			albedoSample.xyz = pow(abs(albedoSample.xyz), SharedData::lodBlendingSettings.LODTerrainGamma) * SharedData::lodBlendingSettings.LODTerrainBrightness;
#		elif !LANDSCAPE && !GRASS
		// FO4: object LOD has no technique define; the host bakes the BTO flag into PrepassFeatureLane, which stock never reads.
		if (PrepassFeatureLane.x != 0.0 && FO4SharedData::EnabledLODBlending != 0)
			albedoSample.xyz = pow(abs(albedoSample.xyz), SharedData::lodBlendingSettings.LODObjectGamma) * SharedData::lodBlendingSettings.LODObjectBrightness;
#		endif
#	endif
#	if LOD_LANDSCAPE && !BONE_TINTING
		albedoSample.xyz *= lodColorNoise;
#	endif
#	if GRADIENT_REMAP && (!HAIR || ADDITIONAL_ALPHA_MASK)
#		if EYE && COMBINED
		float gradientU = pow(albedoSample.y, 1.0 / 2.2);
		float4 gradientSample = g_tEyeGradient.SampleLevel(
			g_sGradient,
			float3(gradientU,
				g_bEyeInstances[input.instanceIndex].gradientV,
				g_bEyeInstances[input.instanceIndex].gradientSlice),
			0.0);
#		else
#			if VC
		float gradientVcPow = pow(input.vertexColor.x, 1.0 / 2.2);
#			endif
		float gradientU = pow(albedoSample.y, 1.0 / 2.2);
#			if VC
		float gradientV = TintColor_row0.x - (1.0 - gradientVcPow);
#			else
		float gradientV = TintColor_row0.x;
#			endif
		float4 gradientSample = g_tGradient.SampleLevel(
			g_sGradient, float2(gradientU, gradientV), 0.0);
#		endif
#	endif

#	if !LANDSCAPE
#		if DISMEMBERMENT_MEATCUFF
		float4 normalMapSample = g_tNormalMap.Sample(g_sNormalMap, uv);
#		elif EYE && COMBINED
		float4 normalMapSample = g_tEyeNormalMap.Sample(
			g_sNormalMap,
			float3(uv, g_bEyeInstances[input.instanceIndex].textureSlices.y));
#		elif TEXTURE
		MESH_TV_SAMPLE(normalMapSample, g_tNormalMap, g_sNormalMap, uv);
#		else
		float4 normalMapSample = 0.5;
#		endif
#		if MODELSPACENORMALS
#			if FACE
		float3 modelNormal = normalMapSample.xzy * 2.0 - 1.0;
		float4 faceDetail = g_tFaceDetail.Sample(g_sFaceDetail, uv) * 2.0 - 1.0;
		float3 blendedModelNormal = lerp(
			modelNormal, faceDetail.xzy, input.faceMask);
#			else
		float3 nmRaw = normalMapSample.xyz * 2.0 - 1.0;
#			endif
#		else
		float2 nts_xy = normalMapSample.xy * 2.0 - 1.0;
		float nts_xy_lenSq = saturate(dot(nts_xy, nts_xy));
		float nts_z = sqrt(1.0 - nts_xy_lenSq);
#			if FACE
		float4 faceDetail = g_tFaceDetail.Sample(g_sFaceDetail, uv);
		float2 faceNormalXY = faceDetail.xy * 2.0 - 1.0;
		float faceNormalZ = sqrt(
			1.0 - saturate(dot(faceNormalXY, faceNormalXY)));
		float3 faceNormal = normalize(lerp(
			float3(0.0, 0.0, 1.0),
			float3(faceNormalXY, faceNormalZ),
			input.faceMask));
		nts_xy += faceNormal.xy;
		float3 normalizedFaceNts = normalize(float3(nts_xy, nts_z));
#			endif
#		endif
#	endif

#	if LANDSCAPE
		float auxRoughness = landMaterial.y;
#	elif !(HAIR && !BLEND) && !(GRADIENT_REMAP && HAIR && BLEND && !ADDITIONAL_ALPHA_MASK)
#		if DISMEMBERMENT_MEATCUFF
	float auxRoughness = g_tMaterial.Sample(g_sMaterial, uv).y;
	float materialX = g_tMaterial.Sample(g_sMaterial, uv).x;
#		elif EYE && COMBINED
	float4 materialSample = g_tEyeMaterial.Sample(
		g_sMaterial,
		float3(uv, g_bEyeInstances[input.instanceIndex].textureSlices.z));
#		elif TEXTURE
	MESH_TV_SAMPLE(materialSample, g_tMaterial, g_sMaterial, uv);
#		else
	float4 materialSample = float4(0.0, 1.0, 0.0, 0.0);
#		endif
#		if DISMEMBERMENT_MEATCUFF
#			if GRADIENT_REMAP
	auxRoughness *= gradientSample.w;
#			endif
#		else
#			if GRADIENT_REMAP
	float auxRoughness = materialSample.y * gradientSample.w;
#			else
	float auxRoughness = materialSample.y;
#			endif
#		endif
#	endif
// The cut cap carries its own surface; the measured emission guards each channel separately.
#	if EYE && DISMEMBERMENT
		bool dismembered = input.dismemberWeight.y > 0.0;
		if (dismembered) {
			auxRoughness = g_tCutMaterial.Sample(g_sCutMaterial, input.cutCoord).y;
		}
		if (dismembered) {
			materialSample.x = g_tCutMaterial.Sample(g_sCutMaterial, input.cutCoord).x;
		}
#	endif

#	if VC
#		if LANDSCAPE
		landAlbedo *= input.vertexColor.xyz;
#		elif TREE_ANIM && ALPHA_TEST
		albedoSample.xyz *= input.vertexColor.xyz;
#		elif EYE && DISMEMBERMENT && BLEND
		albedoSample.a *= input.vertexColor.a;
#		elif EYE && DISMEMBERMENT_MEATCUFF && GRADIENT_REMAP
		albedoSample.a *= input.vertexColor.a;
#		elif HAIR && (BLEND || GRADIENT_REMAP)
		albedoSample.a *= input.vertexColor.a;
#		elif LOD_LANDSCAPE && !BONE_TINTING
		albedoSample.a *= input.vertexColor.a;
		albedoSample.xyz *= input.vertexColor.xyz;
#		else
		albedoSample *= input.vertexColor;
#		endif
#	endif
#	if MENU_SCREEN
		float3 menuOverlay = g_tMenuOverlay.Sample(g_sMenuOverlay, uv).xyz;
		albedoSample.xyz += menuOverlay;
#	endif
#	if SKIN_TINT
#		if ALPHA_TEST
		clip(albedoSample.a - EmissiveColor_row0.w);
#		endif
// The measured joint cell reaches this arm too, though it clears ADDITIONAL_ALPHA_MASK.
#		if ADDITIONAL_ALPHA_MASK || (EYE && (DISMEMBERMENT || DISMEMBERMENT_MEATCUFF))
		float3 skinBase = pow(albedoSample.xyz, 1.0 / 2.2);
		float3 skinColor = pow(TintColor.xyz, 1.0 / 2.2);
		bool3 useSkinLow = skinColor < 0.5;
		float3 doubledSkinBase = skinBase + skinBase;
		float3 skinLow = skinBase * skinBase;
		skinLow *= 1.0 - 2.0 * skinColor;
		skinLow = doubledSkinBase * skinColor + skinLow;
		float3 rootedSkinBase = sqrt(skinBase);
		float3 skinHigh = skinColor * 2.0 - 1.0;
		float3 inverseSkinColor = 1.0 - skinColor;
		inverseSkinColor = doubledSkinBase * inverseSkinColor;
		skinHigh = rootedSkinBase * skinHigh + inverseSkinColor;
		float3 skinResult = useSkinLow ? skinLow : skinHigh;
#		elif VC || ALPHA_TEST
		float3 skinBase = pow(albedoSample.xyz, 1.0 / 2.2);
		float3 rootedSkinBase = sqrt(skinBase);
		float3 skinColor = pow(TintColor.xyz, 1.0 / 2.2);
		float3 skinHigh = skinColor * 2.0 - 1.0;
		float3 skinLow = 1.0 - skinColor;
		float3 doubledSkinBase = skinBase + skinBase;
		skinBase *= skinBase;
		skinLow *= doubledSkinBase;
		skinHigh = rootedSkinBase * skinHigh + skinLow;
		skinLow = doubledSkinBase * skinColor + skinBase * (1.0 - 2.0 * skinColor);
		float3 skinResult = (skinColor < 0.5) ? skinLow : skinHigh;
#		else
		float3 skinBase = pow(albedoSample.xyz, 1.0 / 2.2);
		float3 skinColor = pow(TintColor.xyz, 1.0 / 2.2);
		float3 skinHigh = skinColor * 2.0 - 1.0;
		float3 skinLow = 1.0 - skinColor;
		float3 doubledSkinBase = skinBase + skinBase;
		skinLow *= doubledSkinBase;
		float3 rootedSkinBase = sqrt(skinBase);
		skinBase *= skinBase;
		skinHigh = rootedSkinBase * skinHigh + skinLow;
		skinLow = doubledSkinBase * skinColor + skinBase * (1.0 - 2.0 * skinColor);
		float3 skinResult = (skinColor < 0.5) ? skinLow : skinHigh;
#		endif
		skinResult = pow(skinResult, 2.2);
		albedoSample.xyz = lerp(
			albedoSample.xyz, skinResult, TintColor.w);
#	endif
#	if EYE && COMBINED
		float4 eyeParams =
			g_bEyeInstances[input.instanceIndex].alphaRefAndFlags;
#	endif
#	if ALPHA_TEST && !SKIN_TINT
#		if EYE && COMBINED
		clip(albedoSample.a - eyeParams.x);
#		else
		clip(albedoSample.a - EmissiveColor_row0.w);
#		endif
#	endif
#	if GRADIENT_REMAP && HAIR && !ADDITIONAL_ALPHA_MASK
#		if VC
		float gradientVcPow = pow(input.vertexColor.x, 1.0 / 2.2);
#		endif
#		if BLEND
		float gradientScale = albedoSample.y * 1.8;
#		endif
		float gradientU = pow(albedoSample.y, 1.0 / 2.2);
#		if VC
		float gradientV = TintColor_row0.x - (1.0 - gradientVcPow);
#		else
		float gradientV = TintColor_row0.x;
#		endif
		float4 gradientSample = g_tGradient.SampleLevel(
			g_sGradient, float2(gradientU, gradientV), 0.0);
#		if BLEND
		gradientSample.xyz *= gradientScale;
		float4 materialSample = g_tMaterial.Sample(g_sMaterial, uv);
		float auxRoughness = materialSample.y * gradientSample.w;
#		endif
#	endif
#	if GLOWMAP && (!HAIR || BLEND)
		float4 glowMapSample = g_tGlowMap.Sample(g_sGlowMap, uv);
#	endif

#	if !DISMEMBERMENT_MEATCUFF || GLOWMAP
		float3 tNorm = normalize(input.tangent);
		float3 bNorm = normalize(input.bitangent);
		float3 nGeom = normalize(input.normal);
#	endif
#	if LANDSCAPE
		float3 nts = float3(landNormal.xy,
			(input.isFrontFace != 0) ? landNormal.z : -landNormal.z);
#	elif LOD_LANDSCAPE && !BONE_TINTING
#		if MODELSPACENORMALS
	float3 lodNormalBase = normalMapSample.xzy * 2.0 - 1.0;
#		else
	float3 lodNormalBase = float3(nts_xy, nts_z);
#		endif
	float lodNoiseZ = sqrt(
		1.0 - min(dot(lodNormalNoiseXY, lodNormalNoiseXY), 1.0));
	float3 lodNoiseVector = float3(lodNormalNoiseXY, lodNoiseZ);
	float3 lodTangent = cross(
		lodNormalBase, cross(lodNormalBase, float3(1.0, 0.0, 0.0)));
	float3 lodBitangent = cross(lodNormalBase, lodTangent);
	float3 nts;
	nts.y = dot(normalize(lodBitangent), lodNoiseVector);
	nts.x = dot(normalize(lodTangent), lodNoiseVector);
	float lodSignedZ = dot(normalize(lodNormalBase), lodNoiseVector);
	nts.z = (input.isFrontFace != 0) ? lodSignedZ : -lodSignedZ;
#	elif MODELSPACENORMALS
#		if FACE
	float signedModelNormalZ = (input.isFrontFace != 0) ? blendedModelNormal.z : -blendedModelNormal.z;
	float3 nts = float3(blendedModelNormal.xy, signedModelNormalZ);
#		else
	float3 nts = float3(nmRaw.x, nmRaw.z,
		(input.isFrontFace != 0) ? nmRaw.y : -nmRaw.y);
#		endif
#	else
#		if FACE
	float signedFaceNtsZ = (input.isFrontFace != 0) ? normalizedFaceNts.z : -normalizedFaceNts.z;
	float3 nts = float3(normalizedFaceNts.xy, signedFaceNtsZ);
#		else
	float3 nts = float3(nts_xy, (input.isFrontFace != 0) ? nts_z : -nts_z);
#		endif
#	endif
#	if DISMEMBERMENT_MEATCUFF
		float axisX;
		float axisY;
		float axisZ;
		bool outsideCuff =
			input.dismember.x < 0.0 || input.dismember.x > 10.0;
#		if !defined(OG)
		// OG-era compilation rejects attributed branches containing cuff samples.
		[branch]
#		endif
			if (outsideCuff)
		{
			axisX = dot(tNorm, nts);
			axisY = dot(bNorm, nts);
			axisZ = dot(nGeom, nts);
			clip(-1.0);
		}
		else
		{
			float cuffProjection =
				dot(cb2_cut_tangent.xyz, input.cuffRadial) * 0.5 + 0.5;
			bool cuffPositiveSide =
				dot(cb2_cut_bitangent.xyz, input.cuffRadial) > 0.0;
			float4 cuffCoordinates;
			cuffCoordinates.x = cuffProjection * 0.5;
			cuffCoordinates.z = input.dismember.x * 0.1;
			cuffCoordinates.w = cuffProjection * -0.5 + 1.0;
			float2 cuffNormalUV = cuffPositiveSide ? cuffCoordinates.xz : cuffCoordinates.wz;
			float2 cuffSurfaceUV = 1.0 - cuffNormalUV;
#		if GRADIENT_REMAP
			gradientSample =
				g_tCutAlbedo.Sample(g_sCutAlbedo, cuffSurfaceUV);
			albedoSample = gradientSample;
#		else
			albedoSample = g_tCutAlbedo.Sample(g_sCutAlbedo, cuffSurfaceUV);
#		endif
			auxRoughness =
				g_tCutMaterial.Sample(g_sCutMaterial, cuffSurfaceUV).y;
			materialX =
				g_tCutMaterial.Sample(g_sCutMaterial, cuffSurfaceUV).x;
			float2 cutNtsXY = g_tCutNormalMap.Sample(
												 g_sCutNormalMap, cuffNormalUV)
		                              .xy *
		                          2.0 -
		                      1.0;
			float cutNtsZ = sqrt(1.0 - dot(cutNtsXY, cutNtsXY));
			float3 cutNts = float3(cutNtsXY, cutNtsZ);
#		if MODELSPACENORMALS
			float3 cutModelNormal = cutNts.y * input.cuffNormal;
			cutModelNormal =
				cutNts.x * input.cuffAxis + cutModelNormal;
			cutModelNormal =
				cutNts.z * input.cuffRadial + cutModelNormal;
			cutNts = normalize(cutModelNormal);
#		endif
			axisX = dot(tNorm, cutNts);
			axisY = dot(bNorm, cutNts);
			axisZ = dot(nGeom, cutNts);
		}
#	else
	float axisX = dot(tNorm, nts);
	float axisY = dot(bNorm, nts);
	float axisZ = dot(nGeom, nts);
#	endif
// The cut normal is oriented by a constant cap frame, not the interpolated one.
#	if EYE && DISMEMBERMENT
		if (dismembered) {
			float2 cutNtsXY = g_tCutNormalMap.Sample(
												 g_sCutNormalMap, input.cutCoord)
		                              .xy *
		                          2.0 -
		                      1.0;
			float3 cutNts = float3(cutNtsXY, sqrt(1.0 - dot(cutNtsXY, cutNtsXY)));
			axisX = dot(cb2_cut_tangent.xyz, cutNts);
			axisY = dot(WetnessControl_Spec2.xyz, cutNts);
			axisZ = dot(WetnessControl_Spec3_row0.xyz, cutNts);
		}
#	endif
#	if LANDSCAPE
		float4 landFlags = max(WetnessControl_Spec, 0.0) * input.layerWeights.x + max(WetnessControl_Spec1, 0.0) * input.layerWeights.y;
		landFlags += max(WetnessControl_Spec2, 0.0) * input.layerWeights.z;
		landFlags += max(WetnessControl_Spec3, 0.0) * input.layerWeights.w;
		float fadeScale = WorldWetnessAndRainTransition.x * landFlags.w;
#	elif !(EYE && COMBINED)
	float fadeScale = WetnessControl_Spec.w * WorldWetnessAndRainTransition.x;
#	endif

#	if !HAIR || BLEND
#		if INSTANCED && !LANDSCAPE
		InstanceRecord instanceRecord = cb13_instances[input.instanceIndex];
#		elif !(EYE && COMBINED)
		float2 anchor = SpecularParam.xy;
		float2 target = SpecularParam2_row0.xy;
		bool2 svScrolls = (target >= 0.0);
		float2 delta = target - anchor;
		float2 svRaw = WorldWetnessAndRainTransition.xx * delta + anchor;
		float2 sval = svScrolls ? (svRaw * anchor) : anchor;
#		endif

#		if LANDSCAPE
		float landZ = saturate(landFlags.z);
		float matZxFade = landZ * WorldWetnessAndRainTransition.x;
		float2 landAux = float2(landFlags.y, landFlags.x);
		bool2 landAuxPinned = (landAux == -1.0);
		float2 landAuxGate = landAuxPinned ? 0.0 : WorldWetnessAndRainTransition.xx;
		float matZxComp = mad(
			-WorldWetnessAndRainTransition.x,
			landZ,
			1.0);
		float lerpedMatX = landMaterial.x * matZxComp + matZxFade;
		float2 landSval = lerp(sval, landAux, landAuxGate);
#		elif EYE && COMBINED
		float eyeZ = saturate(eyeParams.w);
		float matZxFade = eyeZ * WorldWetnessAndRainTransition.x;
		float2 eyeAux = float2(eyeParams.z, eyeParams.y);
		bool2 eyeAuxPinned = (eyeAux == -1.0);
		float2 eyeAuxGate = eyeAuxPinned ? 0.0 : WorldWetnessAndRainTransition.xx;
		float matZxComp = mad(
			-WorldWetnessAndRainTransition.x,
			eyeZ,
			1.0);
#			if DISMEMBERMENT_MEATCUFF
		float lerpedMatX = materialX * matZxComp + matZxFade;
#			else
		float lerpedMatX = materialSample.x * matZxComp + matZxFade;
#			endif
		float4 eyeScroll = g_bEyeInstances[input.instanceIndex].scroll;
		float2 eyeSval = lerp(eyeScroll.xy, eyeAux, eyeAuxGate);
#		else
		float matZxFade = WorldWetnessAndRainTransition.x * WetnessControl_Spec.z;
		float matZxComp = mad(
			-WetnessControl_Spec.z,
			WorldWetnessAndRainTransition.x,
			1.0);
#			if DISMEMBERMENT_MEATCUFF
		float lerpedMatX = materialX * matZxComp + matZxFade;
#			else
		float lerpedMatX = materialSample.x * matZxComp + matZxFade;
#			endif
#		endif

#		if LANDSCAPE
		output.auxA.x = auxRoughness * landSval.x;
		output.auxA.y = lerpedMatX * landSval.y;
#		elif EYE && COMBINED
		output.auxA.x = auxRoughness * eyeSval.x;
		output.auxA.y = lerpedMatX * eyeSval.y * g_bEyeInstances[input.instanceIndex].scroll.y;
#		elif INSTANCED
		output.auxA.x = auxRoughness * instanceRecord.rows[7].x;
		output.auxA.y = lerpedMatX * instanceRecord.rows[7].y;
#		else
		output.auxA.x = auxRoughness * sval.x;
		output.auxA.y = lerpedMatX * sval.y;
#		endif
#	endif

		float3 nWorldFromTBN = normalize(float3(axisX, axisY, min(axisZ, 0.0)));
#	if BLEND
		output.normalOct.z = -nWorldFromTBN.z;
#	endif
		float octZ = sqrt(nWorldFromTBN.z * -8.0 + 8.0);
		output.normalOct.xy = nWorldFromTBN.xy / octZ.xx + 0.5;
#	if !HAIR || BLEND
#		if INSTANCED && !LANDSCAPE
		output.auxA.z = 0.01 * instanceRecord.rows[7].w;
#		elif EYE && COMBINED
		output.auxA.z = g_bEyeInstances[input.instanceIndex].scroll.w * 0.01;
#		else
		output.auxA.z = SpecularParam.w * 0.01;
#		endif
#	endif

#	if EYE && COMBINED
		float eyeMaterialIndex = g_bEyeMaterials[input.instanceIndex].materialIndex;
		output.material.z = sqrt(
			g_bEyeInstances[input.instanceIndex].smoothness * 0.02);
#	else
	float matGate = (cb2_material_id_and_smoothness.w < 0.0) ? 0.0 : WorldWetnessAndRainTransition.x;
	bool matUseSpan = cb2_material_id_and_smoothness.y;
	float matSpan = cb2_material_id_and_smoothness.w - cb2_material_id_and_smoothness.z;
	float matVal = matUseSpan ? (matGate * matSpan + cb2_material_id_and_smoothness.z) : (matGate * cb2_material_id_and_smoothness.w);
	output.material.z = sqrt(matVal * 0.02);
#	endif

#	if LANDSCAPE
		output.material.x = (WorldWetnessAndRainTransition.x != 0.0) ? WetnessControl_Reflect.x : 0.0;
#	else
	bool bitB = WetnessControl_Spec.x;
	bool bitA = WetnessControl_Spec.y && WorldWetnessAndRainTransition.x;
	output.material.x = (bitB || bitA) ? 1.0 : 0.0;
#	endif

#	if EYE && COMBINED
		output.material.y = eyeMaterialIndex * (1.0 / 255.0);
		output.material.w = saturate(eyeMaterialIndex);
#	else
	output.material.y = cb2_material_id_and_smoothness.x * (1.0 / 255.0);
	output.material.w = saturate(cb2_material_id_and_smoothness.x);
#	endif

#	if HAIR && !BLEND
		float3 hairMaterialVector = g_tHairMaterial.Sample(
													   g_sHairMaterial, uv)
	                                        .xyz *
	                                    2.0 -
	                                1.0;
		float3 hairVector = normalize(hairMaterialVector);
		output.auxA.x = dot(tNorm, hairVector);
		output.auxA.y = dot(bNorm, hairVector);
		output.auxA.z = dot(nGeom, hairVector);
#	endif

#	if PIPBOY_SCREEN && ADDITIONAL_ALPHA_MASK
		bool bypassFade = (WetnessControl_Spec.w == -1.0);
		float alphaFade = bypassFade ? 1.0 : (1.0 - fadeScale);
#	endif

#	if PIPBOY_SCREEN
		float3 pipboyView = normalize(-input.pipboyView);
		float3 pipboyNormal = float3(axisX, axisY, axisZ);
		float2 pipboyOffset = -pipboyView.xy / dot(pipboyView, pipboyNormal);
		pipboyOffset = (pipboyOffset + pipboyNormal.xy * 2.0) * PipboyParams.x;
		float2 pipboyUV = uv + float2(pipboyOffset.x, -pipboyOffset.y) * PipboyParams.y;
		float3 pipboySample = pow(g_tPipboyScreen.Sample(
													 g_sPipboyScreen, pipboyUV)
									  .xyz,
			2.2);
#		if ADDITIONAL_ALPHA_MASK
		output.albedo.xyz = albedoSample.xyz * alphaFade + pipboySample * PipboyParams.z;
#		endif
#	endif

#	if GLOWMAP && (!HAIR || BLEND)
#		if PIPBOY_SCREEN
		output.specTint.xyz = EmissiveColor_row0.xyz * glowMapSample.xyz;
#		else
		output.specTint.xyz = glowMapSample.xyz * EmissiveColor_row0.xyz;
#		endif
#	elif INSTANCED && !LANDSCAPE
	output.specTint.xyz = instanceRecord.rows[8].xyz;
#	elif EYE && COMBINED
	output.specTint.xyz = g_bEyeInstances[input.instanceIndex].specularTint;
#	else
	output.specTint.xyz = EmissiveColor_row0.xyz;
#	endif
#	if PIPBOY_SCREEN
		output.specTint.xyz += pipboySample * PipboyParams.w;
#	endif

#	if BLEND
		float blendAlpha = (AlphaScale_row0.y == 1.0) ? albedoSample.a : 1.0;
		clip(AlphaScale_row0.x * blendAlpha - (4.0 / 255.0));
		blendAlpha *= AlphaScale_row0.x;
		output.albedo.w = blendAlpha;
		output.normalOct.w = blendAlpha;
#		if SKIN_TINT
		output.auxA.w = 0.019608;
#		elif HAIR
#			if BLEND && GRADIENT_REMAP && ADDITIONAL_ALPHA_MASK
		output.auxA.w = blendAlpha;
#			else
		output.auxA.w = min(max(blendAlpha, 5.0 / 255.0), 1.0);
#			endif
#		elif SKEW_SPECULAR_ALPHA
		output.auxA.w = pow(blendAlpha, 0.1);
#		else
		output.auxA.w = blendAlpha;
#		endif
#		if HAIR && defined(FO4_PREPASS_VERTEX_AO)
		// MRT4 alpha is also its blend factor, so hair writes 0 and keeps the value beneath instead of upstream's coverage-weighted 1 - vertexAO.
		output.specTint.w = 0.0;
#		else
		output.specTint.w = blendAlpha;
#		endif
#	else
#		ifdef FO4_PREPASS_VERTEX_AO
	// 1 - vertexAO as upstream's Masks2; SSGI composite and Skylighting read it.
#			if GRASS && VC
	output.specTint.w = 1.0 - grassVertexAO;
#			elif VC && !LANDSCAPE && !HAIR && !EYE && !SKIN_TINT
	// Albedo is already linear here, so unlike upstream the max needs no ColorToLinear.
	output.specTint.w = 1.0 - max(max(input.vertexColor.x, input.vertexColor.y), input.vertexColor.z);
#			else
	output.specTint.w = 0.0;
#			endif
#		endif
#		if HAIR
#			if GRADIENT_REMAP
	output.albedo.w = albedoSample.y;
#			else
	output.albedo.w = 0.0;
#			endif
#		elif INSTANCED && !LANDSCAPE
	output.albedo.w = instanceRecord.rows[7].z;
#		elif EYE && COMBINED
	output.albedo.w = eyeScroll.z;
#		else
	output.albedo.w = SpecularParam.z;
#		endif
#	endif

#	if FACE && ADDITIONAL_ALPHA_MASK
		float faceInfluence = (1.0 - faceDetail.w) * input.faceMask;
		float faceAlbedoScale = faceInfluence * -0.3 + 1.0;
		float3 faceAlbedo = albedoSample.xyz * faceAlbedoScale;
#	endif

#	if LANDSCAPE
		bool landFadeApplies = (landFlags.w != -1.0);
		float alphaFade = saturate(1.0 - fadeScale);
#	elif EYE && COMBINED
	float eyeFadeFlag = g_bEyeInstances[input.instanceIndex].fadeFlag;
	bool eyeFadeApplies = (eyeFadeFlag != -1.0);
	float alphaFade = saturate(
		1.0 - WorldWetnessAndRainTransition.x * eyeFadeFlag);
#	elif !PIPBOY_SCREEN || !ADDITIONAL_ALPHA_MASK
	bool bypassFade = (WetnessControl_Spec.w == -1.0);
	float alphaFade = bypassFade ? 1.0 : (1.0 - fadeScale);
#	endif
// The cut cap swaps the composed colour, so its single fade is applied after the swap.
#	if EYE && DISMEMBERMENT
		alphaFade = 1.0;
#	endif

#	if BONE_TINTING
		float4 boneIndex = g_tBoneIndex.Sample(g_sBoneIndex, uv);
		float2 boneUv = float2(boneIndex.y, frac(cb2_bone_tint_params.x));
		float4 boneTint = g_tBoneTint.Sample(g_sBoneTint, boneUv);
		float3 boneContribution = boneTint.xyz * boneTint.w * boneIndex.w * input.boneTint.w * 4.0;
#	endif
#	if FACE && !ADDITIONAL_ALPHA_MASK
		float faceInfluence = (1.0 - faceDetail.w) * input.faceMask;
		float faceAlbedoScale = faceInfluence * -0.3 + 1.0;
#	endif
#	if EYE && COMBINED
#		if GRADIENT_REMAP
		float3 eyeAlbedo = gradientSample.xyz;
#		else
		float3 eyeAlbedo = albedoSample.xyz;
#		endif
		[branch] if (eyeFadeApplies)
		{
			output.albedo.xyz = eyeAlbedo * alphaFade;
		}
		else
		{
			output.albedo.xyz = eyeAlbedo;
		}
#	elif GRADIENT_REMAP && HAIR && BLEND && ADDITIONAL_ALPHA_MASK
	float4 finalAlbedo = 0.0;
	finalAlbedo.x = albedoSample.y * 1.8;
	finalAlbedo.xzw = gradientSample.xyzw * finalAlbedo.xxxx;
	float hairAlpha = max(blendAlpha, 5.0 / 255.0);
	output.auxA.w = min(hairAlpha, 1.0);
	bool lateBypassFade = (WetnessControl_Spec.w == -1.0);
	float lateAlphaFade = lateBypassFade ? 1.0 : (1.0 - fadeScale);
	output.albedo.xyz = finalAlbedo.xzw * lateAlphaFade;
#	elif GRADIENT_REMAP
#		if BONE_TINTING
	output.albedo.xyz = gradientSample.xyz * alphaFade + boneContribution;
#		else
	output.albedo.xyz = gradientSample.xyz * alphaFade;
#		endif
#	else
#		if HAIR && BLEND
	output.albedo.xyz = 0.0;
#		else
#			if LANDSCAPE
	output.albedo.xyz = landFadeApplies ? (landAlbedo * alphaFade) : landAlbedo;
#			elif BONE_TINTING && FACE && ADDITIONAL_ALPHA_MASK
	output.albedo.xyz = faceAlbedo * alphaFade + boneContribution;
#			elif BONE_TINTING && FACE
	output.albedo.xyz = albedoSample.xyz * faceAlbedoScale * alphaFade + boneContribution;
#			elif BONE_TINTING
	output.albedo.xyz = albedoSample.xyz * alphaFade + boneContribution;
#			elif FACE && ADDITIONAL_ALPHA_MASK
	output.albedo.xyz = faceAlbedo * alphaFade;
#			elif FACE
	output.albedo.xyz = albedoSample.xyz * faceAlbedoScale * alphaFade;
#			elif PIPBOY_SCREEN && !ADDITIONAL_ALPHA_MASK
	output.albedo.xyz = albedoSample.xyz * alphaFade + pipboySample * PipboyParams.z;
#			elif !PIPBOY_SCREEN
	output.albedo.xyz = albedoSample.xyz * alphaFade;
#			endif
#		endif
#	endif
// The cut cap is its own surface: it replaces the composed colour, then takes the fade.
#	if EYE && DISMEMBERMENT
		if (dismembered) {
			output.albedo.xyz = g_tCutAlbedo.Sample(g_sCutAlbedo, input.cutCoord).xyz;
		}
#		if VC && BLEND
		else {
			output.albedo.xyz = albedoSample.xyz * input.vertexColor.xyz;
		}
#		endif
		output.albedo.xyz *= (WetnessControl_Spec.w == -1.0) ? 1.0 : (1.0 - fadeScale);
#	endif

#	if !BLEND
#		if PIPBOY_SCREEN
		output.auxA.w = 0.0156864;
#		elif SKIN_TINT
		output.auxA.w = 0.019608;
#		elif HAIR
		output.auxA.w = 0.0039216;
#		elif FACE
		output.auxA.w = 0.019608;
#		else
		output.auxA.w = 1.0;
#		endif
#	endif

#	if PREPASS_MOTION_VECTOR
#		if TESSELLATE_DISP_HEIGHT
		float4 worldPosition = float4(input.curr_pos_u.xyz, 1.0);
		float currClipX = dot(UnjitteredWorldToClip_row0, worldPosition);
		float currClipY = dot(UnjitteredWorldToClip_row1, worldPosition);
		float currClipW = dot(UnjitteredWorldToClip_row3, worldPosition);
		float2 currNDC = float2(currClipX, currClipY) / currClipW.xx;

		float prevClipX = dot(PreviousWorldToClip_row0, worldPosition);
		float prevClipY = dot(PreviousWorldToClip_row1, worldPosition);
		float prevClipW = dot(PreviousWorldToClip_row3, worldPosition);
		float2 prevNDC = float2(prevClipX, prevClipY) / prevClipW.xx;
#		else
		float4 currWorld = float4(input.curr_pos_u.xyz, 1.0);
		float currClipX = dot(UnjitteredWorldToClip_row0, currWorld);
		float currClipY = dot(UnjitteredWorldToClip_row1, currWorld);
		float currClipW = dot(UnjitteredWorldToClip_row3, currWorld);
		float2 currNDC = float2(currClipX, currClipY) / currClipW.xx;

		float4 prevWorld = float4(input.prev_pos_v.xyz, 1.0);
		float prevClipX = dot(PreviousWorldToClip_row0, prevWorld);
		float prevClipY = dot(PreviousWorldToClip_row1, prevWorld);
		float prevClipW = dot(PreviousWorldToClip_row3, prevWorld);
		float2 prevNDC = float2(prevClipX, prevClipY) / prevClipW.xx;
#		endif

#		if defined(FO4_TERRAIN_BLENDING)
		output.motionVec.xy = (currNDC - prevNDC) * float2(-0.5, 0.5);
#		else
		output.motionVec = (currNDC - prevNDC) * float2(-0.5, 0.5);
#		endif
#	endif
#	if defined(EXTENDED_MATERIALS)
	// Every draw writes, so surfaces drawn over parallax never keep its shadow.
#		if BLEND
		output.parallaxShadow = float4(parallaxShadow, 0, 0, output.albedo.w);
#		else
		output.parallaxShadow = float4(parallaxShadow, 0, 0, 1);
#		endif
#	endif
#	if defined(SUBSURFACE_SCATTERING)
	// Every draw writes, so surfaces drawn over skin never keep its profile.
#		if LANDSCAPE
		const float sssProfile = 0.0;
#		else
		const float sssProfile = PrepassFeatureLane.w != 0.0 ? 1.0 : 0.0;
#		endif
#		if BLEND
		output.sssProfile = float4(sssProfile, 0, 0, output.albedo.w);
#		else
		output.sssProfile = float4(sssProfile, 0, 0, 1);
#		endif
#	endif
#	if defined(FO4_TERRAIN_BLENDING)
		// The colour draw reads every blended target's alpha as the lerp factor.
		[branch] if (TerrainBlendingPass == 1)
		{
			output.albedo.w = terrainBlendFactor;
			output.normalOct.w = terrainBlendFactor;
			output.auxA.w = terrainBlendFactor;
			output.specTint.w = terrainBlendFactor;
#		if PREPASS_MOTION_VECTOR
			output.motionVec.w = terrainBlendFactor;
#		endif
#		if defined(EXTENDED_MATERIALS)
			output.parallaxShadow.w = terrainBlendFactor;
#		endif
#		if defined(SUBSURFACE_SCATTERING)
			output.sssProfile.w = terrainBlendFactor;
#		endif
		}
#	endif
		return output;
	}
#elif defined(BSDFPREPASS_VS_SOURCE)
// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// Exact AE 1.11.240 PrePass ordinary, tessellated, instanced, combined and terrain vertex cells.

cbuffer PerFrame_CB12 : register(b12)
{
	float4 WorldToView_row0;
	float4 WorldToView_row1;
	float4 WorldToView_row2;
	float4 WorldToView_row3;
	row_major float4x4 ViewToClip;
	float4 WorldToClip_row0;
	float4 WorldToClip_row1;
	float4 WorldToClip_row2;
	float4 WorldToClip_row3;
	row_major float4x4 ViewToWorld;
	row_major float4x4 ClipToWorld;
	row_major float4x4 ClipToView;
	row_major float4x4 FirstPersonClipToView;
	float4 HairSpecularScaleAndPower;
	float4 HairSpecularShift;
	float4 WorldWetnessAndRainTransition;
	row_major float4x4 PreviousWorldToClip;
	float4 PosAdjust;
	float4 PreviousPosAdjust;
};

#	if LOD_LANDSCAPE || CLIP_VOLUME
cbuffer PerGeometry_CB0 : register(b0)
{
#		if LOD_LANDSCAPE
	// xy is the blend centre, zw the half extent that admits the drop.
	float4 HighDetailRange;
#		endif
#		if CLIP_VOLUME
	// The volume keeps its own rows, so a landscape call still owns the bounds row above it.
	float4 ClipVolume_row0;  // Clip-volume center.
	float4 ClipVolume_row1;  // Clip-volume extent.
#		endif
};
#	endif

cbuffer PerMaterial_CB1 : register(b1)
{
	float4 TexcoordOffset;  // Texcoord scale and bias.
};

#	if BONE_TINTING && SKINNED
// A skinned tint call rebinds the unused world rows as the palette's per-influence bit masks.
cbuffer PerCall_CB2 : register(b2)
{
	uint4 BoneTintIndices[4];
};
#	elif SKINNED && (GRASS || TREE_ANIM || SPLINE)
// A skinned wind call takes its transform from the palette, so its own rows start the buffer.
cbuffer PerCall_CB2 : register(b2)
{
#		if GRASS
	// xyz scales the blade against its instance lane.
	float4 ScaleMask_row0;
	// z starts the distance fade and w spans it.
	float4 ScaleMask_row1;
	float4 ScaleMask_row2;
#		endif
	// x aims the gust and z and w phase the previous and current wind.
#		if GRASS
	float4 ScaleMask_row3;
#			define WindVector ScaleMask_row3
#		else
	float4 WindVector;
#		endif
	// xy bound the gust and z is the wind clock.
#		if GRASS
	float4 ScaleMask_row4;
#			define WindVectorEx ScaleMask_row4
#		else
	float4 WindVectorEx;
#		endif
#		if GRASS
	// xy centre a collider, z weights its push and w is its radius.
	float4 ScaleMask_row5[4];
#			define ActorCollision ScaleMask_row5
#		endif
#		if TREE_ANIM || SPLINE
	// x is the span the sway measures against, z and w scale it.
	float4 TreeParams;
#		endif
};
#	else
cbuffer PerCall_CB2 : register(b2)
{
#		if SKINNED
	float4 DismemberHitLoc;
	float4 DismemberHitDir_row0;
	float4 DismemberHitDir_row1;
	float4 DismemberHitDir_row2;
	float4 DismemberHitDir_row3;
	float4 DismemberHitDir_row4;
	float4 cb2_idx6_previous_world_row2;
#		elif INSTANCED
	float4 World_row0;
	float4 World_row1;
	float4 World_row2;
	float4 World_row3;
	float4 World_row4;
	float4 World_row5;
	float4 World_row6;
#		else
	float4 World_row0;
	float4 World_row1;
	float4 World_row2;
	float4 World_row3;
	float4 PreviousWorld_row0;
	float4 PreviousWorld_row1;
	float4 PreviousWorld_row2;
#		endif
#		if LANDSCAPE || PIPBOY_SCREEN || MERGE_INSTANCED || TREE_ANIM || SPLINE || GRASS
#			if INSTANCED
	float4 World_row7;
#			else
	float4 PreviousWorld_row3;
#			endif
#		endif
#		if GRASS
	// xyz scales the blade against its instance lane.
	float4 ScaleMask_row0;
	// z starts the distance fade and w spans it.
#			if VC
	float4 AlphaParam_row0;
	float4 AlphaParam_row1;
#			else
	float4 ScaleMask_row1;
	float4 ScaleMask_row2;
#			endif
#		endif
#		if GRASS || TREE_ANIM || SPLINE
	// x aims the sway, y weights it, and z and w phase the previous and current wind.
#			if GRASS && !VC
	float4 ScaleMask_row3;
#				define WindVector ScaleMask_row3
#			else
	float4 WindVector;
#			endif
	// xy bound the sway amplitude and z is the wind clock.
#			if GRASS && !VC
	float4 ScaleMask_row4;
#				define WindVectorEx ScaleMask_row4
#			else
	float4 WindVectorEx;
#			endif
#		endif
#		if GRASS
	// xy centre a collider, z weights its push and w is its radius.
#			if VC
	float4 ActorCollision[4];
#			else
	float4 ScaleMask_row5[4];
#				define ActorCollision ScaleMask_row5
#			endif
#		endif
#		if TREE_ANIM || SPLINE
	// x is the span the sway measures against, z and w scale it.
	float4 TreeParams;
#		endif
#		if LANDSCAPE
	// A terrain call fades against the same rows it transforms with, so xy sheets and zw centres.
	float4 LandLODParamsVS;
#		endif
#		if PIPBOY_SCREEN
	float4 WorldView_row0;  // Screen projection.
	float4 WorldView_row1;
	float4 WorldView_row2;
#		endif
#		if MERGE_INSTANCED
	// z offsets the packed texcoord inside one merged vertex.
	uint4 VertexAttribOffsets1;
	// x strides a merged vertex, y counts an object's triangles and z is the first merged vertex.
	uint4 VertexAttribOffsets2;
#		endif
};

// A cut call rebinds the first two world rows as the cut plane's point and axis.
#		define CUT_POINT DismemberHitLoc
#		define CUT_AXIS DismemberHitDir_row0
#	endif

#	if SKINNED
cbuffer PerSkin_CB10 : register(b10)
{
	float4 cb10_bone_transform[180];
};

cbuffer PerSkin_CB9 : register(b9)
{
	float4 cb9_previous_bone_transform[180];
};
#	endif

// A terrain patch arrives either as a structured quad the instance indexes or as plain attributes.
#	define STRUCTURED_TERRAIN (LANDSCAPE && INSTANCED)
#	define ATTRIBUTE_TERRAIN (LANDSCAPE && !INSTANCED)

// A terrain call spends its LOD axis on the patch fade, so only a non-terrain call drops height.
#	define LOD_HEIGHT_DROP (LOD_LANDSCAPE && !LANDSCAPE)

#	if defined(OG) && BLEND
// OG blends leave both frame lanes empty because they write no motion vector.
#		define MOTION_LANES 0
#	else
#		define MOTION_LANES 1
#	endif

#	if STRUCTURED_TERRAIN
cbuffer PerInstance_CB13 : register(b13)
{
	float4 cb13_terrain_quad[100];
};

struct TerrainQuad
{
	float pad0;
	float3 origin;
	float2 texcoord_offset;
	int variant;
	float4 pad1;
	float4 pad2;
	float4 pad3;
};

struct TerrainVertex
{
	float height;
	float3 color;
	float3 normal;
	float3 tangent;
	float3 binormal;
	float3 blend;
	float alpha;
};

StructuredBuffer<TerrainQuad> g_TerrainQuads : register(t4);
StructuredBuffer<TerrainVertex> g_TerrainVertices : register(t5);
#	elif INSTANCED
// Nine rows per instance: the world transform, five reserved rows and the texture scale-bias.
struct InstanceTransform
{
	float4 row0;
	float4 row1;
	float4 row2;
	float4 pad3;
	float4 pad4;
	float4 pad5;
	float4 texcoord_scale_bias;
	float4 pad7;
	float4 pad8;
};

cbuffer PerInstance_CB13 : register(b13)
{
	InstanceTransform cb13_instance[455];
};
#	endif

#	if COMBINED
struct CombinedTransform
{
	float4 row0;
	float4 row1;
	float4 row2;
	float4 previous_row0;
	float4 previous_row1;
	float4 previous_row2;
	float pad;
};

StructuredBuffer<CombinedTransform> g_CombinedTransforms : register(t6);
#	endif

#	if LOD_OBJECT_INSTANCED
// A LOD object instance places a model frame: xyz translates, w scales and the quaternion turns.
struct LodObjectTransform
{
	float4 translation_and_scale;
	float4 rotation;
};

StructuredBuffer<LodObjectTransform> g_LodObjectTransforms : register(t6);
StructuredBuffer<uint> g_LodObjectRemap : register(t7);
#	endif

#	if MERGE_INSTANCED
// A merged draw fetches its own vertices: t4 packs them, t6 and t7 index them, t8 places them.
ByteAddressBuffer g_MergedVertices : register(t4);
ByteAddressBuffer g_MergedIndices : register(t6);
ByteAddressBuffer g_MergedObjects : register(t7);

struct MergedTransform
{
	float4 row0;
	float4 row1;
	float4 row2;
	float4 row3;
	// x tints the merged vertex colour before it is linearised.
	float4 tint;
};

StructuredBuffer<MergedTransform> g_MergedTransforms : register(t8);

// A merged index list packs two entries per dword, so an odd entry takes the high half.
uint LoadMergedIndex(ByteAddressBuffer buffer, uint entry)
{
	uint odd = entry & 1;
	uint packed = buffer.Load(2 * entry - 2 * odd);
	return (packed & 0xFFFF) * (1 - odd) + (packed >> 16) * odd;
}

// The merged stream is half precision, so every dword it holds carries two lanes.
float2 UnpackHalf2(uint packed)
{
	return float2(f16tof32(packed), f16tof32(packed >> 16));
}

// A merged byte lane is unsigned; a direction lane doubles it back into a signed one.
float UnpackUnormByte(uint packed, uint shift)
{
	return ((packed >> shift) & 0xFF) / 255.0;
}

float4 UnpackUnorm4(uint packed)
{
	return float4(UnpackUnormByte(packed, 0), UnpackUnormByte(packed, 8),
		UnpackUnormByte(packed, 16), UnpackUnormByte(packed, 24));
}

float UnpackSignedByte(uint packed, uint shift)
{
	return UnpackUnormByte(packed, shift) * 2.0 - 1.0;
}

float3 UnpackSigned3(uint packed)
{
	return float3(UnpackSignedByte(packed, 0), UnpackSignedByte(packed, 8),
		UnpackSignedByte(packed, 16));
}

float4 LoadMergedPosition(uint address)
{
	uint2 packed = g_MergedVertices.Load2(address);
	return float4(UnpackHalf2(packed.x), UnpackHalf2(packed.y));
}
#	endif

// A merged draw feeds itself, so the pipeline hands it nothing but the vertex id.
#	define ATTRIBUTE_VERTEX (!MERGE_INSTANCED)

// A wind regime bends the model frame itself, so it brings the attributes that frame needs.
#	define WIND_FRAME (GRASS || SPLINE || TREE_ANIM)

struct VertexInput
{
#	if ATTRIBUTE_VERTEX
	float4 position: POSITION0;
#		if TEXTURE
	float2 texcoord: TEXCOORD0;
#		endif
#		if NORMALS || WIND_FRAME
	float4 normal: NORMAL0;
#		endif
#		if BINORMAL_TANGENT || WIND_FRAME
	float4 binormal: BINORMAL0;
#		endif
#		if VC || TREE_ANIM || SPLINE
	float4 color: COLOR0;
#		endif
#		if SKINNED
	float4 blend_weight: BLENDWEIGHT0;
	float4 blend_indices: BLENDINDICES0;
#		endif
#		if LANDSCAPE
	float4 terrain: TEXCOORD3;
#		endif
#		if EYE
	float eye_index: TEXCOORD2;
#		endif
#		if GRASS
	float4 grass_origin: TEXCOORD4;
	float4 grass_row0: TEXCOORD5;
	float4 grass_row1: TEXCOORD6;
	// x closes the instance basis; y drives the per-blade scale.
	float4 grass_row2: TEXCOORD7;
#		endif
#		if INSTANCED || LOD_OBJECT_INSTANCED
	uint instance_id: SV_InstanceID;
#		endif
#	endif
#	if STRUCTURED_TERRAIN || MERGE_INSTANCED
	uint vertex_id: SV_VertexID;
#	endif
};

// An ablated attribute leaves the signature, so its consumers read a neutral constant instead.
#	if TEXTURE && ATTRIBUTE_VERTEX
#		define INPUT_TEXCOORD input.texcoord
#	else
#		define INPUT_TEXCOORD float2(0.0, 0.0)
#	endif

#	if (NORMALS || WIND_FRAME) && ATTRIBUTE_VERTEX
#		define INPUT_NORMAL input.normal
#	else
#		define INPUT_NORMAL float4(0.0, 0.0, 0.0, 0.0)
#	endif

#	if (BINORMAL_TANGENT || WIND_FRAME) && ATTRIBUTE_VERTEX
#		define INPUT_BINORMAL input.binormal
#	else
#		define INPUT_BINORMAL float4(0.0, 0.0, 0.0, 0.0)
#	endif

#	if EYE
#		define INPUT_EYE_INDEX input.eye_index
#	else
#		define INPUT_EYE_INDEX 0.0
#	endif

#	if INSTANCED
#		define INPUT_INSTANCE_ID input.instance_id
#	else
#		define INPUT_INSTANCE_ID 0
#	endif

// A merged draw carries its colour in its own stream; every other call reads the attribute.
#	if MERGE_INSTANCED
#		define VERTEX_COLOR mergedColor
#	else
#		define VERTEX_COLOR input.color
#	endif

#	if TESSELLATE_DISP_HEIGHT
struct VertexOutput
{
	float4 position: POSITION0;
	float3 normal: NORMAL0;
	float3 binormal: BINORMAL0;
	float3 tangent: TANGENT0;
	float2 texcoord: TEXCOORD0;
#		if EYE && DISMEMBERMENT
	float2 dismember: TEXCOORD7;
#		endif
#		if VC
	float4 color: COLOR0;
#		endif
	float4 position1: POSITION1;
	float4 position2: POSITION2;
};
#	else
struct VertexOutput
{
	float4 position: SV_POSITION;
	float3 tangentRow0: TEXCOORD0;
#		if FACE
	// The face lane packs into the spare component of the first tangent row.
	float eyeIndexSquare: TEXCOORD6;
#		endif
	float3 tangentRow1: TEXCOORD1;
	float3 tangentRow2: TEXCOORD2;
	float4 currentPositionAndU: TEXCOORD3;
	float4 previousPositionAndV: TEXCOORD4;
#		if VC
	float4 color: COLOR0;
#		endif
#		if BONE_TINTING
	float4 boneTint: COLOR1;
#		endif
#		if INSTANCED || COMBINED
	uint instanceIndex: COLOR2;
#		endif
#		if LOD_HEIGHT_DROP
	float2 blendCoord: TEXCOORD9;
#		endif
#		if DISMEMBERMENT_MEATCUFF
	float2 dismember: TEXCOORD6;
	float3 cuffRadial: TEXCOORD7;
	float3 cuffAxis: TEXCOORD8;
	float3 cuffNormal: TEXCOORD9;
#		endif
#		if LANDSCAPE
	float2 terrainTexcoord: TEXCOORD6;
	float4 terrainBlend: TEXCOORD7;
	float3 terrainFade: TEXCOORD8;
#		endif
#		if PIPBOY_SCREEN
	float3 screenPosition: TEXCOORD6;
#		endif
#		if CLIP_VOLUME
	float clipDistance: SV_ClipDistance;
#		endif
};
#	endif

// The native folds the frame offset lanes into the first three row translations.
float3 TransformPosition(float4 row0, float4 row1, float4 row2, float3 offset, float4 position)
{
	float3 translation = float3(row0.w - offset.x, row1.w - offset.y, row2.w - offset.z);
	return float3(
		dot(float4(row0.xyz, translation.x), position),
		dot(float4(row1.xyz, translation.y), position),
		dot(float4(row2.xyz, translation.z), position));
}

// A blade offsets both frames at once, so the second transform takes its rows already folded.
float3 TransformRows(float4 row0, float4 row1, float4 row2, float3 translation, float4 position)
{
	return float3(
		dot(float4(row0.xyz, translation.x), position),
		dot(float4(row1.xyz, translation.y), position),
		dot(float4(row2.xyz, translation.z), position));
}

#	if LANDSCAPE
// The terrain patch is axis aligned, so the transform above collapses to a two term axis dot.
float3 OffsetPosition(float4 position, float3 offset)
{
	return float3(
		dot(float2(1.0, 1.0), float2(position.x, -offset.x)),
		dot(float2(1.0, 1.0), float2(position.y, -offset.y)),
		dot(float2(1.0, 1.0), float2(position.z, -offset.z)));
}
#	endif

// Keep the cbuffer row first so fxc emits the native dp4 operand order.
float3 ConcatenateRow(float4 row, float4 column0, float4 column1, float4 column2)
{
	return float3(dot(row, column0), dot(row, column1), dot(row, column2));
}

float3 ConcatenateRow(float4 row, float3 column0, float3 column1, float3 column2)
{
	return float3(dot(row.xyz, column0), dot(row.xyz, column1), dot(row.xyz, column2));
}

// Keep the cbuffer row first so fxc emits the native dp4 operand order.
float3 ProjectRows(float4 row0, float4 row1, float4 row2, float4 position)
{
	return float3(dot(row0, position), dot(row1, position), dot(row2, position));
}

float3 ProjectRows(float3 row0, float3 row1, float3 row2, float3 direction)
{
	return float3(dot(row0, direction), dot(row1, direction), dot(row2, direction));
}

// A cleared texture attribute drops its scale-bias with it, so the lane leaves as a literal zero.
float2 ScaleTexcoord(float2 texcoord, float4 scaleBias)
{
#	if TEXTURE
	return texcoord * scaleBias.zw + scaleBias.xy;
#	else
	return float2(0.0, 0.0);
#	endif
}

// Keep the projected row first so fxc emits the native dp3 operand order.
float3 ProjectBasis(float3 row, float3 tangent, float3 binormal, float3 normal)
{
	return float3(dot(row, tangent), dot(row, binormal), dot(row, normal));
}

#	if MERGE_INSTANCED
// The merged object owns a 4x4 whose upper 3x3 turns a model direction into the merged frame.
float3 TurnMerged(MergedTransform transform, float3 direction)
{
	return ProjectBasis(direction,
		float3(transform.row0.x, transform.row1.x, transform.row2.x),
		float3(transform.row0.y, transform.row1.y, transform.row2.y),
		float3(transform.row0.z, transform.row1.z, transform.row2.z));
}

// The last row of that 4x4 translates the turned position and its w scales it.
float3 PlaceMerged(MergedTransform transform, float3 position)
{
	return transform.row3.w * TurnMerged(transform, position) + transform.row3.xyz;
}
#	endif

#	if LOD_OBJECT_INSTANCED
// Rodrigues on a unit quaternion: the doubled cross turns a model direction into instance space.
float3 RotateByQuaternion(float4 rotation, float3 direction)
{
	float3 turn = 2.0 * cross(rotation.xyz, direction);
	return direction + rotation.w * turn + cross(rotation.xyz, turn);
}
#	endif

#	if GRASS
// A blade leans on a gust whose shape folds a doubled sine into a cosine ripple.
float GrassGust(float angle, float halfSpan)
{
	float phase;
	float ripple;
	sincos(angle, phase, ripple);
	float2 wave = sin(phase * float2(3.1415927, 6.2831853));
	float gust = (wave.x + wave.y) * 0.3 + cos(ripple * 3.1415927) * 0.2 + 1.0;
	return gust * halfSpan + WindVectorEx.x;
}

// Each collider pushes the blade sideways in clip space only, so the world lane keeps its place.
float3 CollideGrass(float3 world, float contact)
{
	float2 pushed = world.xy;
	[loop] for (uint collider = 0; collider < 4; ++collider)
	{
		float4 sphere = ActorCollision[collider];
		float3 delta = float3(pushed, world.z) - float3(sphere.xy, sphere.w * 0.17 + world.z);
		float reach = length(delta);
		float bite = max(sphere.w - reach, 0.0);
		float fade = saturate(reach / (0.33 * sphere.w));
		float2 direction = normalize(delta).xy;
		float falloff = 1.0 - fade;
		pushed += contact * (direction * (bite * falloff)) * sphere.z;
	}
	return float3(pushed, world.z);
}
#	endif

#	if TREE_ANIM || SPLINE
// Half the sway band, shared by every gust the wind rows drive.
float WindHalfRange()
{
	return (WindVectorEx.y - WindVectorEx.x) * 0.5;
}
#	endif

#	if SPLINE
// The drift folds a slow beat of the instance seed into the gust before the throw.
float SplineDrift(float seed, float beat, float clock)
{
	return seed * sin(seed * beat) / clock;
}
#	endif

#	if TREE_ANIM
// A trunk leans with its height ratio, and the doubled sine shapes each gust.
float3 TreeThrow(float ratio, float seed, float wind)
{
	float2 gust;
#		if SKINNED
	// A skinned trunk brings no seed, so the gust folds the wind term ahead of the height lean.
	gust = sin(sin(seed + wind + ratio * -1.5) * float2(3.1415927, 6.2831853));
#		else
	gust = sin(sin(ratio * -1.5 + seed + wind) * float2(3.1415927, 6.2831853));
#		endif
	float3 swing = ((gust.x * 0.3 + gust.y + 2.0) * WindHalfRange() + WindVectorEx.x) * ratio;
	return swing / 3.0;
}

// The trunk keeps its length as it leans, so the bend renormalises the offset from its base.
float3 BendTree(float3 origin, float3 delta, float distance, float3 lean)
{
	return normalize(delta + lean) * distance + origin;
}
#	endif

#	if LANDSCAPE
// The fade measures the patch position, so it is taken before the frame offset.
float TerrainFade(float4 position)
{
	float2 delta = LandLODParamsVS.zw - float2(dot(World_row0, position), dot(World_row1, position));
	return 1.0 - saturate((9625.6 - length(delta)) / 2662.4);
}
#	endif

#	if SKINNED
// A palette entry is three rows; the native subtracts the frame offset from every row it blends.
float3x4 BoneMatrix(float4 row0, float4 row1, float4 row2, float3 offset)
{
	return float3x4(
		row0 - float4(0.0, 0.0, 0.0, offset.x),
		row1 - float4(0.0, 0.0, 0.0, offset.y),
		row2 - float4(0.0, 0.0, 0.0, offset.z));
}

float3x4 CurrentBone(int index, float3 offset)
{
	return BoneMatrix(cb10_bone_transform[index], cb10_bone_transform[index + 1],
		cb10_bone_transform[index + 2], offset);
}

float3x4 PreviousBone(int index, float3 offset)
{
	return BoneMatrix(cb9_previous_bone_transform[index], cb9_previous_bone_transform[index + 1],
		cb9_previous_bone_transform[index + 2], offset);
}

// Keep the frame vector first so fxc emits the native dp3 operand order.
float3 SkinDirection(float3 direction, float3x4 skin)
{
	return float3(dot(direction, skin[0].xyz), dot(direction, skin[1].xyz),
		dot(direction, skin[2].xyz));
}

float4 SkinPosition(float4 position, float3x4 skin)
{
	return float4(dot(position, skin[0]), dot(position, skin[1]), dot(position, skin[2]), 1.0);
}
#	endif

#	if BONE_TINTING && SKINNED
// A palette row admits an influence when the row owns its index group and holds its index bit.
float4 BoneTintRow(uint4 group, uint4 owner, uint4 bit, uint4 mask, float4 weight)
{
	return (group == owner) ? ((bit & mask) ? weight : 0.0) : 0.0;
}
#	endif

// A wind regime already spent the vertex alpha on its sway, so that lane leaves opaque.
#	if TREE_ANIM || SPLINE
#		define OUTPUT_ALPHA 1.0
#	else
#		define OUTPUT_ALPHA VERTEX_COLOR.a
#	endif

VertexOutput main(VertexInput input)
{
	VertexOutput output;

#	if STRUCTURED_TERRAIN
	int quadIndex = (int)cb13_terrain_quad[INPUT_INSTANCE_ID].x;
	int gridIndex = (int)input.vertex_id;
	int gridColumn = gridIndex % 17;
	int gridRow = (gridIndex - gridColumn) / 17;
	float2 gridCell = float2(gridColumn, gridRow);
	// Order is load-bearing: the scaled cell claims the first converted lane pair.
	float2 gridTexcoord = gridCell * 0.75;
	float2 gridLocal = gridCell * 128.0 - 2048.0;
	TerrainQuad quad = g_TerrainQuads[quadIndex];
	TerrainVertex corner = g_TerrainVertices[quadIndex * 289 + gridIndex];
	float4 position = float4(float3(gridLocal, corner.height) + quad.origin, 1.0);
	float fade = TerrainFade(position);
#	elif LOD_OBJECT_INSTANCED
	LodObjectTransform lodObject = g_LodObjectTransforms[g_LodObjectRemap[input.instance_id]];
	float4 position = float4(RotateByQuaternion(lodObject.rotation, input.position.xyz) * lodObject.translation_and_scale.w + lodObject.translation_and_scale.xyz, 1.0);
#	elif MERGE_INSTANCED
	uint mergedVertex = input.vertex_id + VertexAttribOffsets2.z;
	uint mergedAddress = LoadMergedIndex(g_MergedIndices, mergedVertex) * VertexAttribOffsets2.x;
	float4 mergedPosition = LoadMergedPosition(mergedAddress);
	uint mergedColorPacked = g_MergedVertices.Load(mergedAddress + VertexAttribOffsets1.w);
	float4 mergedColor = UnpackUnorm4(mergedColorPacked);
	// The object lookup only places the vertex, so it follows the stream the vertex came from.
	MergedTransform merged = g_MergedTransforms[LoadMergedIndex(g_MergedObjects,
		mergedVertex / (VertexAttribOffsets2.y * 3))];
	// An ablated direction axis drops the stream lane it reads, leaving a neutral packed word.
#		if NORMALS
	uint mergedNormalPacked = g_MergedVertices.Load(mergedAddress + VertexAttribOffsets1.x);
#		else
	uint mergedNormalPacked = 0;
#		endif
#		if BINORMAL_TANGENT
	uint mergedBinormalPacked = g_MergedVertices.Load(mergedAddress + VertexAttribOffsets1.y);
#		else
	uint mergedBinormalPacked = 0;
#		endif
	float2 mergedTexcoord =
		UnpackHalf2(g_MergedVertices.Load(mergedAddress + VertexAttribOffsets1.z));
	float4 position = float4(PlaceMerged(merged, mergedPosition.xyz), 1.0);
#	else
	float4 position = float4(input.position.xyz, 1.0);
#	endif

#	if STRUCTURED_TERRAIN
	float3 normal = corner.normal;
	float3 binormal = corner.binormal;
	float3 tangent = corner.tangent;
#	elif MODELSPACENORMALS
	float3 normal = float3(0.0, 0.0, 1.0);
	float3 binormal = float3(0.0, 1.0, 0.0);
	float3 tangent = float3(1.0, 0.0, 0.0);
#	elif MERGE_INSTANCED
	float3 normal = TurnMerged(merged, UnpackSigned3(mergedNormalPacked));
	float3 binormal = TurnMerged(merged, UnpackSigned3(mergedBinormalPacked));
	float3 tangent = TurnMerged(merged, float3(mergedPosition.w,
											UnpackSignedByte(mergedNormalPacked, 24),
											UnpackSignedByte(mergedBinormalPacked, 24)));
#	elif GRASS
	// A blade unpacks its whole frame before it normalises, so the rows arrive together.
	float3 bladeNormal = INPUT_NORMAL.xyz * 2.0 - 1.0;
	float3 bladeBinormal = INPUT_BINORMAL.xyz * 2.0 - 1.0;
	float3 normal = normalize(bladeNormal);
	float3 binormal = normalize(bladeBinormal);
	float3 tangent = normalize(float3(input.position.w, INPUT_NORMAL.w * 2.0 - 1.0,
		INPUT_BINORMAL.w * 2.0 - 1.0));
#	else
	float3 normal = normalize(INPUT_NORMAL.xyz * 2.0 - 1.0);
	float3 binormal = normalize(INPUT_BINORMAL.xyz * 2.0 - 1.0);
	float3 tangent = normalize(float3(input.position.w, INPUT_NORMAL.w * 2.0 - 1.0,
		INPUT_BINORMAL.w * 2.0 - 1.0));
#	endif

// A blade owns its texture lane before it leans, so the scale-bias is taken with the basis.
#	define GRASS_TEXCOORD (GRASS && !INSTANCED && !MERGE_INSTANCED)

#	if GRASS_TEXCOORD
	float2 texcoord = ScaleTexcoord(INPUT_TEXCOORD, TexcoordOffset);
#	endif
#	if GRASS
	// The blade carries its own basis: two attribute rows and a third from their spare lanes.
	float3 grassRow2 = float3(input.grass_row2.x, input.grass_row0.w, input.grass_row1.w);
	float3 grassPlaced = ProjectRows(input.grass_row0.xyz, input.grass_row1.xyz, grassRow2,
		(input.grass_row2.y * ScaleMask_row0.xyz + 1.0) * input.position.xyz);
	normal = ProjectRows(input.grass_row0.xyz, input.grass_row1.xyz, grassRow2, normal);
	binormal = ProjectRows(input.grass_row0.xyz, input.grass_row1.xyz, grassRow2, binormal);
	tangent = ProjectRows(input.grass_row0.xyz, input.grass_row1.xyz, grassRow2, tangent);
#		if VC
	float2 grassAngle = float2(WindVector.w, WindVector.z) - (input.grass_origin.x + input.grass_origin.y) * 0.0078125;
	float grassSpan = (WindVectorEx.y - WindVectorEx.x) * 0.5;
	grassAngle *= WindVectorEx.z;
	float grassSweep = GrassGust(grassAngle.x, grassSpan) * (VERTEX_COLOR.a * VERTEX_COLOR.a * 0.5);
	float3 grassLean;
	sincos(WindVector.x, grassLean.y, grassLean.x);
	grassLean.z = 0.0;
	float3 grassGust = grassLean * grassSweep;
#		else
	float3 grassGust = float3(0.0, 0.0, 0.0);
#		endif
	normal = normalize(normal + grassGust);
	binormal = normalize(binormal + grassGust);
	tangent = normalize(tangent + grassGust);
	position.xyz = grassPlaced + grassGust + input.grass_origin.xyz;
#		if VC && !(TREE_ANIM || SPLINE)
	// The previous gust only shapes the previous frame, so it is taken after the blade lands.
	float3 grassPreviousGust = grassLean * (GrassGust(grassAngle.y, grassSpan) * (VERTEX_COLOR.a * VERTEX_COLOR.a * 0.5));
#		elif !(TREE_ANIM || SPLINE)
	float3 grassPreviousGust = float3(0.0, 0.0, 0.0);
#		endif
#		if TREE_ANIM || SPLINE
	// A blade under another wind regime carries one gust, so both frames leave from it.
	float4 previousPosition = float4(position.xyz, 1.0);
#		else
	float4 previousPosition =
		float4(grassPlaced + (input.grass_origin.xyz + grassPreviousGust), 1.0);
#		endif
#		if VC
	// A blade tints with its instance lane, so the colour is taken where the blade lands.
	float3 grassColor = input.color.rgb * input.grass_origin.w;
#		endif
#	endif

#	if LOD_OBJECT_INSTANCED
	normal = RotateByQuaternion(lodObject.rotation, normal);
	binormal = RotateByQuaternion(lodObject.rotation, binormal);
	tangent = RotateByQuaternion(lodObject.rotation, tangent);
#	endif

#	if TREE_ANIM && !SKINNED
	// The trunk leans about both frame origins, so the two row translations land side by side.
	float3 treeOrigin = float3(World_row0.w - PosAdjust.x,
		World_row1.w - PosAdjust.y,
		World_row2.w - PosAdjust.z);
	float3 previousOrigin = float3(
		PreviousWorld_row0.w - PreviousPosAdjust.x,
		PreviousWorld_row1.w - PreviousPosAdjust.y,
		PreviousWorld_row2.w - PreviousPosAdjust.z);
#	endif

#	if TREE_ANIM
#		if GRASS
	// A blade already scales its own wind rows, so a grassy trunk takes the shape lane raw.
	float windStrength = TreeParams.w;
	float windClock = WindVectorEx.z * windStrength;
#		else
	// The trunk takes its strength and its leaf scale from one shape pair.
	float2 windScale = TreeParams.wz * float2(20.0, 0.035);
	float windStrength = windScale.x;
	float windClock = windStrength * WindVectorEx.z * 5.0;
#		endif
#		if SKINNED
	// A skinned trunk stands on the palette, not on a world row, so it seeds from the origin.
	float windSeed = 0.0;
#		else
	float windSeed = (World_row0.w - PosAdjust.x) + (World_row1.w - PosAdjust.y) + (World_row2.w - PosAdjust.z) + PosAdjust.x + PosAdjust.y + PosAdjust.z;
#		endif
	// The frame offset cancels out of the seed, so a tree keeps its phase as the origin moves.
	float windPhase = normal.x + normal.y + normal.z;
	windPhase = windPhase * 3.0 + windSeed;
	float2 windBeat = float2(WindVector.w, WindVector.z);
	float2 leafAngle = windBeat * windClock + windPhase;
#		if GRASS
	float leafScale = TreeParams.z * 0.035;
#		else
	float leafScale = windScale.y;
#		endif
	float2 leafGust = sin(leafAngle) * WindHalfRange() + WindVectorEx.x;
	float2 leafSway = leafGust * leafScale;
	float windWeight = saturate(VERTEX_COLOR.a);
#		if !GRASS && !SKINNED
	// A bare trunk keeps its previous frame where the leaf found it, so the sway lands later.
	float4 previousPosition = float4(position.xyz, 1.0);
#		endif
	position.xyz = normal * leafSway.x * windWeight + position.xyz;
#		if SKINNED
	// The palette blends an already bent vertex, so the trunk leans about the model origin.
	float treeRatio = (position.x + position.y + position.z) / TreeParams.x;
	float2 windTime = windBeat * WindVectorEx.z;
	float3 windLean;
	sincos(WindVector.x, windLean.y, windLean.x);
	windLean.z = 0.0;
	position.xyz = BendTree(float3(0.0, 0.0, 0.0), position.xyz, length(position.xyz),
		(TreeThrow(treeRatio, windSeed, windTime.x * windStrength) * windLean) * windWeight);
#		endif
#	endif

#	if INSTANCED && !LANDSCAPE
	InstanceTransform instance = cb13_instance[input.instance_id];
#	endif

#	if COMBINED
	uint transformIndex = (uint)INPUT_EYE_INDEX;
#		if !SKINNED
	CombinedTransform transform = g_CombinedTransforms[transformIndex];
#		endif
#	endif

#	if LOD_HEIGHT_DROP
// A skinned vertex has no single world row set, so its drop reads model space instead.
#		if SKINNED
	float3 blendPosition = position.xyz;
#		elif INSTANCED
	float3 blendPosition = TransformPosition(instance.row0, instance.row1, instance.row2,
		PosAdjust.xyz, position);
#		else
	float3 blendPosition = TransformPosition(World_row0, World_row1,
		World_row2, PosAdjust.xyz, position);
#		endif
	output.blendCoord = blendPosition.xy;
#		if !CLIP_VOLUME
	if (all(abs(blendPosition.xy - HighDetailRange.xy) < HighDetailRange.zw)) {
		// The near-zero height weight is the native's: it keeps the drop constant.
		position.z = position.z - (blendPosition.z * 0.000000001 + 230.0);
	}
#		endif
#	endif

#	if SPLINE && !TREE_ANIM
	// A spline sways both frames together, so the two swayed row translations land side by side.
	float3 splineOrigin = float3(World_row0.w - PosAdjust.x,
		World_row1.w - PosAdjust.y,
		World_row2.w - PosAdjust.z);
	float3 previousOrigin = float3(
		PreviousWorld_row0.w - PreviousPosAdjust.x,
		PreviousWorld_row1.w - PreviousPosAdjust.y,
		PreviousWorld_row2.w - PreviousPosAdjust.z);
	float splineRange = WindVectorEx.y - WindVectorEx.x;
	float2 splineBeat = float2(WindVector.w, WindVector.z) * 5.0;
	float splineSquare = splineRange * splineRange;
	float splineGust = splineSquare * 0.002;
	float3 splineClock = float3(WindVectorEx.z, WindVectorEx.z,
							 WindVectorEx.x) *
	                     float3(10.0, -40.0, 0.25);
	float splineSeed =
		dot(splineOrigin + PosAdjust.xyz, float3(1.0, 1.0, 1.0)) * 0.001;
	float splineDrift =
		SplineDrift(splineSeed, splineBeat.x, splineClock.x) * 3.1415927 + splineSeed;
	float splineWeight = WindVector.y * VERTEX_COLOR.a + 1.0;
	float splineAngle = splineWeight * splineDrift + splineClock.y;
	splineAngle = splineClock.x * splineBeat.x + splineAngle;
	float splineAmp = (splineGust * sin(splineAngle) + splineClock.z) * VERTEX_COLOR.a;
	float2 splineLean;
	sincos(WindVector.x, splineLean.y, splineLean.x);
	// Keep the lean packed and the translations scalar to preserve the native MAD allocation.
	float2 splineSway = splineLean * splineAmp;
	splineSway.x += splineOrigin.x;
	splineSway.y += splineOrigin.y;
	float previousSeed =
		dot(previousOrigin + PreviousPosAdjust.xyz, float3(1.0, 1.0, 1.0)) * 0.001;
	float previousDrift =
		SplineDrift(previousSeed, splineBeat.y, splineClock.x) * 3.1415927 + previousSeed;
	float previousAngle = splineWeight * previousDrift + splineClock.y;
	previousAngle = splineClock.x * splineBeat.x + previousAngle;
	float previousAmp = (splineGust * sin(previousAngle) + splineClock.z) * VERTEX_COLOR.a;
	float2 previousSway = splineLean * previousAmp;
	previousSway.x += previousOrigin.x;
	previousSway.y += previousOrigin.y;
#	endif

#	if GRASS && !SKINNED
	// The blade offsets both frames together, so the two row translations land side by side.
	float3 grassTranslation = float3(
		World_row0.w - PosAdjust.x,
		World_row1.w - PosAdjust.y,
		World_row2.w - PosAdjust.z);
	float3 grassPreviousTranslation = float3(
		PreviousWorld_row0.w - PreviousPosAdjust.x,
		PreviousWorld_row1.w - PreviousPosAdjust.y,
		PreviousWorld_row2.w - PreviousPosAdjust.z);
#	endif
#	if SKINNED
	float3 weights = input.blend_weight.xyz;
	float lastWeight = 1.0 - saturate(weights.x + weights.y + weights.z);
	int4 bone = (int4)(input.blend_indices * 765.01);
	float3x4 skin = weights.x * CurrentBone(bone.x, PosAdjust.xyz) + weights.y * CurrentBone(bone.y, PosAdjust.xyz) + weights.z * CurrentBone(bone.z, PosAdjust.xyz) + lastWeight * CurrentBone(bone.w, PosAdjust.xyz);
	float4 world = SkinPosition(position, skin);
#	elif TESSELLATE_DISP_HEIGHT
	float4 world = position;
#	elif STRUCTURED_TERRAIN
	float4 world = float4(OffsetPosition(position, PosAdjust.xyz), 1.0);
#	elif TREE_ANIM
	float4 worldRow0 = World_row0;
	float4 worldRow1 = World_row1;
	float4 worldRow2 = World_row2;
	float4 worldRow3 = World_row3;
	// The depth lane rides with the other three row dots, so it is taken before the bend.
	float3 treePlaced = TransformPosition(worldRow0, worldRow1, worldRow2,
		PosAdjust.xyz, position);
	float treeDepth = dot(worldRow3, position);
	float3 treeDelta = treePlaced - treeOrigin;
	float treeDistance = length(treeDelta);
	float treeRatio = (treeDelta.x + treeDelta.y + treeDelta.z) / TreeParams.x;
	float2 windTime = windBeat * WindVectorEx.z;
	// The flat lane keeps the lean a triple, so the sway never leaves the ground plane.
	float3 windLean;
	sincos(WindVector.x, windLean.y, windLean.x);
	windLean.z = 0.0;
	float4 world = float4(BendTree(treeOrigin, treeDelta, treeDistance,
							  (TreeThrow(treeRatio, windSeed, windTime.x * windStrength) * windLean) * windWeight),
		treeDepth);
#	elif SPLINE
	float4 worldRow0 = World_row0;
	float4 worldRow1 = World_row1;
	float4 worldRow2 = World_row2;
	float4 worldRow3 = World_row3;
	float4 world = float4(ProjectRows(
							  float4(worldRow0.xyz, splineSway.x),
							  float4(worldRow1.xyz, splineSway.y),
							  float4(worldRow2.xyz, splineOrigin.z), position),
		dot(worldRow3, position));
#	elif INSTANCED
	float4 worldRow0 = instance.row0;
	float4 worldRow1 = instance.row1;
	float4 worldRow2 = instance.row2;
	float4 world = float4(TransformPosition(worldRow0, worldRow1, worldRow2,
							  PosAdjust.xyz, position),
		1.0);
#	elif COMBINED
	float4 worldRow0 = transform.row0;
	float4 worldRow1 = transform.row1;
	float4 worldRow2 = transform.row2;
	float4 world = float4(TransformPosition(worldRow0, worldRow1, worldRow2,
							  PosAdjust.xyz, position),
		1.0);
#	elif GRASS
	float4 worldRow0 = World_row0;
	float4 worldRow1 = World_row1;
	float4 worldRow2 = World_row2;
	float4 worldRow3 = World_row3;
	float4 world = float4(TransformRows(worldRow0, worldRow1, worldRow2,
							  grassTranslation, position),
		dot(worldRow3, position));
#	else
	float4 worldRow0 = World_row0;
	float4 worldRow1 = World_row1;
	float4 worldRow2 = World_row2;
	float4 worldRow3 = World_row3;
	float4 world = float4(TransformPosition(worldRow0, worldRow1, worldRow2,
							  PosAdjust.xyz, position),
		dot(worldRow3, position));
#	endif

#	if SKINNED
	float3x4 previousSkin = weights.x * PreviousBone(bone.x, PreviousPosAdjust.xyz) + weights.y * PreviousBone(bone.y, PreviousPosAdjust.xyz) + weights.z * PreviousBone(bone.z, PreviousPosAdjust.xyz) + lastWeight * PreviousBone(bone.w, PreviousPosAdjust.xyz);
	float3 previous = SkinPosition(position, previousSkin).xyz;
#	elif TESSELLATE_DISP_HEIGHT
	float3 previous = position.xyz;
#	elif STRUCTURED_TERRAIN
	float3 previous = OffsetPosition(position, PreviousPosAdjust.xyz);
#	elif TREE_ANIM
	// The previous frame sways only where it is projected, so the leaf lane is taken here.
	previousPosition.xyz = normal * leafSway.y * windWeight + previousPosition.xyz;
	float3 previousDelta = TransformPosition(PreviousWorld_row0,
							   PreviousWorld_row1, PreviousWorld_row2,
							   PreviousPosAdjust.xyz, previousPosition) -
	                       previousOrigin;
	float previousRatio =
		(previousDelta.x + previousDelta.y + previousDelta.z) / TreeParams.x;
	float3 previous = BendTree(previousOrigin, previousDelta, treeDistance,
		(windLean * TreeThrow(previousRatio, windSeed, windTime.y * windStrength)) * windWeight);
#	elif SPLINE
	float3 previous = ProjectRows(
		float4(PreviousWorld_row0.xyz, previousSway.x),
		float4(PreviousWorld_row1.xyz, previousSway.y),
		float4(PreviousWorld_row2.xyz, previousOrigin.z), position);
#	elif GRASS
	float3 previous = TransformRows(PreviousWorld_row0,
		PreviousWorld_row1, PreviousWorld_row2,
		grassPreviousTranslation, previousPosition);
#	elif INSTANCED
	float3 previous = TransformPosition(worldRow0, worldRow1, worldRow2,
		PreviousPosAdjust.xyz, position);
#	elif COMBINED
	float3 previous = TransformPosition(transform.previous_row0, transform.previous_row1,
		transform.previous_row2, PreviousPosAdjust.xyz, position);
#	else
	float3 previous = TransformPosition(PreviousWorld_row0,
		PreviousWorld_row1, PreviousWorld_row2,
		PreviousPosAdjust.xyz, position);
#	endif

#	if SKINNED
	float3 worldNormal = normalize(SkinDirection(normal, skin));
	float3 worldBinormal = normalize(SkinDirection(binormal, skin));
	float3 worldTangent = normalize(SkinDirection(tangent, skin));
#	elif TESSELLATE_DISP_HEIGHT
	float3 worldNormal = normal;
	float3 worldBinormal = binormal;
	float3 worldTangent = tangent;
#	elif STRUCTURED_TERRAIN
	float3 column0 = float3(1.0, 0.0, 0.0);
	float3 column1 = float3(0.0, 1.0, 0.0);
	float3 column2 = float3(0.0, 0.0, 1.0);
#	elif INSTANCED || COMBINED
	float3 column0 = float3(worldRow0.x, worldRow1.x, worldRow2.x);
	float3 column1 = float3(worldRow0.y, worldRow1.y, worldRow2.y);
	float3 column2 = float3(worldRow0.z, worldRow1.z, worldRow2.z);
#	else
	float4 column0 = float4(worldRow0.x, worldRow1.x, worldRow2.x, worldRow3.x);
	float4 column1 = float4(worldRow0.y, worldRow1.y, worldRow2.y, worldRow3.y);
	float4 column2 = float4(worldRow0.z, worldRow1.z, worldRow2.z, worldRow3.z);
#	endif

#	if TESSELLATE_DISP_HEIGHT
	output.position = world;
	output.normal = worldNormal;
	output.binormal = worldBinormal;
	output.tangent = worldTangent;
#		if EYE && DISMEMBERMENT
	output.dismember = float2(input.eye_index * CUT_AXIS.w, 0.0);
#		endif
	output.texcoord = ScaleTexcoord(INPUT_TEXCOORD, TexcoordOffset);
#		if VC
	output.color = float4(pow(input.color.rgb, 2.2), input.color.a);
#		endif
	output.position1 = world;
	output.position2 = float4(previous, 1.0);
#	else
#		if GRASS && VC
	// The colliders only push the blade in clip space, so the world lane keeps its own place.
	float4 projected =
		float4(CollideGrass(world.xyz, saturate(ceil(VERTEX_COLOR.a))), world.w);
#		else
	float4 projected = world;
#		endif
	output.position.x = dot(WorldToClip_row0, projected);
	output.position.y = dot(WorldToClip_row1, projected);
	output.position.z = dot(WorldToClip_row2, projected);
	output.position.w = dot(WorldToClip_row3, projected);
#		if GRASS && VC
	// A blade fades out with distance, so its alpha lane carries the fade instead.
	output.color = float4(pow(grassColor, 2.2),
		1.0 - saturate((length(projected.xyz) - AlphaParam_row0.z) / AlphaParam_row0.w));
#		endif
#		if MOTION_LANES
	output.currentPositionAndU.xyz = world.xyz;
#		else
	output.currentPositionAndU.xyz = float3(0.0, 0.0, 0.0);
#		endif
#		if CLIP_VOLUME
	output.clipDistance =
		length((world.xyz - ClipVolume_row0.xyz) / ClipVolume_row1.xyz) - 1.0;
#		endif
#		if FACE
	output.eyeIndexSquare = INPUT_EYE_INDEX * INPUT_EYE_INDEX;
#		endif
#		if SKINNED
	output.tangentRow0 = ProjectBasis(WorldToView_row0.xyz, worldTangent, worldBinormal,
		worldNormal);
	output.tangentRow1 = ProjectBasis(WorldToView_row1.xyz, worldTangent, worldBinormal,
		worldNormal);
	output.tangentRow2 = ProjectBasis(WorldToView_row2.xyz, worldTangent, worldBinormal,
		worldNormal);
#		else
	float3 viewRow0 = ConcatenateRow(WorldToView_row0, column0, column1, column2);
	float3 viewRow1 = ConcatenateRow(WorldToView_row1, column0, column1, column2);
	float3 viewRow2 = ConcatenateRow(WorldToView_row2, column0, column1, column2);
	output.tangentRow0 = ProjectBasis(viewRow0, tangent, binormal, normal);
	output.tangentRow1 = ProjectBasis(viewRow1, tangent, binormal, normal);
	output.tangentRow2 = ProjectBasis(viewRow2, tangent, binormal, normal);
#		endif
#		if STRUCTURED_TERRAIN
	float2 texcoord = gridTexcoord;
#		elif MERGE_INSTANCED
	float2 texcoord = ScaleTexcoord(mergedTexcoord, TexcoordOffset);
#		elif INSTANCED
	float2 texcoord = ScaleTexcoord(INPUT_TEXCOORD, instance.texcoord_scale_bias);
#		elif !GRASS_TEXCOORD
	float2 texcoord = ScaleTexcoord(INPUT_TEXCOORD, TexcoordOffset);
#		endif
	output.currentPositionAndU.w = texcoord.x;
	output.previousPositionAndV.w = texcoord.y;
#		if MOTION_LANES
	output.previousPositionAndV.xyz = previous;
#		else
	output.previousPositionAndV.xyz = float3(0.0, 0.0, 0.0);
#		endif
#		if STRUCTURED_TERRAIN
#			if VC
	output.color = float4(pow(corner.color, 2.2), 1.0);
#			endif
#			if INSTANCED || COMBINED
	output.instanceIndex = quadIndex;
#			endif
#		else
#			if VC
#				if MERGE_INSTANCED
	output.color = float4(pow(merged.tint.x * mergedColor.rgb, 2.2), OUTPUT_ALPHA);
#				elif GRASS
	// The blade already wrote its faded colour with the clip position.
#				else
	output.color = float4(pow(input.color.rgb, 2.2), OUTPUT_ALPHA);
#				endif
#			endif
#			if BONE_TINTING
#				if SKINNED
	uint4 tintIndex = (uint4)(input.blend_indices * 255.01);
	uint4 tintBit = 1u << tintIndex;
	uint4 tintGroup = tintIndex & 96;
	float4 tintLane =
		BoneTintRow(tintGroup, 0, tintBit, BoneTintIndices[0], input.blend_weight) + BoneTintRow(tintGroup, 32, tintBit, BoneTintIndices[1], input.blend_weight) + BoneTintRow(tintGroup, 64, tintBit, BoneTintIndices[2], input.blend_weight) + BoneTintRow(tintGroup, 96, tintBit, BoneTintIndices[3], input.blend_weight);
	float tintWeight = tintLane.x + tintLane.y + tintLane.z + tintLane.w;
#				else
	float tintWeight = 1.0;
#				endif
	output.boneTint = float4(1.0, 1.0, 1.0, tintWeight);
#			endif
#			if DISMEMBERMENT_MEATCUFF
	float cutDepth = INPUT_EYE_INDEX * CUT_AXIS.w;
	output.dismember = float2(cutDepth, 0.0);
#				if MODELSPACENORMALS
	// The cuff frame is model space, so only a model-space cell resolves the cut plane.
	float3 cutOrigin = CUT_POINT.xyz + CUT_AXIS.xyz * -cutDepth;
	float3 cuffRadial = normalize(input.position.xyz - cutOrigin);
	float3 cuffAxis = normalize(CUT_AXIS.xyz - cutOrigin);
#				elif SKINNED
	float3 cuffRadial = worldNormal;
	float3 cuffAxis = float3(0.0, 0.0, 0.0);
#				else
	float3 cuffRadial = normal;
	float3 cuffAxis = float3(0.0, 0.0, 0.0);
#				endif
	output.cuffRadial = cuffRadial;
	output.cuffAxis = cuffAxis;
	output.cuffNormal = cross(cuffRadial, cuffAxis);
#			endif
#			if INSTANCED
	output.instanceIndex = input.instance_id;
#			elif COMBINED
	output.instanceIndex = transformIndex;
#			endif
#			if PIPBOY_SCREEN
	output.screenPosition = ProjectRows(WorldView_row0, WorldView_row1,
		WorldView_row2, position);
#			endif
#		endif
#		if LANDSCAPE
#			if STRUCTURED_TERRAIN
	float2 terrainOffset = quad.texcoord_offset;
#			else
	float2 terrainOffset = LandLODParamsVS.xy;
#			endif
	float2 terrainTexcoord = INPUT_TEXCOORD * (1.0 / 48.0) + terrainOffset;
	output.terrainTexcoord = float2(terrainTexcoord.x, 1.0 - terrainTexcoord.y);
#			if STRUCTURED_TERRAIN
	output.terrainBlend = float4(corner.blend, corner.alpha);
	output.terrainFade = float3(world.xy, quad.variant == -1 ? 0.0 : fade);
#			else
	output.terrainBlend = input.terrain;
#				if LOD_LANDSCAPE
	output.terrainFade = float3(world.xy, TerrainFade(position));
#				else
	output.terrainFade = float3(world.xy, 0.0);
#				endif
#			endif
#		endif
#	endif
	return output;
}
#else
#	error "Select one shader family stage or kernel."
#endif
