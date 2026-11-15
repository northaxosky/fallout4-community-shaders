#include "Render/PrepassExtraTargets.h"

#include "Log.h"
#include "LogThrottle.h"
#include "Render/DerivedBlendStates.h"
#include "Render/Engine.h"
#include "Render/FrameBindings.h"
#include "Render/RenderHooks.h"
#include "Render/RuntimeCallback.h"
#include "Render/ShaderInjection.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSBuffer.h"

#include <d3d11.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.prepassextratargets");

		struct Entry
		{
			Feature* owner = nullptr;
			PrepassExtraTarget target;
			std::unique_ptr<cs::buffer::Texture2D> texture;
			bool ready = false;
		};

		constexpr std::size_t kMaxRenderTargets = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
		// Runs after the shared substrate resets the frame's bindings.
		constexpr auto kBeginPriority = static_cast<HookPriority>(101);

		// Entries are added only during Load, before rendering.
		std::vector<Entry> g_entries;
		bool g_hooked = false;
		std::uint64_t g_bindEpoch = UINT64_MAX;

		// Render thread only.
		PrepassExtraWritePhase g_phase = PrepassExtraWritePhase::kNative;
		render::DerivedBlendStateCache g_blend;
		std::atomic<std::uint64_t> g_finalizedDraws{ 0 };
		std::atomic<std::uint64_t> g_derivedBinds{ 0 };
		std::atomic<std::uint64_t> g_derivedSeenAsSource{ 0 };
		std::atomic<std::uint64_t> g_unscopedSkips{ 0 };
		std::atomic<std::uint64_t> g_nullBlendDraws{ 0 };

		constexpr std::array<const char*, 4> kPhaseNames{ "native", "terrain-color", "terrain-data", "terrain-opaque" };

		bool EnsureTexture(Entry& a_entry, const RE::BSGraphics::State& a_graphics)
		{
			auto& texture = a_entry.texture;
			if (texture && texture->desc.Width == a_graphics.screenWidth && texture->desc.Height == a_graphics.screenHeight)
				return true;
			const auto& name = a_entry.target.name;
			try {
				texture = cs::buffer::CreateRenderSizeTexture(
					a_graphics.screenWidth, a_graphics.screenHeight, a_entry.target.format,
					D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, name);
				return true;
			} catch (const std::exception& e) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Prepass target {} unavailable: {}", name, e.what());
			} catch (...) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Prepass target {} unavailable.", name);
			}
			return false;
		}

		void BeginPrepass()
		{
			for (auto& entry : g_entries)
				entry.ready = false;
			auto* context = GetImmediateContext();
			if (!context)
				return;
			// The prepass renders into the target, so no stage may still sample it.
			ID3D11ShaderResourceView* none = nullptr;
			for (const auto& entry : g_entries) {
				if (entry.target.publishSlot)
					BindFrameShaderResources(context, ShaderStage::kPixel, *entry.target.publishSlot, 1, &none);
			}
			const auto* graphics = GetGraphicsState();
			if (!graphics)
				return;
			for (auto& entry : g_entries) {
				bool active = false;
				cs::render::RunRenderCallback(entry.owner, "PrepassExtraTarget", [&] { active = entry.target.active(); });
				if (!active || !EnsureTexture(entry, *graphics))
					continue;
				context->ClearRenderTargetView(entry.texture->rtv.get(), entry.target.clear.data());
				entry.ready = true;
			}
		}

		std::uint32_t ReadyRenderTargetCount() noexcept
		{
			std::uint32_t count = 0;
			for (const auto& entry : g_entries) {
				if (entry.ready)
					count = (std::max)(count, entry.target.rtvSlot + 1);
			}
			return count;
		}

		// Lets a_rebind edit the bound set for ready targets; one OMSet if any changed.
		template <class Rebind>
		void RebindReadyTargets(ID3D11DeviceContext* a_context, std::uint32_t a_count, Rebind&& a_rebind)
		{
			std::array<ID3D11RenderTargetView*, kMaxRenderTargets> targets{};
			winrt::com_ptr<ID3D11DepthStencilView> depth;
			a_context->OMGetRenderTargets(a_count, targets.data(), depth.put());
			auto next = targets;
			bool changed = false;
			for (const auto& entry : g_entries) {
				if (entry.ready)
					changed |= a_rebind(entry, next[entry.target.rtvSlot]);
			}
			if (changed)
				a_context->OMSetRenderTargets(a_count, next.data(), depth.get());
			for (auto* target : targets) {
				if (target)
					target->Release();
			}
		}

		void BindTargets(ID3D11DeviceContext* a_context)
		{
			// Only an engine output commit or plugin work can unbind a target.
			const auto epoch = GetFrameBindingEpoch();
			const auto count = ReadyRenderTargetCount();
			if (count == 0 || epoch == g_bindEpoch)
				return;
			g_bindEpoch = epoch;
			// FO4: extra slots follow the engine's six G-buffer targets, left as bound.
			RebindReadyTargets(a_context, count, [](const Entry& a_entry, ID3D11RenderTargetView*& a_slot) {
				auto* const rtv = a_entry.texture->rtv.get();
				const bool changed = a_slot != rtv;
				a_slot = rtv;
				return changed;
			});
		}

		// Native states map to themselves; a terrain phase overrides each target's write.
		std::optional<D3D11_BLEND_DESC> DeriveBlend(D3D11_BLEND_DESC a_desc, PrepassExtraWritePhase a_phase)
		{
			if (a_phase == PrepassExtraWritePhase::kNative)
				return std::nullopt;
			if (!a_desc.IndependentBlendEnable) {
				for (std::size_t slot = 1; slot < kMaxRenderTargets; ++slot)
					a_desc.RenderTarget[slot] = a_desc.RenderTarget[0];
				a_desc.IndependentBlendEnable = TRUE;
			}
			const auto phase = static_cast<std::size_t>(a_phase) - 1;
			for (const auto& entry : g_entries) {
				const auto write = entry.target.terrain[phase];
				a_desc.RenderTarget[entry.target.rtvSlot] = {
					.BlendEnable = write.blend,
					.SrcBlend = D3D11_BLEND_SRC_ALPHA,
					.DestBlend = D3D11_BLEND_INV_SRC_ALPHA,
					.BlendOp = D3D11_BLEND_OP_ADD,
					.SrcBlendAlpha = D3D11_BLEND_ONE,
					.DestBlendAlpha = D3D11_BLEND_ZERO,
					.BlendOpAlpha = D3D11_BLEND_OP_ADD,
					.RenderTargetWriteMask = write.mask
				};
			}
			return a_desc;
		}

		// The bound state follows the draw, so the derivation reads it per draw and the draw scope restores it.
		void BindBlend(ID3D11DeviceContext* a_context)
		{
			ID3D11BlendState* raw = nullptr;
			FLOAT factor[4]{};
			UINT sampleMask = 0;
			a_context->OMGetBlendState(&raw, factor, &sampleMask);
			winrt::com_ptr<ID3D11BlendState> bound;
			bound.attach(raw);
			if (!bound) {
				g_nullBlendDraws.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			if (g_blend.IsDerived(bound.get())) {
				g_derivedSeenAsSource.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			const auto phase = g_phase;
			auto* const target = g_blend.Resolve(bound.get(), static_cast<std::uint32_t>(phase), [phase](const D3D11_BLEND_DESC& a_desc) {
				return DeriveBlend(a_desc, phase);
			});
			if (target == bound.get())
				return;
			if (!CaptureShaderInjectionBlendState(a_context)) {
				g_unscopedSkips.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			a_context->OMSetBlendState(target, factor, sampleMask);
			g_derivedBinds.fetch_add(1, std::memory_order_relaxed);
		}

		// After every contribution bind of a prepass draw, so no feature's state can overwrite it.
		void FinalizeTargets(ID3D11DeviceContext* a_context)
		{
			if (ReadyRenderTargetCount() == 0)
				return;
			BindTargets(a_context);
			g_finalizedDraws.fetch_add(1, std::memory_order_relaxed);
			BindBlend(a_context);
		}

		void PublishTargets()
		{
			auto* context = GetImmediateContext();
			const auto count = ReadyRenderTargetCount();
			if (!context || count == 0)
				return;
			RebindReadyTargets(context, count, [](const Entry& a_entry, ID3D11RenderTargetView*& a_slot) {
				const bool changed = a_slot == a_entry.texture->rtv.get();
				if (changed)
					a_slot = nullptr;
				return changed;
			});
			for (const auto& entry : g_entries) {
				if (!entry.ready || !entry.target.publishSlot)
					continue;
				auto* srv = entry.texture->srv.get();
				BindFrameShaderResources(context, ShaderStage::kPixel, *entry.target.publishSlot, 1, &srv);
			}
		}
	}

	std::optional<PrepassExtraTargetId> RegisterPrepassExtraTarget(Feature& a_owner, PrepassExtraTarget a_target)
	{
		if (a_target.rtvSlot >= kMaxRenderTargets || !a_target.active ||
			std::ranges::any_of(g_entries, [&](const Entry& a_entry) { return a_entry.target.rtvSlot == a_target.rtvSlot; })) {
			L->error("Prepass target {} has an invalid or taken render target slot {}.", a_target.name, a_target.rtvSlot);
			return std::nullopt;
		}
		if (!g_hooked) {
			if (!RegisterPreDeferredPrePass(&BeginPrepass, kBeginPriority) ||
				!RegisterPostDeferredPrePass(&PublishTargets) ||
				!RegisterShaderInjectionFinalizer(ShaderInjectionTarget::kDeferredPrepass, &FinalizeTargets)) {
				return std::nullopt;
			}
			g_hooked = true;
		}
		g_entries.push_back({ .owner = &a_owner, .target = std::move(a_target) });
		return g_entries.size() - 1;
	}

	ScopedPrepassExtraWritePhase::ScopedPrepassExtraWritePhase(PrepassExtraWritePhase a_phase) noexcept :
		_previous(std::exchange(g_phase, a_phase))
	{}

	ScopedPrepassExtraWritePhase::~ScopedPrepassExtraWritePhase() noexcept
	{
		g_phase = _previous;
	}

	void WritePrepassExtraTargetTelemetry(telemetry::Sink& a_sink)
	{
		auto census = g_blend.TakeCensus();
		std::ranges::sort(census, [](const auto& a_left, const auto& a_right) { return a_left.uses > a_right.uses; });
		a_sink
			.Field("finalized_draws", g_finalizedDraws.load(std::memory_order_relaxed))
			.Field("derived_binds", g_derivedBinds.load(std::memory_order_relaxed))
			.Field("derived_seen_as_source", g_derivedSeenAsSource.load(std::memory_order_relaxed))
			.Field("unscoped_skips", g_unscopedSkips.load(std::memory_order_relaxed))
			.Field("null_blend_draws", g_nullBlendDraws.load(std::memory_order_relaxed))
			.Field("cache_entries", census.size());
		std::size_t index = 0;
		for (const auto& entry : census) {
			a_sink.Field(std::format("state_{}", index++), std::format("phase={} derived={} uses={} {}", kPhaseNames[entry.policy], entry.derived, entry.uses, entry.description));
		}
	}

	ID3D11ShaderResourceView* GetPrepassExtraTargetSRV(PrepassExtraTargetId a_id) noexcept
	{
		const auto& entry = g_entries[a_id];
		return entry.ready ? entry.texture->srv.get() : nullptr;
	}
}
