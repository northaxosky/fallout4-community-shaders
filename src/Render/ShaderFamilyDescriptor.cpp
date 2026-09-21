#include "Render/ShaderFamilyDescriptor.h"

#include "Log.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.shaderfamilydescriptor");

		void Define(
			ShaderInjectionDefines& a_defines,
			std::string_view a_name,
			std::string_view a_value = "1")
		{
			a_defines.insert_or_assign(
				std::string(a_name),
				std::string(a_value));
		}

		void DefineBit(
			ShaderInjectionDefines& a_defines,
			std::uint32_t a_descriptor,
			std::uint32_t a_mask,
			std::string_view a_name)
		{
			if ((a_descriptor & a_mask) != 0)
				Define(a_defines, a_name);
		}

		std::string ProfileForStage(ShaderStage a_stage)
		{
			switch (a_stage) {
			case ShaderStage::kVertex:
				return "vs_5_0";
			case ShaderStage::kPixel:
				return "ps_5_0";
			case ShaderStage::kCompute:
				return "cs_5_0";
			default:
				return {};
			}
		}

		bool AddPrepassDefines(
			ShaderInjectionDefines& a_defines,
			const ShaderFamilyDescriptor& a_family)
		{
			const auto a_descriptor = a_family.descriptor;
			const bool tessellatedVertex =
				a_family.stage == ShaderStage::kVertex
				&& (a_descriptor & ((1U << 19) | (1U << 20))) != 0;
			DefineBit(a_defines, a_descriptor, 1U << 0, "VC");
			Define(
				a_defines,
				"TEXTURE",
				(a_descriptor & (1U << 1)) != 0 ? "1" : "0");
			DefineBit(a_defines, a_descriptor, 1U << 2, "SKINNED");
			DefineBit(a_defines, a_descriptor, 1U << 3, "NORMALS");
			DefineBit(a_defines, a_descriptor, 1U << 4, "BINORMAL_TANGENT");
			DefineBit(a_defines, a_descriptor, 1U << 5, "LANDSCAPE");
			DefineBit(a_defines, a_descriptor, 1U << 6, "EYE");
			const auto grassBit =
				(a_descriptor & (1U << 7)) != 0;
			const auto grass =
				a_family.stage == ShaderStage::kVertex && grassBit;
			if (grass) {
				Define(a_defines, "GRASS");
				Define(a_defines, "MAX_ACTOR_VEGETATION_COLLISION", "4");
			}
			DefineBit(a_defines, a_descriptor, 1U << 8, "ALPHA_TEST");
			if ((a_descriptor & (1U << 9)) != 0
				|| (a_family.stage == ShaderStage::kVertex
					&& !tessellatedVertex
					&& (a_descriptor & (1U << 25)) != 0)) {
				Define(a_defines, "LOD_LANDSCAPE");
			}
			if ((a_descriptor & (1U << 10)) != 0
				&& (a_family.stage == ShaderStage::kVertex || !grassBit))
				Define(a_defines, (a_descriptor & (1U << 23)) != 0 ?
					"SPLINE" : "TREE_ANIM");
			DefineBit(a_defines, a_descriptor, 1U << 12, "CHARACTER_LIGHT_MASK");
			DefineBit(a_defines, a_descriptor, 1U << 13, "MODELSPACENORMALS");
			DefineBit(a_defines, a_descriptor, 1U << 14, "GLOWMAP");
			DefineBit(a_defines, a_descriptor, 1U << 15, "BLEND");
			if ((a_descriptor & (1U << 11)) != 0)
				Define(a_defines, (a_descriptor & (1U << 16)) != 0 ?
					"SKEW_SPECULAR_ALPHA" : "LOD_OBJECT_INSTANCED");
			else
				DefineBit(a_defines, a_descriptor, 1U << 16, "MENU_SCREEN");
			if ((a_descriptor & (1U << 10)) == 0)
				DefineBit(a_defines, a_descriptor, 1U << 23, "PIPBOY_SCREEN");
			DefineBit(a_defines, a_descriptor, 1U << 17, "HAIR");
			DefineBit(a_defines, a_descriptor, 1U << 18, "SKIN_TINT");
			DefineBit(a_defines, a_descriptor, 1U << 19, "TESSELLATE_DISP_HEIGHT");
			DefineBit(a_defines, a_descriptor, 1U << 20, "TESSELLATE_DISP_NORMALS");
			DefineBit(a_defines, a_descriptor, 1U << 21, "DISMEMBERMENT");
			DefineBit(a_defines, a_descriptor, 1U << 22, "DISMEMBERMENT_MEATCUFF");
			DefineBit(a_defines, a_descriptor, 1U << 24, "ADDITIONAL_ALPHA_MASK");
			if (a_family.stage == ShaderStage::kPixel || tessellatedVertex)
				DefineBit(
					a_defines,
					a_descriptor,
					1U << 25,
					"LAND_LOD_BLEND");
			DefineBit(a_defines, a_descriptor, 1U << 26, "GRADIENT_REMAP");
			if ((a_descriptor & (1U << 27)) != 0) {
				if ((a_descriptor & (1U << 28)) != 0)
					Define(a_defines, "MERGE_INSTANCED");
				else {
					Define(a_defines, "INSTANCED");
					Define(a_defines, "WIN32_MAX_BATCH_INSTANCES", "455");
				}
			} else {
				DefineBit(a_defines, a_descriptor, 1U << 28, "COMBINED");
			}
			DefineBit(a_defines, a_descriptor, 1U << 29, "CLIP_VOLUME");
			DefineBit(a_defines, a_descriptor, 1U << 30, "BONE_TINTING");
			DefineBit(a_defines, a_descriptor, 1U << 31, "FACE");
			Define(a_defines, "MOTION_VECTORS");
			if (a_family.stage == ShaderStage::kVertex
				&& a_defines.contains("GRASS")
				&& a_defines.contains("SPLINE")) {
				return false;
			}
			return true;
		}

		bool AddEffectDefines(
			ShaderInjectionDefines& a_defines,
			std::uint32_t a_descriptor)
		{
			static constexpr std::array<std::string_view, 31> names{
				"VC", "SKINNED", "TEXTURE", "INDEXED_TEXTURE", "FALLOFF",
				"ADDBLEND", "MULTBLEND", "PARTICLES", "STRIP_PARTICLES",
				"MEMBRANE", "LIGHTING", "PROJECTED_UV", "SOFT",
				"GRAYSCALE_TO_COLOR", "GRAYSCALE_TO_ALPHA",
				"IGNORE_TEX_ALPHA", "MULTBLEND_DECAL", "ALPHA_TEST",
				"SKY_OBJECT", "ENVMAP", "PIPBOY_SCREEN", "RGB_FALLOFF",
				"PARTICLE_DISTORTION", "NORMALS", "ENVCUBE_RAIN",
				"ENVCUBE_SNOW", "EYE", "UI_MASK_RECTS", "DECAL",
				"MERGE_INSTANCED", "PREMULTIPLY_ALPHA"
			};
			for (std::uint32_t bit = 0; bit < names.size(); ++bit)
				DefineBit(a_defines, a_descriptor, 1U << bit, names[bit]);
			return true;
		}

		bool AddUtilityDefines(
			ShaderInjectionDefines& a_defines,
			std::uint32_t d)
		{
			DefineBit(a_defines, d, 1U << 0, "VC");
			DefineBit(a_defines, d, 1U << 1, "TEXTURE");
			DefineBit(a_defines, d, 1U << 2, "SKINNED");
			DefineBit(a_defines, d, 1U << 3, "NORMALS");
			DefineBit(a_defines, d, 1U << 4, "BINORMAL_TANGENT");
			DefineBit(a_defines, d, 1U << 6, "EYE");
			DefineBit(a_defines, d, 1U << 7, "ALPHA_TEST");
			if ((d & (1U << 9)) != 0 && (d & (1U << 12)) == 0)
				Define(a_defines, "RENDER_NORMAL");
			DefineBit(a_defines, d, 1U << 10, "RENDER_NORMAL_FALLOFF");
			DefineBit(a_defines, d, 1U << 11, "RENDER_NORMAL_CLAMP");
			if ((d & (1U << 12)) != 0 && (d & (1U << 9)) == 0)
				Define(a_defines, "RENDER_NORMAL_CLEAR");
			DefineBit(a_defines, d, 1U << 13, "RENDER_DEPTH");
			if ((d & 0x1C000U) != 0)
				Define(a_defines, "RENDER_SHADOWMAP");
			DefineBit(a_defines, d, 1U << 15, "RENDER_SHADOWMAP_CLAMPED");
			DefineBit(a_defines, d, 1U << 16, "RENDER_SHADOWMAP_PB");
			if ((d & 0x4A0000U) != 0)
				Define(a_defines, "DEBUG_COLOR");
			DefineBit(a_defines, d, 1U << 18, "DEBUG_SHADOWSPLIT");
			DefineBit(a_defines, d, 1U << 20, "GRAYSCALE_MASK");
			DefineBit(a_defines, d, 1U << 25, "RENDER_BASE_TEXTURE");
			DefineBit(a_defines, d, 1U << 26, "TREE_ANIM");
			DefineBit(a_defines, d, 1U << 27, "LOD_OBJECT");
			DefineBit(a_defines, d, 1U << 24, "VATS_MASK");
			if ((d & (1U << 9)) != 0 && (d & (1U << 12)) != 0)
				Define(a_defines, "STENCIL_ABOVE_WATER");
			DefineBit(a_defines, d, 1U << 30, "SPLINE");
			if ((d & (1U << 23)) != 0) {
				if ((d & (1U << 21)) != 0)
					Define(a_defines, "MERGE_INSTANCED");
				else {
					Define(a_defines, "INSTANCED");
					Define(a_defines, "WIN32_MAX_BATCH_INSTANCES", "455");
				}
			} else {
				DefineBit(a_defines, d, 1U << 21, "COMBINED");
			}
			DefineBit(a_defines, d, 1U << 28, "ADDITIONAL_ALPHA_MASK");
			DefineBit(a_defines, d, 1U << 29, "VATS_DEBUG_COLOR");
			DefineBit(a_defines, d, 1U << 8, "CLIP_VOLUME");
			return true;
		}

		bool AddWaterDefines(
			ShaderInjectionDefines& a_defines,
			std::uint32_t d)
		{
			Define(a_defines, "WATER");
			DefineBit(a_defines, d, 1U << 0, "VC");
			DefineBit(a_defines, d, 1U << 1, "NORMAL_TEXCOORD");
			DefineBit(a_defines, d, 1U << 2, "REFLECTIONS");
			DefineBit(a_defines, d, 1U << 3, "REFRACTIONS");
			DefineBit(a_defines, d, 1U << 4, "DEPTH");
			DefineBit(a_defines, d, 1U << 6, "WADING");
			DefineBit(a_defines, d, 1U << 5, "INTERIOR");
			DefineBit(a_defines, d, 1U << 7, "VERTEX_ALPHA_DEPTH");
			DefineBit(a_defines, d, 1U << 8, "CUBEMAP");
			DefineBit(a_defines, d, 1U << 9, "SSLR");
			DefineBit(a_defines, d, 1U << 10, "CLIP_VOLUME");
			DefineBit(a_defines, d, 1U << 12, "UNDERWATER");

			const auto technique = (d >> 13) & 0xFU;
			switch (technique) {
			case 8:
				Define(a_defines, "LOD");
				break;
			case 9:
				Define(a_defines, "STENCIL");
				break;
			case 10:
				Define(a_defines, "STENCIL_DISPLACEMENT");
				break;
			case 11:
				Define(a_defines, "SIMPLE");
				break;
			case 12:
				Define(a_defines, "FOG");
				break;
			default:
				if (technique > 0 && technique < 8) {
					Define(a_defines, "SPECULAR");
					Define(
						a_defines,
						"NUM_SPECULAR_LIGHTS",
						std::to_string(technique));
				}
				break;
			}
			return true;
		}

		bool AddSkyDefines(
			ShaderInjectionDefines& a_defines,
			std::uint32_t a_descriptor)
		{
			switch (a_descriptor & 0xFFU) {
			case 0:
				Define(a_defines, "OCCLUSION");
				break;
			case 1:
				Define(a_defines, "DITHER");
				break;
			case 2:
				Define(a_defines, "TEX");
				Define(a_defines, "MOONMASK");
				break;
			case 3:
				Define(a_defines, "HORIZFADE");
				break;
			case 4:
				Define(a_defines, "TEX");
				break;
			case 5:
				Define(a_defines, "TEX");
				Define(a_defines, "CLOUDS");
				break;
			case 6:
				Define(a_defines, "TEX");
				Define(a_defines, "CLOUDS");
				Define(a_defines, "TEXLERP");
				break;
			case 7:
				Define(a_defines, "TEX");
				Define(a_defines, "CLOUDS");
				if (a_descriptor == 7)
					Define(a_defines, "TEXFADE");
				break;
			case 8:
				Define(a_defines, "TEX");
				Define(a_defines, "DITHER");
				break;
			default:
				return false;
			}
			return true;
		}

		bool AddParticleDefines(
			ShaderInjectionDefines& a_defines,
			ShaderStage a_stage,
			std::uint32_t a_descriptor)
		{
			if (a_descriptor > 5)
				return false;
			if (a_stage == ShaderStage::kPixel) {
				if (a_descriptor == 1 || a_descriptor == 3)
					Define(a_defines, "GRAYSCALE_TO_COLOR");
				if (a_descriptor == 2 || a_descriptor == 3)
					Define(a_defines, "GRAYSCALE_TO_ALPHA");
			} else if (a_stage == ShaderStage::kVertex) {
				if (a_descriptor == 4 || a_descriptor == 5) {
					Define(a_defines, "ENVCUBE");
					Define(a_defines, a_descriptor == 4 ? "SNOW" : "RAIN");
				}
			} else {
				return false;
			}
			return true;
		}

		bool AddLightingDefines(
			ShaderInjectionDefines& a_defines,
			ShaderStage a_stage,
			std::uint32_t a_descriptor)
		{
			if (a_stage == ShaderStage::kVertex) {
				if ((a_descriptor & 0x800U) != 0)
					Define(a_defines, "BSLIGHTING_VS_DISPLACED");
				else if ((a_descriptor & 0x4U) != 0)
					Define(a_defines, "BSLIGHTING_VS_REDUCED");
				else if ((a_descriptor & 0x2U) != 0)
					Define(a_defines, "BSLIGHTING_VS_SKINNED");
				else if ((a_descriptor & 0x100U) != 0)
					Define(a_defines, "BSLIGHTING_VS_WORLD");
				else
					Define(a_defines, "BSLIGHTING_VS_STATIC");
				DefineBit(a_defines, a_descriptor, 0x1U, "BSL_VERTEX_COLOR");
				return true;
			}
			if (a_stage != ShaderStage::kPixel)
				return false;

			if ((a_descriptor & 0xC0U) != 0)
				Define(a_defines, "BSLIGHTING_PS_RESOURCE");
			else if ((a_descriptor & 0x400U) != 0)
				Define(a_defines, "BSLIGHTING_PS_COLOR");
			else
				Define(a_defines, "BSLIGHTING_PS_CORE");
			DefineBit(a_defines, a_descriptor, 0x40U, "BSL_BASE_LUT");
			DefineBit(a_defines, a_descriptor, 0x80U, "BSL_OVERLAY");
			if ((a_descriptor & 0xC00U) == 0xC00U) {
				Define(a_defines, "BSL_NO_VERTEX_ALPHA");
			} else if ((a_descriptor & 0x600U) == 0x600U) {
				Define(a_defines, "BSL_VERTEX_TINT");
			} else if ((a_descriptor & 0x500U) == 0x500U) {
				Define(a_defines, "BSL_BASE_BLEND");
				Define(a_defines, "BSL_BASE_BLEND_TINT");
			} else {
				DefineBit(a_defines, a_descriptor, 0x100U, "BSL_ENVMAP");
				DefineBit(a_defines, a_descriptor, 0x200U, "BSL_GLOWMAP");
				DefineBit(a_defines, a_descriptor, 0x400U, "BSL_BASE_BLEND");
			}
			DefineBit(a_defines, a_descriptor, 0x4U, "BSL_REDUCED_NORMAL");
			return true;
		}

		bool AddBsdfLightDefines(
			ShaderInjectionDefines& a_defines,
			ShaderStage a_stage,
			std::uint32_t d)
		{
			if (a_stage == ShaderStage::kVertex) {
				if (d != 0)
					return false;
				Define(a_defines, "BSDFLIGHT_VS");
				Define(a_defines, "DIRSPLITS", "2");
				return true;
			}
			if (a_stage != ShaderStage::kPixel)
				return false;

			const auto lightKind = d & 0x7FU;
			if ((d & 0x140U) != 0) {
				Define(a_defines, "BSDFLIGHT_PS_OVERDRAW");
				Define(a_defines, "OVERDRAW");
				Define(a_defines, "RGBSPEC");
				Define(a_defines, "DIRSPLITS", "2");
				return true;
			}
			if (lightKind == 4U && (d & 0x800U) != 0) {
				Define(a_defines, "BSDFLIGHT_PS_ATTENUATION_ONLY");
				Define(a_defines, "POINTOMNI");
				Define(a_defines, "ATTENUATION_ONLY");
				Define(a_defines, "RGBSPEC");
				Define(a_defines, "DIRSPLITS", "2");
				return true;
			}
			enum class Family
			{
				kDeferred,
				kDirSplits1,
				kDirSplits2,
				kDirSplits3,
				kUnshadowed,
				kGobo,
				kShadowOnly,
				kShadowOnlyBlendSplit,
				kCharacter,
				kCharacterC26,
				kAmbient
			};
			Family family = Family::kDeferred;
			const bool characterLight = (d & 0x4000000U) != 0;
			if (characterLight
				&& (lightKind == 1U || (d & 0x400000U) != 0))
				family = Family::kCharacterC26;
			else if (characterLight)
				family = Family::kCharacter;
			else if (lightKind == 0U && (d & 0x20000U) != 0)
				family = Family::kAmbient;
			else if (lightKind == 4U && (d & 0x1000U) != 0)
				family = Family::kGobo;
			else if ((lightKind & 0x2U) != 0) {
				if ((d & 0x40000U) != 0) {
					family = (d & 0x1000000U) != 0 ?
						Family::kShadowOnlyBlendSplit :
						Family::kShadowOnly;
				} else if ((d & 0x8000000U) != 0) {
					family = (d & 0x10000U) != 0 ?
						Family::kDirSplits1 :
						Family::kDirSplits3;
				} else if ((d & 0x10000U) != 0) {
					family = Family::kDirSplits1;
				} else {
					family = Family::kDirSplits2;
				}
			} else if (lightKind == 1U || lightKind == 4U) {
				family = Family::kUnshadowed;
			}

			switch (family) {
			case Family::kCharacterC26:
				Define(a_defines, "BSDFLIGHT_PS_CHARACTER_LIGHT_C26");
				break;
			case Family::kCharacter:
				Define(a_defines, "BSDFLIGHT_PS_CHARACTER_LIGHT");
				break;
			case Family::kAmbient:
				Define(a_defines, "BSDFLIGHT_PS_AMBIENT");
				break;
			case Family::kGobo:
				Define(a_defines, "BSDFLIGHT_PS_GOBO");
				break;
			case Family::kShadowOnlyBlendSplit:
				Define(a_defines, "BSDFLIGHT_PS_SHADOW_ONLY_BLEND_SPLIT");
				break;
			case Family::kDirSplits3:
				Define(a_defines, "BSDFLIGHT_PS_DIRSPLITS3");
				break;
			case Family::kShadowOnly:
				Define(a_defines, "BSDFLIGHT_PS_SHADOW_ONLY");
				break;
			case Family::kDirSplits1:
				Define(a_defines, "BSDFLIGHT_PS_DIRSPLITS1");
				break;
			case Family::kDirSplits2:
				Define(a_defines, "BSDFLIGHT_PS_DIRSPLITS2");
				break;
			case Family::kUnshadowed:
				Define(a_defines, "BSDFLIGHT_PS_UNSHADOWED");
				break;
			case Family::kDeferred:
				Define(a_defines, "BSDFLIGHT_PS_DEFERRED");
				break;
			}
			if (family == Family::kCharacterC26)
				return true;
			if (family == Family::kCharacter) {
				Define(a_defines, "CHARACTER_LIGHT");
				Define(a_defines, "RGBSPEC");
				Define(a_defines, "DIRSPLITS", "2");
				return true;
			}
			if (family == Family::kAmbient) {
				Define(a_defines, "AMBIENT");
				Define(a_defines, "RGBSPEC");
				Define(a_defines, "DIRSPLITS", "2");
				return true;
			}

			DefineBit(a_defines, d, 0x200U, "SPECULAR");
			DefineBit(a_defines, d, 0x1000U, "GOBOPROJECTION");
			const bool attenuationOnly = (d & 0x800U) != 0;
			if (!attenuationOnly) {
				DefineBit(a_defines, d, 0x4000U, "IGNOREROUGHNESS");
				const bool combinedIgnoreMode =
					(d & 0xC000U) == 0xC000U;
				const bool ignoreRimSuppressed =
					combinedIgnoreMode
					&& (lightKind == 4U
						|| (lightKind == 8U
							&& (d & 0x1000U) == 0));
				if (!ignoreRimSuppressed)
					DefineBit(a_defines, d, 0x8000U, "IGNORERIM");
			}
			Define(a_defines, "RGBSPEC");
			if (lightKind == 1U || lightKind == 2U)
				Define(a_defines, "DIRECTIONAL");
			if (lightKind == 2U || lightKind == 8U ||
				lightKind == 16U || lightKind == 32U)
				Define(a_defines, "SHADOW");
			if (family == Family::kShadowOnly
				|| family == Family::kShadowOnlyBlendSplit)
				Define(a_defines, "SHADOW_ONLY");
			DefineBit(a_defines, d, 0x80000U, "FILTER_PCF1");
			DefineBit(a_defines, d, 0x100000U, "FILTER_PCF9");
			DefineBit(a_defines, d, 0x200000U, "FILTER_POISSON");
			if ((d & 0x4780103U) == 0x400002U)
				Define(a_defines, "FILTER_PCSS");
			DefineBit(a_defines, d, 0x800000U, "FILTER_PCSSPOISSON");
			if ((family == Family::kDirSplits2
					|| family == Family::kDirSplits3
					|| family == Family::kShadowOnlyBlendSplit)
				&& (d & 0x1000000U) != 0)
				Define(a_defines, "BLENDSPLIT");
			if (attenuationOnly)
				Define(a_defines, "ATTENUATION_ONLY");
			if (lightKind == 4U || lightKind == 8U || lightKind == 16U)
				Define(a_defines, "POINTOMNI");
			if (lightKind == 16U)
				Define(a_defines, "HALFOMNI");
			if (lightKind == 32U)
				Define(a_defines, "POINTSPOT");
			if (lightKind == 0U && (d & 0x400U) != 0)
				Define(a_defines, "SPOT");
			if ((d & 0x20000U) != 0)
				Define(a_defines, "AMBIENT");
			if ((d & 0x20000U) != 0 && family == Family::kDirSplits2)
				Define(a_defines, "AMBIENT_IBL_IN_LIGHT");
			switch (family) {
			case Family::kDirSplits1:
			case Family::kShadowOnly:
			case Family::kShadowOnlyBlendSplit:
				Define(a_defines, "DIRSPLITS", "1");
				break;
			case Family::kDirSplits3:
				Define(a_defines, "DIRSPLITS", "3");
				break;
			case Family::kAmbient:
			case Family::kCharacter:
			case Family::kCharacterC26:
				break;
			default:
				Define(a_defines, "DIRSPLITS", "2");
				break;
			}
			if (family == Family::kDeferred)
				Define(a_defines, "LIGHT_TYPE", "3");
			else if (family == Family::kDirSplits2
				|| family == Family::kShadowOnly)
				Define(a_defines, "LIGHT_TYPE", "1");
			return true;
		}

		bool AddBsdfCompositeDefines(
			ShaderInjectionDefines& a_defines,
			ShaderStage a_stage,
			std::uint32_t d)
		{
			if (a_stage == ShaderStage::kVertex) {
				Define(a_defines, "BSDFCOMPOSITE_VS");
				DefineBit(a_defines, d, 0x1U, "TEXTURE");
				if ((d & 0x1013U) != 0 && (d & 0x1013U) != 0x1010U)
					Define(a_defines, "GEOMETRY");
				DefineBit(a_defines, d, 0x1000U, "DECAL");
				return true;
			}
			if (a_stage != ShaderStage::kPixel)
				return false;

			switch (d) {
			case 0x1000U:
			case 0x11000U:
			case 0x81000U:
			case 0x91000U:
			case 0x181000U:
			case 0x191000U:
				Define(
					a_defines,
					"BSDFCOMPOSITE_PS_SSS_MRT_SURFACE_CONTACT");
				Define(
					a_defines,
					"WAVE5B_SSS_SURFACE_CONTACT_SHAPE",
					"1");
				return true;
			case 0x1020U:
			case 0x3000U:
			case 0x11020U:
			case 0x13000U:
				Define(
					a_defines,
					"BSDFCOMPOSITE_PS_SSS_MRT_RECORD_NORMAL");
				Define(
					a_defines,
					"WAVE5B_SSS_RECORD_NORMAL_SHAPE",
					"1");
				return true;
			default:
				break;
			}

			// The remaining native SSS MRT families still lack
			// stock-faithful reconstructions.
			if ((d & 0x1000U) != 0)
				return false;

			enum class Family
			{
				kCubeIbl,
				kAmbientCb47,
				kAmbientCb31,
				kAmbientCompact,
				kAmbientMinimal,
				kAccumulator2D,
				kFog2D,
				kNoTextureAccumulator,
				kNoTextureFog,
				kNoPosition,
				kNoPositionTexcoord
			};
			Family family = Family::kCubeIbl;
			switch (d) {
			case 0x800U:
			case 0x20800U:
			case 0x40800U:
			case 0x50800U:
			case 0x60800U:
			case 0x70800U:
				family = Family::kNoPosition;
				break;
			case 0x801U:
			case 0x805U:
			case 0x20801U:
			case 0x20805U:
			case 0x40801U:
			case 0x40805U:
			case 0x60801U:
			case 0x60805U:
			case 0x70801U:
			case 0x70805U:
				family = Family::kNoPositionTexcoord;
				break;
			case 0x10000U:
			case 0x200000U:
			case 0x210000U:
				family = Family::kAccumulator2D;
				break;
			case 0x204088U:
				family = Family::kNoTextureAccumulator;
				break;
			default:
				break;
			}
			if (family == Family::kCubeIbl) {
				if ((d & ~0x10300U) == 0x860U)
					family = Family::kAmbientCb47;
				else if ((d & ~0x70301U) == 0x820U)
					family = Family::kAmbientCb31;
				else if ((d & ~0x10240U) == 0x120U)
					family = Family::kAmbientCompact;
				else if ((d & ~0x200U) == 0x4068U)
					family = Family::kAmbientMinimal;
				else if ((d & ~0x30280U) == 0x4008U)
					family = Family::kNoTextureAccumulator;
				else if ((d & ~0x10280U) == 0x4048U)
					family = Family::kNoTextureFog;
				else if ((d & ~0x230280U) == 0x8U)
					family = Family::kAccumulator2D;
				else if ((d & 0x7FU) == 0x40U ||
					(d & 0x7FU) == 0x48U)
					family = Family::kFog2D;
			}

			switch (family) {
			case Family::kAmbientMinimal:
				Define(a_defines, "BSDFCOMPOSITE_PS_AMBIENT_IBL_MINIMAL_FAMILY");
				break;
			case Family::kAmbientCb47:
				Define(a_defines, "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY");
				break;
			case Family::kAmbientCb31:
				Define(a_defines, "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY");
				break;
			case Family::kAmbientCompact:
				Define(a_defines, "BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY");
				break;
			case Family::kNoPosition:
				Define(a_defines, "BSDFCOMPOSITE_PS_NO_SRV_POSITION");
				break;
			case Family::kNoPositionTexcoord:
				Define(a_defines, "BSDFCOMPOSITE_PS_NO_SRV_POSITION_TEXCOORD");
				break;
			case Family::kNoTextureFog:
				Define(a_defines, "BSDFCOMPOSITE_PS_NO_T0_FOG");
				break;
			case Family::kNoTextureAccumulator:
				Define(a_defines, "BSDFCOMPOSITE_PS_NO_T0_ACCUMULATOR");
				break;
			case Family::kFog2D:
				Define(a_defines, "BSDFCOMPOSITE_PS_2D_FOG");
				break;
			case Family::kAccumulator2D:
				Define(a_defines, "BSDFCOMPOSITE_PS_2D_ACCUMULATOR");
				break;
			case Family::kCubeIbl:
				Define(a_defines, "BSDFCOMPOSITE_PS_CUBE_IBL");
				break;
			}

			switch (family) {
			case Family::kCubeIbl:
				if ((d & 0x60U) == 0x20U) {
					Define(a_defines, "COMPOSITE_CB12_COUNT", "31");
					Define(a_defines, "COMPOSITE_FOG_STACK", "0");
					Define(a_defines, "COMPOSITE_MATERIAL_EXCLUSION", "0");
				}
				if ((d & 0x200U) == 0) {
					Define(
						a_defines,
						"COMPOSITE_CB2_COUNT",
						(d & 0x40U) != 0 ? "3" : "1");
					Define(a_defines, "COMPOSITE_MODULATION", "0");
				}
				DefineBit(a_defines, d, 0x1U, "COMPOSITE_UNUSED_TEXCOORD");
				break;
			case Family::kAmbientCb47:
				if ((d & 0x200U) == 0)
					Define(a_defines, "FO4_AMBIENT_OCCLUSION", "0");
				if ((d & 0x100U) == 0)
					Define(a_defines, "FO4_SKIN_BLUR", "0");
				DefineBit(a_defines, d, 0x10000U, "TILELIGHT");
				return true;
			case Family::kAmbientCb31:
				if (((d & 0x50000U) == 0)
					|| ((d & 0x60000U) == 0x20000U))
					Define(a_defines, "AMBIENT_DIFFUSE_SET_B", "0");
				if ((d & 0x200U) == 0)
					Define(a_defines, "AMBIENT_SSAO", "0");
				if ((d & 0x100U) == 0)
					Define(a_defines, "AMBIENT_SUBSURFACE_BLUR", "0");
				DefineBit(a_defines, d, 0x1U, "AMBIENT_UNUSED_TEXCOORD");
				return true;
			case Family::kAmbientCompact:
				DefineBit(a_defines, d, 0x40U, "FOGSTACK");
				DefineBit(a_defines, d, 0x200U, "OUTPUTMASK");
				DefineBit(a_defines, d, 0x10000U, "TILELIGHT");
				return true;
			case Family::kAmbientMinimal:
				DefineBit(a_defines, d, 0x200U, "OUTPUTMASK");
				return true;
			case Family::kAccumulator2D:
				Define(
					a_defines,
					"COMPOSITE_CB2_COUNT",
					(d & 0x200U) != 0 ? "6" : "1");
				DefineBit(a_defines, d, 0x80U, "COMPOSITE_MATERIAL_5");
				DefineBit(a_defines, d, 0x200U, "COMPOSITE_MODULATION");
				break;
			case Family::kFog2D:
				Define(
					a_defines,
					"COMPOSITE_CB2_COUNT",
					(d & 0x200U) != 0 ? "6" : "3");
				if ((d & 0x8000U) == 0) {
					Define(a_defines, "COMPOSITE_HAS_TYPE");
					Define(a_defines, "COMPOSITE_MATERIAL_EXCLUSION");
				}
				DefineBit(a_defines, d, 0x8U, "COMPOSITE_HAS_LIGHT");
				DefineBit(a_defines, d, 0x80U, "COMPOSITE_MATERIAL_5");
				DefineBit(a_defines, d, 0x200U, "COMPOSITE_MODULATION");
				DefineBit(a_defines, d, 0x8U, "COMPOSITE_SCENE_BLEND");
				DefineBit(a_defines, d, 0x8U, "COMPOSITE_TILE_AMBIENT");
				break;
			case Family::kNoTextureAccumulator:
				Define(
					a_defines,
					"WAVE5A_ACCUMULATOR_SHAPE",
					d == 0x204088U ? "2" :
					(d & 0x20000U) != 0 ?
						((d & 0x10000U) != 0 ? "3" : "2") :
						((d & 0x10000U) != 0 ? "1" : "4"));
				return true;
			case Family::kNoTextureFog: {
				const auto shape =
					(d & 0x10000U) != 0 ?
						((d & 0x200U) != 0 ? 6U :
							((d & 0x80U) != 0 ? 5U : 4U)) :
						((d & 0x200U) != 0 ? 3U :
							((d & 0x80U) != 0 ? 2U : 1U));
				Define(
					a_defines,
					"WAVE5A_FOG_SHAPE",
					std::to_string(shape));
				return true;
			}
			case Family::kNoPosition:
			case Family::kNoPositionTexcoord:
				return true;
			}
			DefineBit(a_defines, d, 0x10000U, "TILED_LIGHTS");
			DefineBit(a_defines, d, 0x100000U, "COMPOSITE_ALPHA_ONE");
			return true;
		}

		bool AddImageSpaceDefines(
			ShaderInjectionDefines& a_defines,
			const ShaderFamilyDescriptor& a_descriptor)
		{
			struct HudGlassRoute
			{
				std::string_view nativeName;
				std::string_view nativeClassName;
				std::string_view nativeMacro;
				std::string_view pixelDefine;
				bool clear = false;
			};
			static constexpr std::array hudGlassRoutes{
				HudGlassRoute{
					"ISHUDGlass",
					"BSImagespaceShaderHUDGlass",
					"",
					"IMAGESPACE_HUD_GLASS_BASE"
				},
				HudGlassRoute{
					"ISHUDGlassDS",
					"BSImagespaceShaderHUDGlassDropShadow",
					"DROPSHADOW",
					"IMAGESPACE_HUD_GLASS_DROPSHADOW"
				},
				HudGlassRoute{
					"ISHUDGlassBY",
					"BSImagespaceShaderHUDGlassBlurY",
					"BLURY",
					"IMAGESPACE_HUD_GLASS_BLUR_Y"
				},
				HudGlassRoute{
					"ISHUDGlassBX",
					"BSImagespaceShaderHUDGlassBlurX",
					"BLURX",
					"IMAGESPACE_HUD_GLASS_BLUR_X"
				},
				HudGlassRoute{
					"ISHUDGlassClear",
					"BSImagespaceShaderHUDGlassClear",
					"CLEAR",
					"IMAGESPACE_HUD_GLASS_CLEAR",
					true
				},
				HudGlassRoute{
					"ISHUDGlassCopy",
					"BSImagespaceShaderHUDGlassCopy",
					"COPY",
					"IMAGESPACE_HUD_GLASS_COPY"
				}
			};
			const auto matchesMacros =
				[&a_descriptor](
					std::initializer_list<ShaderInjectionDefineMetadata> a_macros) {
					return a_descriptor.nativeMacros.size() == a_macros.size()
						&& std::ranges::all_of(a_macros, [&](const auto& a_macro) {
							const auto found = a_descriptor.nativeMacros.find(a_macro.name);
							return found != a_descriptor.nativeMacros.end()
								&& found->second == a_macro.value;
						});
				};
			const auto matchesRoute = [&](
				std::string_view a_name,
				std::string_view a_className,
				std::string_view a_sourceGroup,
				std::initializer_list<ShaderInjectionDefineMetadata> a_macros = {}) {
				return a_descriptor.descriptor == 0
					&& a_descriptor.nativeName == a_name
					&& a_descriptor.nativeClassName == a_className
					&& a_descriptor.nativeSourceGroup == a_sourceGroup
					&& matchesMacros(a_macros);
			};
			const auto hudGlassRoute = std::ranges::find_if(
				hudGlassRoutes,
				[&](const HudGlassRoute& a_route) {
					if (a_route.nativeMacro.empty()) {
						return matchesRoute(
							a_route.nativeName,
							a_route.nativeClassName,
							"ISHUDGlass");
					}
					return matchesRoute(
						a_route.nativeName,
						a_route.nativeClassName,
						"ISHUDGlass",
						{ { a_route.nativeMacro, "" } });
				});

			if (a_descriptor.stage == ShaderStage::kCompute) {
				if (matchesRoute(
						"ISSAOCameraZAndMipsCS",
						"BSImagespaceShaderSAOCameraZAndMipsCS",
						"ISSAOCameraZAndMipsCS")) {
					Define(
						a_defines,
						"IMAGESPACE_SSAO_CAMERA_Z_AND_MIPS_CS_SOURCE");
					return true;
				}
				if (matchesRoute(
						"ISSAOMipsCS",
						"BSImagespaceShaderSAOMipsCS",
						"ISSAOMipsCS")) {
					Define(a_defines, "IMAGESPACE_SSAO_MIPS_CS_SOURCE");
					return true;
				}
				if (matchesRoute(
						"ISSAOBlurHCS",
						"BSImagespaceShaderSAOBlurHCS",
						"ISSAOBlurCS",
						{ { "AXIS_H", "" }, { "GRID_SIZE", "972" } })) {
					Define(a_defines, "IMAGESPACE_SSAO_BLUR_CS_SOURCE");
					Define(a_defines, "GRID_SIZE", "972");
					return true;
				}
				if (matchesRoute(
						"ISSAOBlurVCS",
						"BSImagespaceShaderSAOBlurVCS",
						"ISSAOBlurCS",
						{ { "AXIS_V", "" }, { "GRID_SIZE", "552" } })) {
					Define(a_defines, "IMAGESPACE_SSAO_BLUR_CS_SOURCE");
					Define(a_defines, "GRID_SIZE", "552");
					return true;
				}
				if (a_descriptor.descriptor != 2
					&& a_descriptor.descriptor != 5
					&& a_descriptor.descriptor != 6) {
					return false;
				}
				Define(a_defines, "IMAGESPACE_INDEXREBASE_CS_SOURCE");
				Define(a_defines, "IMAGESPACE_INDEXREBASE_DST_ODD",
					(a_descriptor.descriptor & 1U) != 0 ? "1" : "0");
				Define(a_defines, "IMAGESPACE_INDEXREBASE_SRC_ODD",
					(a_descriptor.descriptor & 2U) != 0 ? "1" : "0");
				Define(a_defines, "IMAGESPACE_INDEXREBASE_COUNT_ODD",
					(a_descriptor.descriptor & 4U) != 0 ? "1" : "0");
				return true;
			}
			if (a_descriptor.stage == ShaderStage::kPixel) {
				const auto macro = [&](std::string_view a_name)
					-> std::optional<std::string_view> {
					const auto found =
						a_descriptor.nativeMacros.find(a_name);
					if (found == a_descriptor.nativeMacros.end())
						return std::nullopt;
					return found->second;
				};
				if (hudGlassRoute != hudGlassRoutes.end()) {
					Define(a_defines, hudGlassRoute->pixelDefine);
					return true;
				}
				if (a_descriptor.descriptor == 0
					&& a_descriptor.nativeName == "ISCopy"
					&& a_descriptor.nativeClassName
						== "BSImagespaceShaderCopy"
					&& a_descriptor.nativeSourceGroup == "ISCopy"
					&& a_descriptor.nativeMacros.empty()) {
					Define(a_defines, "IMAGESPACE_COPY_PS_SOURCE");
					return true;
				}
				if (a_descriptor.descriptor == 0
					&& a_descriptor.nativeName == "ISCopyNormals"
					&& a_descriptor.nativeClassName
						== "BSImagespaceShaderCopyNormals"
					&& a_descriptor.nativeSourceGroup == "ISCopy"
					&& matchesMacros({ { "COPY_NORMALS", "" } })) {
					Define(a_defines, "IMAGESPACE_COPY_PS_SOURCE");
					return true;
				}
				if (a_descriptor.descriptor == 0
					&& a_descriptor.nativeName == "ISFullScreenColor"
					&& a_descriptor.nativeClassName
						== "BSImagespaceShaderFullScreenColor"
					&& a_descriptor.nativeSourceGroup == "ISFullScreenColor"
					&& a_descriptor.nativeMacros.empty()) {
					Define(
						a_defines,
						"IMAGESPACE_FULLSCREEN_COLOR_PS_SOURCE");
					return true;
				}
				if (matchesRoute(
						"LensFlare",
						"BSLensFlare",
						"LensFlare")) {
					Define(a_defines, "IMAGESPACE_LENS_FLARE_PS_SOURCE");
					return true;
				}
				if (matchesRoute(
						"LensFlareVis",
						"BSLensFlareVis",
						"LensFlare",
						{ { "VISIBILITY", "" } })) {
					Define(
						a_defines,
						"IMAGESPACE_LENS_FLARE_VISIBILITY_PS_SOURCE");
					Define(a_defines, "VISIBILITY");
					return true;
				}
				if (matchesRoute(
						"ISGammaLUT",
						"BSImagespaceShaderGammaCorrectLUT",
						"ISGamma",
						{ { "LUT", "" } })) {
					Define(a_defines, "IMAGESPACE_GAMMA_LUT_PS_SOURCE");
					return true;
				}
				if (matchesRoute(
						"ISMotionBlur",
						"BSImagespaceShaderMotionBlur",
						"ISMotionBlur")) {
					Define(a_defines, "IMAGESPACE_MOTION_BLUR_PS_SOURCE");
					return true;
				}
				if (matchesRoute(
						"ISRefraction",
						"BSImagespaceShaderRefraction",
						"ISRefraction")) {
					Define(a_defines, "IMAGESPACE_REFRACTION_PS_SOURCE");
					return true;
				}
				std::optional<std::string_view> hdrDefine;
				if (matchesRoute(
						"ISHDRDownSample4",
						"BSImagespaceShaderHDRDownSample4",
						"ISHDR",
						{ { "DOWNSAMPLE", "4" } })) {
					hdrDefine = "DOWNSAMPLE";
				} else if (matchesRoute(
							   "ISHDRTonemapBlendCinematic",
							   "BSImagespaceShaderHDRTonemapBlendCinematic",
							   "ISHDR",
							   { { "BLEND", "4" } })) {
					hdrDefine = "BLEND";
				} else if (matchesRoute(
						"ISHDRDownSample16Lum",
						"BSImagespaceShaderHDRDownSample16Lum",
						"ISHDR",
						{ { "DOWNSAMPLE", "16" }, { "LUM", "" } })) {
					hdrDefine = "LUM";
				} else if (matchesRoute(
							   "ISHDRDownSample4RGB2Lum",
							   "BSImagespaceShaderHDRDownSample4RGB2Lum",
							   "ISHDR",
							   {
								   { "DOWNSAMPLE", "4" },
								   { "LUM", "" },
								   { "RGB2LUM", "" }
							   })) {
					hdrDefine = "RGB2LUM";
				} else if (matchesRoute(
							   "ISHDRDownSample16LightAdapt",
							   "BSImagespaceShaderHDRDownSample16LightAdapt",
							   "ISHDR",
							   {
								   { "DOWNADAPT", "" },
								   { "DOWNSAMPLE", "16" },
								   { "LUM", "" }
							   })) {
					hdrDefine = "";
				}
				if (hdrDefine) {
					Define(a_defines, "IMAGESPACE_HDR_PS_SOURCE");
					if (!hdrDefine->empty())
						Define(a_defines, *hdrDefine);
					return true;
				}
				if (a_descriptor.nativeSourceGroup == "ISBlur") {
					const auto tapCount = macro("TEXTAP");
					const auto brightPass = macro("BRIGHTPASS");
					if (!tapCount
						|| (*tapCount != "3" && *tapCount != "5"
							&& *tapCount != "7" && *tapCount != "9"
							&& *tapCount != "11" && *tapCount != "13"
							&& *tapCount != "15")
						|| (brightPass && !brightPass->empty())
						|| a_descriptor.nativeMacros.size()
							!= (brightPass ? 2U : 1U)) {
						return false;
					}
					const auto expectedOwner =
						std::string("BSImagespaceShader")
						+ (brightPass ? "BrightPass" : "")
						+ "Blur" + std::string(*tapCount);
					const auto nonHdrOwner =
						std::string("BSImageSpaceShaderNonHDRBlur")
						+ std::string(*tapCount);
					const auto nonHdrRttiOwner =
						std::string("BSImagespaceShaderNonHDRBlur")
						+ std::string(*tapCount);
					if (a_descriptor.nativeClassName != expectedOwner
						&& (brightPass
							|| a_descriptor.nativeClassName
								!= nonHdrOwner
							&& a_descriptor.nativeClassName
								!= nonHdrRttiOwner)) {
						return false;
					}
					Define(a_defines, "IMAGESPACE_TAPARRAY_PS_SOURCE");
					Define(
						a_defines,
						"IMAGESPACE_TAPARRAY_TAP_COUNT",
						*tapCount);
					Define(
						a_defines,
						"IMAGESPACE_TAPARRAY_THRESHOLD_SOURCE",
						brightPass ? "1" : "0");
					return true;
				}
				if (a_descriptor.nativeSourceGroup == "ISGamma") {
					const auto resize = macro("RESIZE");
					const auto linearize = macro("LINEARIZE");
					if ((resize && !resize->empty())
						|| (linearize && !linearize->empty())
						|| a_descriptor.nativeMacros.size()
							!= (resize ? 1U : 0U)
								+ (linearize ? 1U : 0U)
						|| (resize && linearize)) {
						return false;
					}
					const std::string_view expectedOwner =
						resize ?
							"BSImagespaceShaderGammaCorrectResize" :
							(linearize ?
								"BSImagespaceShaderGammaLinearize" :
								"BSImagespaceShaderGammaCorrect");
					if (a_descriptor.nativeClassName != expectedOwner)
						return false;
					Define(a_defines, "IMAGESPACE_GAMMA_PS_SOURCE");
					Define(
						a_defines,
						"IMAGESPACE_GAMMA_UV_PRE_TRANSFORM",
						resize ? "1" : "0");
					Define(
						a_defines,
						"IMAGESPACE_GAMMA_CURVE_EXPONENT",
						linearize ?
							"2.200000047683716" :
							"0.4545454680919647");
					return true;
				}
				if (a_descriptor.nativeClassName
						== "BSImagespaceShaderVLSSliceCoord"
					&& a_descriptor.nativeSourceGroup
						== "ISVLS_Coord"
					&& a_descriptor.nativeMacros.empty()) {
					Define(a_defines, "VLS_SLICE_COORD_SOURCE");
					return true;
				}
				if (a_descriptor.nativeClassName
						== "BSImagespaceShaderVLSSliceInterp"
					&& a_descriptor.nativeSourceGroup == "ISVLS"
					&& a_descriptor.nativeMacros
						== ShaderInjectionDefines{
							{ "SLICE_INTERP", "" }
						}) {
					Define(a_defines, "VLS_SLICE_INTERP_SOURCE");
					return true;
				}
			}
			if (a_descriptor.stage == ShaderStage::kVertex) {
				if (matchesRoute(
						"ISSSLRBlurV",
						"BSImagespaceShaderSSLRBlurV",
						"ISSSLRBlur",
						{ { "VERTICAL", "" } })) {
					Define(a_defines, "IMAGESPACE_SSLR_BLUR_V_VS_SOURCE");
					return true;
				}
				if (matchesRoute(
						"LensFlare",
						"BSLensFlare",
						"LensFlare")) {
					Define(a_defines, "IMAGESPACE_LENS_FLARE_VS_SOURCE");
					return true;
				}
				if (hudGlassRoute != hudGlassRoutes.end()) {
					Define(a_defines, "IMAGESPACE_HUD_GLASS_VS_SOURCE");
					if (hudGlassRoute->clear)
						Define(a_defines, "IMAGESPACE_HUD_GLASS_CLEAR");
					return true;
				}
				if (a_descriptor.nativeSourceGroup == "ISHUDGlass"
					&& a_descriptor.nativeMacros
						== ShaderInjectionDefines{
							{ "MARKERS", "" }
						}) {
					Define(a_defines, "IMAGESPACE_XYQUAD_VS_SOURCE");
					Define(a_defines, "IMAGESPACE_XYQUAD_PACKED");
					return true;
				}
				if (a_descriptor.nativeSourceGroup == "LensFlare"
					&& a_descriptor.nativeMacros
						== ShaderInjectionDefines{
							{ "VISIBILITY", "" }
						}) {
					Define(a_defines, "IMAGESPACE_XYQUAD_VS_SOURCE");
					return true;
				}
				if (a_descriptor.nativeSourceGroup == "ISFXAA"
					&& a_descriptor.nativeMacros.empty()) {
					Define(a_defines, "IMAGESPACE_PASSTHROUGH_VS_SOURCE");
					Define(a_defines, "IMAGESPACE_PASSTHROUGH_TEXCOORD1");
					return true;
				}
				static constexpr std::array passthroughOwners{
					"BSImagespaceShaderGammaCorrectLUT",
					"BSImagespaceShaderCopy",
					"BSImagespaceShaderCopyVisAlpha",
					"BSImagespaceShaderRefraction",
					"BSImagespaceShaderDoubleVision",
					"BSImagespaceShaderDepthOfField",
					"BSImagespaceShaderBokehDepthOfFieldPass2",
					"BSImagespaceShaderBokehDepthOfFieldPass3",
					"BSImagespaceShaderDistantBlur",
					"BSImagespaceShaderFullScreenColor",
					"BSImagespaceShaderRadialBlur",
					"BSImagespaceShaderRadialBlurMedium",
					"BSImagespaceShaderRadialBlurHigh",
					"BSImagespaceShaderGreyScale",
					"BSImagespaceShaderDownsampleDepth",
					"BSImagespaceShaderCopyStencil",
					"BSImagespaceShaderCopyWaterMask",
					"BSImagespaceShaderCopyShadowMapToArray",
					"BSImagespaceShaderCopyNormals",
					"BSImagespaceShaderHDRTonemapBlendCinematic",
					"BSImagespaceShaderHDRTonemapBlendCinematicFade",
					"BSImagespaceShaderHDRDownSample16",
					"BSImagespaceShaderHDRDownSample4",
					"BSImagespaceShaderHDRDownSample16Lum",
					"BSImagespaceShaderHDRDownSample4RGB2Lum",
					"BSImagespaceShaderHDRDownSample4LightAdapt",
					"BSImagespaceShaderHDRDownSample4LumClamp",
					"BSImagespaceShaderHDRDownSample16LightAdapt",
					"BSImagespaceShaderHDRDownSample16LumClamp",
					"BSImagespaceShaderBlur3",
					"BSImagespaceShaderBlur5",
					"BSImagespaceShaderBlur7",
					"BSImagespaceShaderBlur9",
					"BSImagespaceShaderBlur11",
					"BSImagespaceShaderBlur13",
					"BSImagespaceShaderBlur15",
					"BSImagespaceShaderNonHDRBlur3",
					"BSImagespaceShaderNonHDRBlur5",
					"BSImagespaceShaderNonHDRBlur7",
					"BSImagespaceShaderNonHDRBlur9",
					"BSImagespaceShaderNonHDRBlur11",
					"BSImagespaceShaderNonHDRBlur13",
					"BSImagespaceShaderNonHDRBlur15",
					"BSImageSpaceShaderNonHDRBlur3",
					"BSImageSpaceShaderNonHDRBlur5",
					"BSImageSpaceShaderNonHDRBlur7",
					"BSImageSpaceShaderNonHDRBlur9",
					"BSImageSpaceShaderNonHDRBlur11",
					"BSImageSpaceShaderNonHDRBlur13",
					"BSImageSpaceShaderNonHDRBlur15",
					"BSImagespaceShaderBrightPassBlur3",
					"BSImagespaceShaderBrightPassBlur5",
					"BSImagespaceShaderBrightPassBlur7",
					"BSImagespaceShaderBrightPassBlur9",
					"BSImagespaceShaderBrightPassBlur11",
					"BSImagespaceShaderBrightPassBlur13",
					"BSImagespaceShaderBrightPassBlur15",
					"BSImagespaceShaderWaterDisplacementClearSimulation",
					"BSImagespaceShaderWaterDisplacementTexOffset",
					"BSImagespaceShaderWaterDisplacementRainRipple",
					"BSImagespaceShaderWaterBlendHeightmaps",
					"BSImagespaceShaderWaterDisplacementNormals",
					"BSISWaterDisplacementClearSimulation",
					"BSISWaterDisplacementTexOffset",
					"BSISWaterDisplacementRainRipple",
					"BSISWaterBlendHeightmaps",
					"BSISWaterDisplacementNormals",
					"BSImagespaceShaderLocalMap",
					"BSImagespaceShaderLocalMapCompanion",
					"BSImagespaceShaderAlphaBlend",
					"BSImagespaceShaderPipboyScreen",
					"BSImagespaceShaderVatsTargetDebug",
					"BSImagespaceShaderVatsTarget",
					"BSImagespaceShaderModMenuEffect",
					"BSImagespaceShaderModMenuGlowComposite",
					"BSImagespaceShaderAmbientOcclusion",
					"BSImagespaceShaderAmbientOcclusionBlur",
					"BSImagespaceShaderVLSSpotLight",
					"BSImagespaceShaderVLSApplication",
					"BSImagespaceShaderVLSComposite",
					"BSImagespaceShaderVLSSliceCoord",
					"BSImagespaceShaderVLSSliceInterp",
					"BSImagespaceShaderVLSSliceStencil",
					"BSImagespaceShaderVLSSliceScatterRay",
					"BSImagespaceShaderVLSSliceScatterInterp",
					"BSImagespaceShaderSAOCameraZ",
					"BSImagespaceShaderSAOMinify",
					"BSImagespaceShaderSAORawAO",
					"BSImagespaceShaderSAOBlurH",
					"BSImagespaceShaderSAOBlurV",
					"BSImagespaceShaderSAORawAOEditor",
					"BSImagespaceShaderMotionBlur",
					"BSImagespaceShaderTemporalAA",
					"BSImagespaceShaderTemporalAAPipboy",
					"BSImagespaceShaderTemporalAAPowerArmorPipboy",
					"BSImagespaceShaderGammaCorrect",
					"BSImagespaceShaderGammaLinearize",
					"BSImagespaceShaderGammaCorrectResize",
					"BSImagespaceShaderSunbeams",
					"BSImagespaceShaderSSLRRaytracing",
					"BSImagespaceShaderSSLRPrepass",
					"BSImagespaceShaderDepthOfFieldFogged",
					"BSImagespaceShaderDepthOfFieldSplitScreen",
					"BSImagespaceShaderDistantBlurFogged",
					"BSImagespaceShaderUpsampleDynamicResolution"
				};
				if (std::ranges::find(
						passthroughOwners,
						a_descriptor.nativeClassName)
					!= passthroughOwners.end()) {
					Define(a_defines, "IMAGESPACE_PASSTHROUGH_VS_SOURCE");
					return true;
				}
				return false;
			}
			return false;
		}

		bool AddFamilyDefines(
			ShaderInjectionDefines& a_defines,
			const ShaderFamilyDescriptor& a_descriptor)
		{
			const auto d = a_descriptor.descriptor;
			switch (a_descriptor.target) {
			case ShaderInjectionTarget::kDeferredPrepass:
				if (!AddPrepassDefines(a_defines, a_descriptor))
					return false;
				if (a_descriptor.stage == ShaderStage::kPixel &&
					a_descriptor.forceEarlyDepthStencil) {
					Define(a_defines, "EARLYDEPTH");
				}
				return true;
			case ShaderInjectionTarget::kUtility:
				return AddUtilityDefines(a_defines, d);
			case ShaderInjectionTarget::kParticle:
				return AddParticleDefines(a_defines, a_descriptor.stage, d);
			case ShaderInjectionTarget::kEffect:
				return AddEffectDefines(a_defines, d);
			case ShaderInjectionTarget::kBloodSplatter:
				if (d > 1)
					return false;
				Define(a_defines, d == 0 ? "SPLATTER" : "FLARE");
				return true;
			case ShaderInjectionTarget::kDistantTree:
				if (d > 1)
					return false;
				DefineBit(a_defines, d, 1, "RENDER_DEPTH");
				return true;
			case ShaderInjectionTarget::kFaceCustomization:
				return d == 0
					&& (a_descriptor.stage == ShaderStage::kVertex
						|| a_descriptor.stage == ShaderStage::kPixel);
			case ShaderInjectionTarget::kImageSpace:
				return AddImageSpaceDefines(a_defines, a_descriptor);
			case ShaderInjectionTarget::kBsSky:
				return AddSkyDefines(a_defines, d);
			case ShaderInjectionTarget::kBsWater:
				return AddWaterDefines(a_defines, d);
			case ShaderInjectionTarget::kBsLighting:
				return AddLightingDefines(
					a_defines, a_descriptor.stage, d);
			case ShaderInjectionTarget::kBsdfLight:
				return AddBsdfLightDefines(
					a_defines, a_descriptor.stage, d);
			case ShaderInjectionTarget::kBsdfComposite:
				return AddBsdfCompositeDefines(
					a_defines, a_descriptor.stage, d);
			case ShaderInjectionTarget::kDfTiledLighting:
				if (a_descriptor.stage != ShaderStage::kCompute ||
					d > 3)
					return false;
				if (d == 3)
					Define(a_defines, "DFTILEDLIGHTING_TILE_CULL_GROUP_DIM", "10");
				Define(
					a_defines,
					"DFTILEDLIGHTING_VARIANT",
					std::to_string(d));
				return true;
			default:
				return false;
			}
		}

		void AddStageSourceDefine(
			ShaderInjectionDefines& a_defines,
			const ShaderFamilyDescriptor& a_descriptor)
		{
			std::string_view define;
			switch (a_descriptor.target) {
			case ShaderInjectionTarget::kDeferredPrepass:
				define = a_descriptor.stage == ShaderStage::kVertex ?
					"BSDFPREPASS_VS_SOURCE" : "BSDFPREPASS_PS_SOURCE";
				break;
			case ShaderInjectionTarget::kUtility:
				define = a_descriptor.stage == ShaderStage::kVertex ?
					"BSUTILITY_VS_SOURCE" : "BSUTILITY_PS_SOURCE";
				break;
			case ShaderInjectionTarget::kParticle:
				define = a_descriptor.stage == ShaderStage::kVertex ?
					"PARTICLE_VS_SOURCE" : "PARTICLE_PS_SOURCE";
				break;
			case ShaderInjectionTarget::kEffect:
				define = a_descriptor.stage == ShaderStage::kVertex ?
					"BSEFFECT_VS_SOURCE" : "BSEFFECT_PS_SOURCE";
				break;
			case ShaderInjectionTarget::kBloodSplatter:
				define = a_descriptor.stage == ShaderStage::kVertex ?
					"BSBLOODSPLATTER_VS_SOURCE" : "BSBLOODSPLATTER_PS_SOURCE";
				break;
			case ShaderInjectionTarget::kDistantTree:
				define = a_descriptor.stage == ShaderStage::kVertex ?
					"BSDISTANTTREE_VS_SOURCE" : "BSDISTANTTREE_PS_SOURCE";
				break;
			case ShaderInjectionTarget::kFaceCustomization:
				define = a_descriptor.stage == ShaderStage::kVertex ?
					"BSFACECUSTOMIZATION_VS_SOURCE" :
					"BSFACECUSTOMIZATION_PS_SOURCE";
				break;
			case ShaderInjectionTarget::kBsSky:
				define = a_descriptor.stage == ShaderStage::kVertex ?
					"BSSKY_VERTEX_SHADER" : "BSSKY_PIXEL_SHADER";
				break;
			case ShaderInjectionTarget::kBsWater:
				define = a_descriptor.stage == ShaderStage::kVertex ?
					"BSWATER_VERTEX_SHADER" : "BSWATER_PIXEL_SHADER";
				break;
			default:
				break;
			}
			if (!define.empty())
				Define(a_defines, define);
		}
	}

	std::optional<ShaderVariantCompilationDescriptor>
		BuildShaderFamilyCompilationDescriptor(
			const ShaderFamilyDescriptor& a_descriptor)
	{
		const auto* target = GetShaderInjectionTarget(a_descriptor.target);
		if (!target || a_descriptor.stage == ShaderStage::kCount)
			return std::nullopt;

		ShaderVariantCompilationDescriptor result;
		result.sourcePath = target->sourcePath;
		result.entryPoint = target->entryPoint;
		result.profile = ProfileForStage(a_descriptor.stage);
		if (result.profile.empty())
			return std::nullopt;

		if (!AddFamilyDefines(result.defines, a_descriptor)) {
			L->warn(
				"Unsupported native shader descriptor: target='{}', stage={}, descriptor={:#010x}, name='{}', source='{}', class='{}', macros='{}'",
				target->name,
				static_cast<unsigned>(a_descriptor.stage),
				a_descriptor.descriptor,
				a_descriptor.nativeName,
				a_descriptor.nativeSourceGroup,
				a_descriptor.nativeClassName,
				DescribeShaderInjectionDefines(a_descriptor.nativeMacros));
			return std::nullopt;
		}
		AddStageSourceDefine(result.defines, a_descriptor);
		return result;
	}
}
