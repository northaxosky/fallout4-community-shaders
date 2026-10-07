#include "Render/OccluderPasses.h"

#include "Log.h"

#include <cstdint>
#include <format>
#include <optional>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.skylighting.capture");

		using ShaderFlag = RE::BSShaderProperty::EShaderPropertyFlag;

		// BSUtilityShader technique bits the stock pass builder composes (vslot 44, AE 0x217B8F0).
		constexpr std::uint32_t kUtilityLandscape = 0x20;
		constexpr std::uint32_t kUtilityAlphaTest = 0x80;
		constexpr std::uint32_t kUtilityShadowMap = 0x4000;
		constexpr std::uint32_t kUtilityShadowMapClamped = 0x8000;
		constexpr std::uint32_t kUtilityGeometryType15 = 0x200040;
		constexpr std::uint32_t kUtilityMergeInstanced = 0xA00000;
		constexpr std::uint32_t kUtilityAdditionalAlphaMask = 0x10000000;

		// BSGeometry::type that the stock builder gives extra technique bits.
		constexpr std::uint8_t kGeometryType15 = 15;
		constexpr std::uint16_t kAlphaPropertyBlend = 0x1;
		constexpr std::uint16_t kAlphaPropertyTest = 0x200;
		constexpr std::ptrdiff_t kEffectDataPayloadOffset = 0x20;
		constexpr std::uint8_t kTreeAnimPassType = 11;
		constexpr std::uint8_t kVatsPassType = 21;
		constexpr std::uint64_t kGeometryMeshLodFlag = 0x1000;
		constexpr std::int8_t kDefaultLodMode = 3;

		[[nodiscard]] std::optional<OccluderReject> RejectOccluder(
			const RE::BSLightingShaderProperty& a_property,
			const RE::BSGeometry& a_geometry,
			const OccluderPolicy& a_policy)
		{
			// FO4: the engine rejects skinned geometry before this builder runs.
			if (a_property.flags.any(ShaderFlag::kSkinned) && a_property.flags.none(ShaderFlag::kTreeAnim))
				return OccluderReject::kSkinned;

			const bool validOccluder = a_property.flags.any(ShaderFlag::kZBufferWrite) &&
			                           a_property.flags.none(
										   ShaderFlag::kRefraction,
										   ShaderFlag::kTempRefraction,
										   ShaderFlag::kLODLandscape,
										   ShaderFlag::kEyeReflect,
										   ShaderFlag::kDecal,
										   ShaderFlag::kDynamicDecal);
			if (!validOccluder)
				return OccluderReject::kFlags;
			if (!(a_geometry.worldBound.fRadius > a_policy.minOccluderRadius))
				return OccluderReject::kRadius;

			// Only occluders above a probe lie on its ray to the sky
			if (a_geometry.worldBound.center.z + a_geometry.worldBound.fRadius < a_policy.probeGridBottomZ - a_policy.belowGridMargin)
				return OccluderReject::kBelowGrid;

			if (a_geometry.userData) {
				RE::BSFadeNode* fadeNode = nullptr;

				RE::NiNode* parent = a_geometry.parent;
				while (parent && !fadeNode) {
					fadeNode = parent->IsFadeNode();
					parent = parent->parent;
				}

				if (fadeNode) {
					// The key is interned once; building it per call costs a string-pool lookup.
					static const RE::BSFixedString bsxKey{ "BSX" };
					if (const auto* bsx = fadeNode->GetExtraData<RE::BSXFlags>(bsxKey)) {
						using Flag = RE::BSXFlags::Flag;
						if (bsx->GetFlags().any(
								Flag::kRagdoll,
								Flag::kDynamic,
								Flag::kAddon,
								Flag::kNeedsTransformUpdate,
								Flag::kMagicShaderParticles,
								Flag::kLights,
								Flag::kBreakable,
								Flag::kSearchedBreakable))
							return OccluderReject::kBsx;
					}
				}
			}
			return std::nullopt;
		}

		[[nodiscard]] const RE::NiAlphaProperty* GetAlphaProperty(const RE::BSGeometry& a_geometry) noexcept
		{
			return static_cast<const RE::NiAlphaProperty*>(a_geometry.properties[0].get());
		}

		// Names the stock builder condition that upstream's predicate does not share.
		[[nodiscard]] OwnBuildReason ClassifyStockRejection(
			const RE::BSLightingShaderProperty& a_property,
			const RE::BSGeometry& a_geometry) noexcept
		{
			if (a_property.flags.any(ShaderFlag::kMultiTextureLandscape))
				return OwnBuildReason::kLandscape;
			if (const auto* alpha = GetAlphaProperty(a_geometry); alpha && (alpha->flags.flags & kAlphaPropertyBlend))
				return OwnBuildReason::kAlphaBlended;
			if (a_property.flags.none(ShaderFlag::kCastShadows))
				return OwnBuildReason::kNotCasting;
			return OwnBuildReason::kOther;
		}

		// The stock builder's technique for the occlusion mode, from the same property and geometry inputs.
		[[nodiscard]] std::uint32_t OcclusionTechnique(
			const RE::BSLightingShaderProperty& a_property,
			RE::BSGeometry& a_geometry)
		{
			auto technique = a_property.DetermineUtilityShaderDecl();
			// Landscape decl carries a land bit whose utility permutation has no code behind it.
			if (a_property.flags.any(ShaderFlag::kMultiTextureLandscape))
				technique &= ~kUtilityLandscape;

			if (a_geometry.type == kGeometryType15) {
				technique |= kUtilityGeometryType15;
				if (a_property.flags.any(ShaderFlag::kAlphaTest))
					technique |= kUtilityAlphaTest;
			} else if (a_geometry.IsBSMergeInstancedTriShape()) {
				technique |= kUtilityMergeInstanced;
			}

			technique |= kUtilityShadowMap | kUtilityShadowMapClamped;
			if (const auto* alpha = GetAlphaProperty(a_geometry); alpha && (alpha->flags.flags & kAlphaPropertyTest))
				technique |= kUtilityAlphaTest;
			if (a_property.effectData && *reinterpret_cast<void* const*>(reinterpret_cast<const std::byte*>(a_property.effectData) + kEffectDataPayloadOffset))
				technique |= kUtilityAdditionalAlphaMask;
			return technique;
		}

		// Mirrors the stock else-branch for pass lists the stock builder rejects; reuses the list once built.
		RE::BSShaderProperty::RenderPassArray* BuildOcclusionPasses(
			RE::BSLightingShaderProperty& a_property,
			RE::BSGeometry& a_geometry,
			const RE::BSShaderAccumulator& a_accumulator,
			bool& a_created)
		{
			auto* shader = RE::BSUtilityShader::GetSingleton();
			if (!shader || a_accumulator.depthPassIndex >= std::size(a_property.depthMapRenderPassListA))
				return nullptr;
			auto& list = a_property.depthMapRenderPassListA[a_accumulator.depthPassIndex];
			const auto technique = OcclusionTechnique(a_property, a_geometry);

			// FO4: stock reads the fade node's mesh LOD level only when this geometry flag is set.
			auto lodMode = kDefaultLodMode;
			if ((a_geometry.GetFlags() & kGeometryMeshLodFlag) && a_property.fadeNode)
				lodMode = a_property.fadeNode->currentMeshLODLevel;

			if (list.passList) {
				if (list.passList->passEnum != technique)
					return nullptr;
				list.passList->lodMode = lodMode;
				return &list;
			}

			auto* pass = list.Add(shader, &a_property, &a_geometry, technique, 0, nullptr, nullptr, nullptr, nullptr);
			if (!pass)
				return nullptr;
			// The pass is new for this list, so it holds no command buffer to release first.
			pass->commandBuffer = shader->CreateCommandBuffer(pass);
			pass->lodMode = lodMode;
			if (a_property.flags.any(ShaderFlag::kTreeAnim))
				pass->passType = kTreeAnimPassType;
			else if (a_property.flags.any(ShaderFlag::kVATSTarget))
				pass->passType = kVatsPassType;
			a_created = true;
			return &list;
		}

		struct LightingShaderProperty_OcclusionPasses
		{
			static RE::BSShaderProperty::RenderPassArray* thunk(
				RE::BSLightingShaderProperty* a_property,
				RE::BSGeometry* a_geometry,
				std::uint32_t a_renderMode,
				RE::BSShaderAccumulator* a_accumulator)
			{
				const auto* capture = GetActiveCapture(a_accumulator);
				if (!capture)
					return func(a_property, a_geometry, a_renderMode, a_accumulator);

				auto& stats = GetOccluderStats();
				auto& timings = GetCaptureTimings();
				const auto begin = ReadTicks();
				const auto reject = RejectOccluder(*a_property, *a_geometry, capture->occluders);
				const auto predicateEnd = ReadTicks();
				timings.hookPredicate.fetch_add(predicateEnd - begin, std::memory_order_relaxed);
				if (reject) {
					stats.rejected[static_cast<std::size_t>(*reject)].fetch_add(1, std::memory_order_relaxed);
					return nullptr;
				}
				stats.accepted.fetch_add(1, std::memory_order_relaxed);

				auto* passes = func(a_property, a_geometry, a_renderMode, a_accumulator);
				const auto stockEnd = ReadTicks();
				timings.hookStock.fetch_add(stockEnd - predicateEnd, std::memory_order_relaxed);
				if (passes) {
					stats.delegated.fetch_add(1, std::memory_order_relaxed);
					return passes;
				}

				bool created = false;
				passes = BuildOcclusionPasses(*a_property, *a_geometry, *a_accumulator, created);
				timings.hookOwn.fetch_add(ReadTicks() - stockEnd, std::memory_order_relaxed);
				if (!passes) {
					stats.stockOnly.fetch_add(1, std::memory_order_relaxed);
					return nullptr;
				}
				stats.ownBuilt[static_cast<std::size_t>(ClassifyStockRejection(*a_property, *a_geometry))].fetch_add(1, std::memory_order_relaxed);
				if (created)
					stats.ownNew.fetch_add(1, std::memory_order_relaxed);
				return passes;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	HookInstall InstallOccluderPassHook()
	{
		HookInstall install{ .name = "BSLightingShaderProperty vslot 44 (GetRenderPasses_ShadowMapOrMask)" };
		stl::write_vfunc<44, LightingShaderProperty_OcclusionPasses>(RE::VTABLE::BSLightingShaderProperty[0]);
		install.installed = LightingShaderProperty_OcclusionPasses::func.address() != 0;
		install.detail = std::format("stock builder {:#x}", LightingShaderProperty_OcclusionPasses::func.address());
		if (!install.installed)
			L->error("Hook {} unavailable: the stock builder did not resolve", install.name);
		return install;
	}
}
