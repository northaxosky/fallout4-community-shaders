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

		using UtilityFlag = RE::BSUtilityShader::Flags;

		// BSGeometry::type that the stock builder gives extra technique bits.
		constexpr std::uint8_t kGeometryType15 = 15;
		constexpr std::uint8_t kTreeAnimPassType = 11;
		constexpr std::uint8_t kVatsPassType = 21;
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

		// Names the stock builder condition that upstream's predicate does not share.
		[[nodiscard]] OwnBuildReason ClassifyStockRejection(
			const RE::BSLightingShaderProperty& a_property,
			const RE::BSGeometry& a_geometry) noexcept
		{
			if (a_property.flags.any(ShaderFlag::kMultiTextureLandscape))
				return OwnBuildReason::kLandscape;
			if (const auto* alpha = a_geometry.GetAlphaProperty(); alpha && alpha->GetAlphaBlending())
				return OwnBuildReason::kAlphaBlended;
			if (a_property.flags.none(ShaderFlag::kCastShadows))
				return OwnBuildReason::kNotCasting;
			return OwnBuildReason::kOther;
		}

		// The stock technique for the occlusion mode, from the same inputs.
		[[nodiscard]] std::uint32_t OcclusionTechnique(
			const RE::BSLightingShaderProperty& a_property,
			RE::BSGeometry& a_geometry)
		{
			REX::TEnumSet<UtilityFlag, std::uint32_t> technique{ static_cast<UtilityFlag>(a_property.DetermineUtilityShaderDecl()) };
			// Landscape decl carries a land bit whose utility permutation has no code.
			if (a_property.flags.any(ShaderFlag::kMultiTextureLandscape))
				technique.reset(UtilityFlag::kLandscape);

			if (a_geometry.type == kGeometryType15) {
				technique.set(UtilityFlag::kCombined, UtilityFlag::kEye);
				if (a_property.flags.any(ShaderFlag::kAlphaTest))
					technique.set(UtilityFlag::kAlphaTest);
			} else if (a_geometry.IsBSMergeInstancedTriShape()) {
				technique.set(UtilityFlag::kInstanced, UtilityFlag::kCombined);
			}

			technique.set(UtilityFlag::kRenderShadowMap, UtilityFlag::kRenderShadowMapClamped);
			if (const auto* alpha = a_geometry.GetAlphaProperty(); alpha && alpha->GetAlphaTesting())
				technique.set(UtilityFlag::kAlphaTest);
			if (a_property.effectData && a_property.effectData->blockOutTexture)
				technique.set(UtilityFlag::kAdditionalAlphaMask);
			return technique.underlying();
		}

		// Mirrors the stock else-branch for rejected pass lists; reuses the built list.
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

			// FO4: stock reads the mesh LOD level only when this geometry flag is set.
			auto lodMode = kDefaultLodMode;
			if (a_geometry.HasFlag(RE::NiAVObject::Flag::kMeshLOD) && a_property.fadeNode)
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
			// A new pass has no command buffer to release first.
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
				const auto begin = CaptureClock::now();
				const auto reject = RejectOccluder(*a_property, *a_geometry, capture->occluders);
				const auto predicateEnd = CaptureClock::now();
				timings.hookPredicate.fetch_add((predicateEnd - begin).count(), std::memory_order_relaxed);
				if (reject) {
					stats.rejected[static_cast<std::size_t>(*reject)].fetch_add(1, std::memory_order_relaxed);
					return nullptr;
				}
				stats.accepted.fetch_add(1, std::memory_order_relaxed);

				auto* passes = func(a_property, a_geometry, a_renderMode, a_accumulator);
				const auto stockEnd = CaptureClock::now();
				timings.hookStock.fetch_add((stockEnd - predicateEnd).count(), std::memory_order_relaxed);
				if (passes) {
					stats.delegated.fetch_add(1, std::memory_order_relaxed);
					return passes;
				}

				bool created = false;
				passes = BuildOcclusionPasses(*a_property, *a_geometry, *a_accumulator, created);
				timings.hookOwn.fetch_add((CaptureClock::now() - stockEnd).count(), std::memory_order_relaxed);
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
