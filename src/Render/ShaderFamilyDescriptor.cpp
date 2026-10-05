#include "Render/ShaderFamilyDescriptor.h"

#include "Log.h"

#include <algorithm>
#include <array>
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

		bool AddPrepassDefines(
			ShaderInjectionDefines& a_defines,
			const ShaderFamilyDescriptor& a_family)
		{
			auto a_descriptor = a_family.descriptor;
			const bool tessellatedVertex =
				a_family.stage == ShaderStage::kVertex && (a_descriptor & ((1U << 19) | (1U << 20))) != 0;
			// FO4 vertex normalization is fitted to the pinned AE 1.11.240 route population.
			if (a_family.stage == ShaderStage::kVertex) {
				if (!tessellatedVertex && (a_descriptor & 0x220U) == 0)
					a_descriptor &= ~(1U << 25);
				if ((a_descriptor & 0x2000U) != 0)
					a_descriptor &= ~0x18U;
				else if ((a_descriptor & 0x2U) != 0 && (a_descriptor & 0x4240U) != 0)
					a_descriptor |= 0x18U;
				if (tessellatedVertex)
					a_descriptor &= ~(1U << 10);
			}
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
			if (grassBit) {
				Define(a_defines, "GRASS");
				if (a_family.stage == ShaderStage::kVertex)
					Define(a_defines, "MAX_ACTOR_VEGETATION_COLLISION", "4");
			}
			DefineBit(a_defines, a_descriptor, 1U << 8, "ALPHA_TEST");
			if ((a_descriptor & (1U << 9)) != 0 || (a_family.stage == ShaderStage::kVertex && !tessellatedVertex && (a_descriptor & (1U << 25)) != 0)) {
				Define(a_defines, "LOD_LANDSCAPE");
			}
			if ((a_descriptor & (1U << 10)) != 0 && (a_family.stage == ShaderStage::kVertex || !grassBit))
				Define(a_defines, (a_descriptor & (1U << 23)) != 0 ?
									  "SPLINE" :
									  "TREE_ANIM");
			DefineBit(a_defines, a_descriptor, 1U << 12, "CHARACTER_LIGHT_MASK");
			DefineBit(a_defines, a_descriptor, 1U << 13, "MODELSPACENORMALS");
			DefineBit(a_defines, a_descriptor, 1U << 14, "GLOWMAP");
			DefineBit(a_defines, a_descriptor, 1U << 15, "BLEND");
			if ((a_descriptor & (1U << 11)) != 0)
				Define(a_defines, (a_descriptor & (1U << 16)) != 0 ?
									  "SKEW_SPECULAR_ALPHA" :
									  "LOD_OBJECT_INSTANCED");
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
			if (characterLight && (lightKind == 1U || (d & 0x400000U) != 0))
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
					combinedIgnoreMode && (lightKind == 4U || (lightKind == 8U && (d & 0x1000U) == 0));
				if (!ignoreRimSuppressed)
					DefineBit(a_defines, d, 0x8000U, "IGNORERIM");
			}
			Define(a_defines, "RGBSPEC");
			if (lightKind == 1U || lightKind == 2U)
				Define(a_defines, "DIRECTIONAL");
			if (lightKind == 2U || lightKind == 8U ||
				lightKind == 16U || lightKind == 32U)
				Define(a_defines, "SHADOW");
			if (family == Family::kShadowOnly || family == Family::kShadowOnlyBlendSplit)
				Define(a_defines, "SHADOW_ONLY");
			DefineBit(a_defines, d, 0x80000U, "FILTER_PCF1");
			DefineBit(a_defines, d, 0x100000U, "FILTER_PCF9");
			DefineBit(a_defines, d, 0x200000U, "FILTER_POISSON");
			if ((d & 0x4780103U) == 0x400002U)
				Define(a_defines, "FILTER_PCSS");
			DefineBit(a_defines, d, 0x800000U, "FILTER_PCSSPOISSON");
			if ((family == Family::kDirSplits2 || family == Family::kDirSplits3 || family == Family::kShadowOnlyBlendSplit) && (d & 0x1000000U) != 0)
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
			else if (family == Family::kDirSplits2 || family == Family::kShadowOnly)
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

			if ((d & 0x1000U) != 0) {
				// Decal bits the shipped decal composites carry; anything else has no native blob.
				constexpr std::uint32_t kDecalBits =
					0x1000U | 0x20U | 0x2000U | 0x10000U | 0x20000U |
					0x40000U | 0x80000U | 0x100000U;
				if ((d & ~kDecalBits) != 0)
					return false;
				const bool parallax = (d & 0x40000U) != 0;
				// POM shadows only select the contact root when POM itself is on.
				const bool recordNormal =
					(d & 0x2020U) != 0 ||
					(parallax && (d & 0x80000U) == 0);
				const auto shape = std::to_string(
					1U + ((d & 0x20000U) != 0 ? 1U : 0U) + (parallax ? 2U : 0U));
				if (recordNormal) {
					Define(a_defines, "BSDFCOMPOSITE_PS_SSS_MRT_RECORD_NORMAL");
					Define(a_defines, "WAVE5B_SSS_RECORD_NORMAL_SHAPE", shape);
				} else {
					Define(a_defines, "BSDFCOMPOSITE_PS_SSS_MRT_SURFACE_CONTACT");
					Define(a_defines, "WAVE5B_SSS_SURFACE_CONTACT_SHAPE", shape);
				}
				return true;
			}

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
				else if ((d & ~0x70305U) == 0x820U)
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
				if ((d & 0x10000U) == 0)
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
					(d & 0x80U) != 0 ?
						((d & 0x10000U) != 0 ? "3" : "2") :
						((d & 0x10000U) != 0 ? "1" : "4"));
				return true;
			case Family::kNoTextureFog:
				{
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
			case ShaderInjectionTarget::kEffect:
				return AddEffectDefines(a_defines, d);
			case ShaderInjectionTarget::kDistantTree:
				if (d > 1)
					return false;
				DefineBit(a_defines, d, 1, "RENDER_DEPTH");
				return true;
			case ShaderInjectionTarget::kBsWater:
				return AddWaterDefines(a_defines, d);
			case ShaderInjectionTarget::kBsdfLight:
				return AddBsdfLightDefines(
					a_defines, a_descriptor.stage, d);
			case ShaderInjectionTarget::kBsdfComposite:
				return AddBsdfCompositeDefines(
					a_defines, a_descriptor.stage, d);
			case ShaderInjectionTarget::kDfTiledLighting:
				if (a_descriptor.stage != ShaderStage::kCompute ||
					d > 0x12)
					return false;
				// Keys 3..18 are the tile-cull entry at group dims 10..25.
				if (d >= 3)
					Define(a_defines, "DFTILEDLIGHTING_TILE_CULL_GROUP_DIM", std::to_string(d + 7));
				Define(
					a_defines,
					"DFTILEDLIGHTING_VARIANT",
					std::to_string(std::min(d, 3U)));
				return true;
			case ShaderInjectionTarget::kImageSpace:
				a_defines = a_descriptor.nativeMacros;
				return true;
			default:
				return true;
			}
		}

		void AddStageSourceDefine(
			ShaderInjectionDefines& a_defines,
			const ShaderFamilyDescriptor& a_descriptor)
		{
			switch (a_descriptor.stage) {
			case ShaderStage::kVertex:
				Define(a_defines, "VSHADER");
				break;
			case ShaderStage::kPixel:
				Define(a_defines, "PSHADER");
				break;
			case ShaderStage::kCompute:
				Define(a_defines, "CSHADER");
				break;
			default:
				break;
			}
			std::string_view define;
			switch (a_descriptor.target) {
			case ShaderInjectionTarget::kDeferredPrepass:
				define = a_descriptor.stage == ShaderStage::kVertex ?
				             "BSDFPREPASS_VS_SOURCE" :
				             "BSDFPREPASS_PS_SOURCE";
				break;
			case ShaderInjectionTarget::kEffect:
				define = a_descriptor.stage == ShaderStage::kVertex ?
				             "BSEFFECT_VS_SOURCE" :
				             "BSEFFECT_PS_SOURCE";
				break;
			case ShaderInjectionTarget::kDistantTree:
				define = a_descriptor.stage == ShaderStage::kVertex ?
				             "BSDISTANTTREE_VS_SOURCE" :
				             "BSDISTANTTREE_PS_SOURCE";
				break;
			case ShaderInjectionTarget::kBsWater:
				define = a_descriptor.stage == ShaderStage::kVertex ?
				             "BSWATER_VERTEX_SHADER" :
				             "BSWATER_PIXEL_SHADER";
				break;
			default:
				break;
			}
			if (!define.empty())
				Define(a_defines, define);
		}
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

	std::filesystem::path GetShaderPath(std::string_view a_nativeName)
	{
		return std::filesystem::path(a_nativeName).concat(".hlsl");
	}

	bool IsShaderSourceAvailable(
		const std::filesystem::path& a_shaderRoot, std::string_view a_nativeName)
	{
		std::error_code error;
		return !a_nativeName.empty() &&
		       std::filesystem::is_regular_file(a_shaderRoot / GetShaderPath(a_nativeName), error);
	}

	std::optional<ShaderVariantCompilationDescriptor>
	BuildShaderFamilyCompilationDescriptor(
		const ShaderFamilyDescriptor& a_descriptor)
	{
		const auto* target = GetShaderInjectionTarget(a_descriptor.target);
		if (!target || a_descriptor.nativeName.empty() || a_descriptor.stage == ShaderStage::kCount ||
			(target->supportedStages & ShaderStageBit(a_descriptor.stage)) == 0)
			return std::nullopt;

		ShaderVariantCompilationDescriptor result;
		result.sourcePath = GetShaderPath(a_descriptor.nativeName).wstring();
		result.entryPoint = "main";
		result.profile = ProfileForStage(a_descriptor.stage);
		if (result.profile.empty())
			return std::nullopt;

		if (!AddFamilyDefines(result.defines, a_descriptor)) {
			L->debug(
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
