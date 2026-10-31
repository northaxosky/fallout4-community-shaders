#include "Render/OccluderGather.h"

#include "Render/OrthoFrustum.h"
#include "Telemetry/Telemetry.h"

#include <cstdint>
#include <tuple>

namespace cs::engine
{
	namespace
	{
		// The culling group is engine-owned; only its Add entry is called.
		struct CullingGroup;

		// The loop that adds each unculled child of the scene root to the group.
		constexpr CallSiteAnchor kSceneRootAdd{
			.name = "BSShaderUtil::AccumulateScene -> BSCullingGroup::Add (scene root children)",
			.function = RE::ID::BSShaderUtil::AccumulateScene,
			.offset = { 0x259, 0x319, 0x319 },
			.target = RE::ID::BSCullingGroup::Add
		};

		// NiAVObject flag of nodes with an authored bound instead of a merged one.
		constexpr std::uint64_t kFixedBound = 1ull << 11;

		// Keeps the container test looser than the culler's own plane test.
		constexpr float kRadiusScale = 1.01f;
		constexpr float kRadiusPad = 1.0f;

		struct GatherCounters
		{
			std::uint64_t tested = 0;
			std::uint64_t culled = 0;
			std::uint64_t leaves = 0;
		};

		// The stock walk skips boundless objects: radius zero without an authored bound.
		[[nodiscard]] bool HasUsableBound(const RE::NiAVObject& a_object) noexcept
		{
			return (a_object.GetFlags() & kFixedBound) || a_object.worldBound.fRadius != 0.0f;
		}

		[[nodiscard]] OrthoFrustum MakeFrustum(const RE::NiCamera& a_camera) noexcept
		{
			const auto axis = [&](std::size_t a_row) {
				const auto& row = a_camera.world.rotate[a_row];
				return OrthoFrustum::Vec{ row.x, row.y, row.z };
			};
			const auto& translate = a_camera.world.translate;
			// Windows headers define near and far as macros, so bind by position.
			const auto& [left, right, top, bottom, nearZ, farZ, ortho] = a_camera.viewFrustum;
			std::ignore = ortho;
			// NiFrustumPlanes::Set reads rows 0, 1 and 2 as depth, up and right.
			return {
				.depthAxis = axis(0),
				.upAxis = axis(1),
				.rightAxis = axis(2),
				.origin = { translate.x, translate.y, translate.z },
				.left = left,
				.right = right,
				.top = top,
				.bottom = bottom,
				.nearPlane = nearZ,
				.farPlane = farZ
			};
		}

		// Fade nodes and geometry are what the culler frustum-tests, fades and expands.
		[[nodiscard]] RE::NiNode* AsContainer(RE::NiAVObject& a_object) noexcept
		{
			return a_object.IsFadeNode() ? nullptr : a_object.IsNode();
		}

		// The stock flatten of a container, but handing leaves to the culler's own tests.
		template <class Add>
		void FeedLeaves(RE::NiNode& a_container, const Add& a_add, GatherCounters& a_counters)
		{
			for (const auto& entry : a_container.children) {
				auto* child = entry.get();
				if (!child || child->GetAppCulled() || !HasUsableBound(*child))
					continue;
				if (child->IsGeometry() || child->IsFadeNode()) {
					a_add(child, &child->worldBound);
					++a_counters.leaves;
				} else if (auto* node = child->IsNode()) {
					FeedLeaves(*node, a_add, a_counters);
				}
			}
		}

		struct SceneRootAdd
		{
			static std::int64_t thunk(
				CullingGroup* a_group,
				RE::NiAVObject* a_root,
				const RE::NiBound* a_bound,
				std::uint8_t a_flags)
			{
				const auto* capture = GetActiveCaptureOnWalkThread();
				auto* container = capture && capture->camera && a_root && !a_flags ? AsContainer(*a_root) : nullptr;
				if (!container)
					return func(a_group, a_root, a_bound, a_flags);

				GatherCounters counters;
				if (HasUsableBound(*a_root)) {
					++counters.tested;
					const auto& bound = a_root->worldBound;
					const bool outside = bound.fRadius > 0.0f &&
					                     SphereOutside(
											 MakeFrustum(*capture->camera),
											 { bound.center.x, bound.center.y, bound.center.z },
											 bound.fRadius * kRadiusScale + kRadiusPad);
					if (outside) {
						++counters.culled;
					} else {
						FeedLeaves(
							*container,
							[&](RE::NiAVObject* a_leaf, const RE::NiBound* a_leafBound) { func(a_group, a_leaf, a_leafBound, 0); },
							counters);
					}
				}

				if (telemetry::pump::Enabled()) {
					auto& stats = GetGatherStats();
					stats.rootsTested.fetch_add(counters.tested, std::memory_order_relaxed);
					stats.rootsCulled.fetch_add(counters.culled, std::memory_order_relaxed);
					stats.leavesFed.fetch_add(counters.leaves, std::memory_order_relaxed);
				}
				return 0;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	HookInstall InstallOccluderGatherHook()
	{
		return InstallCallSiteHook(kSceneRootAdd, &stl::write_thunk_call<SceneRootAdd>);
	}
}
