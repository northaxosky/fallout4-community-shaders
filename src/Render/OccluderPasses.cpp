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

		// BSUtilityShader technique bits of a depth-only static: T N BT, Sm, Smclamp.
		constexpr std::uint32_t kUtilityTexNormalBinormalTangent = 0x1A;
		constexpr std::uint32_t kUtilityShadowMap = 0x4000;
		constexpr std::uint32_t kUtilityShadowMapClamped = 0x8000;
		// Without the stock land bit: the utility vertex shader has no code behind it.
		constexpr std::uint32_t kLandscapeOcclusionTechnique =
			kUtilityTexNormalBinormalTangent | kUtilityShadowMap | kUtilityShadowMapClamped;

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

		[[nodiscard]] bool IsLoadedCellLandscape(const RE::BSLightingShaderProperty& a_property) noexcept
		{
			return a_property.flags.any(ShaderFlag::kMultiTextureLandscape) && a_property.flags.none(ShaderFlag::kLODLandscape);
		}

		// Land clears Cast Shadows at creation, so stock never builds its pass.
		RE::BSShaderProperty::RenderPassArray* BuildLandscapePasses(
			RE::BSLightingShaderProperty& a_property,
			RE::BSGeometry& a_geometry,
			const RE::BSShaderAccumulator& a_accumulator)
		{
			auto* shader = RE::BSUtilityShader::GetSingleton();
			if (!shader || a_accumulator.depthPassIndex >= std::size(a_property.depthMapRenderPassListA))
				return nullptr;
			auto& list = a_property.depthMapRenderPassListA[a_accumulator.depthPassIndex];

			// FO4: stock reads the fade node's mesh LOD level only when this geometry flag is set.
			auto lodMode = kDefaultLodMode;
			if ((a_geometry.GetFlags() & kGeometryMeshLodFlag) && a_property.fadeNode)
				lodMode = a_property.fadeNode->currentMeshLODLevel;

			if (list.passList) {
				if (list.passList->passEnum != kLandscapeOcclusionTechnique)
					return nullptr;
				list.passList->lodMode = lodMode;
				return &list;
			}

			auto* pass = list.Add(shader, &a_property, &a_geometry, kLandscapeOcclusionTechnique, 0, nullptr, nullptr, nullptr, nullptr);
			if (!pass)
				return nullptr;
			// The pass is new for this list, so it holds no command buffer to release first.
			pass->commandBuffer = shader->CreateCommandBuffer(pass);
			pass->lodMode = lodMode;
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
				if (IsLoadedCellLandscape(*a_property)) {
					passes = BuildLandscapePasses(*a_property, *a_geometry, *a_accumulator);
					timings.hookOwn.fetch_add(ReadTicks() - stockEnd, std::memory_order_relaxed);
					if (passes) {
						stats.ownBuilt.fetch_add(1, std::memory_order_relaxed);
						return passes;
					}
				}
				stats.stockOnly.fetch_add(1, std::memory_order_relaxed);
				return nullptr;
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
