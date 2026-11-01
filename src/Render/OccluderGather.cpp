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

		// Keeps the container test looser than the culler's own plane test.
		constexpr float kRadiusScale = 1.01f;
		constexpr float kRadiusPad = 1.0f;

		struct GatherCounters
		{
			std::uint64_t tested = 0;
			std::uint64_t culled = 0;
			std::uint64_t leaves = 0;
			std::uint64_t pruned = 0;
		};

		constexpr std::size_t kUpdateWorldBoundSlot = 0x33;

		// The stock walk skips radius-zero objects unless flagged always-draw.
		[[nodiscard]] bool HasUsableBound(const RE::NiAVObject& a_object) noexcept
		{
			return a_object.HasFlag(RE::NiAVObject::Flag::kAlwaysDraw) || a_object.worldBound.fRadius != 0.0f;
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

		[[nodiscard]] bool BoundOutside(const OrthoFrustum& a_frustum, const RE::NiBound& a_bound) noexcept
		{
			return a_bound.fRadius > 0.0f &&
			       SphereOutside(
					   a_frustum,
					   { a_bound.center.x, a_bound.center.y, a_bound.center.z },
					   a_bound.fRadius * kRadiusScale + kRadiusPad);
		}

		// FO4: only NiNode's own UpdateWorldBound merges every child into the bound.
		[[nodiscard]] bool BoundEnclosesChildren(const RE::NiNode& a_node) noexcept
		{
			static const auto stock = reinterpret_cast<const std::uintptr_t*>(
				REL::Relocation<std::uintptr_t>{ RE::VTABLE::NiNode[0] }.address())[kUpdateWorldBoundSlot];
			const auto* vtable = *reinterpret_cast<const std::uintptr_t* const*>(&a_node);
			return vtable[kUpdateWorldBoundSlot] == stock && !a_node.HasFlag(RE::NiAVObject::Flag::kFixedWorldBound);
		}

		// Fade nodes and geometry are what the culler frustum-tests, fades and expands.
		[[nodiscard]] RE::NiNode* AsContainer(RE::NiAVObject& a_object) noexcept
		{
			return a_object.IsFadeNode() ? nullptr : a_object.IsNode();
		}

		// The stock container flatten, leaving leaf tests to the culler.
		template <class Add>
		void FeedLeaves(RE::NiNode& a_container, const OrthoFrustum& a_frustum, const Add& a_add, GatherCounters& a_counters)
		{
			for (const auto& entry : a_container.children) {
				auto* child = entry.get();
				if (!child || child->GetAppCulled() || !HasUsableBound(*child))
					continue;
				const bool leaf = child->IsGeometry() || child->IsFadeNode();
				auto* node = leaf ? nullptr : child->IsNode();
				if (!leaf && !node)
					continue;
				// FO4: the culler keeps kAlwaysDraw entries whatever their bound.
				const bool prunable = !child->HasFlag(RE::NiAVObject::Flag::kAlwaysDraw) && (leaf || BoundEnclosesChildren(*node));
				if (prunable && BoundOutside(a_frustum, child->worldBound)) {
					++a_counters.pruned;
					continue;
				}
				if (leaf) {
					a_add(child, &child->worldBound);
					++a_counters.leaves;
				} else {
					FeedLeaves(*node, a_frustum, a_add, a_counters);
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

				const ScopedCaptureTimer timer{ GetCaptureTimings().gather };
				GatherCounters counters;
				if (HasUsableBound(*a_root)) {
					++counters.tested;
					const auto frustum = MakeFrustum(*capture->camera);
					if (BoundOutside(frustum, a_root->worldBound)) {
						++counters.culled;
					} else {
						FeedLeaves(
							*container,
							frustum,
							[&](RE::NiAVObject* a_leaf, const RE::NiBound* a_leafBound) { func(a_group, a_leaf, a_leafBound, 0); },
							counters);
					}
				}

				if (telemetry::pump::Enabled()) {
					auto& stats = GetGatherStats();
					stats.rootsTested.fetch_add(counters.tested, std::memory_order_relaxed);
					stats.rootsCulled.fetch_add(counters.culled, std::memory_order_relaxed);
					stats.leavesFed.fetch_add(counters.leaves, std::memory_order_relaxed);
					stats.nodesPruned.fetch_add(counters.pruned, std::memory_order_relaxed);
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
