#include "Render/InterfaceLayer.h"

#include "Log.h"
#include "Render/EngineCallSite.h"
#include "Render/RendererContext.h"
#include "Render/TemporalPipeline.h"

#include <array>
#include <utility>

namespace cs::render
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.interfacelayer");

		// SetDirtyStates' ID3D11DeviceContext::OMSetBlendState, on its blend-dirty branch.
		constexpr engine::ContextIndirectCallSiteAnchor kBlendBind{
			.name = "SetDirtyStates -> ID3D11DeviceContext::OMSetBlendState",
			.function = RE::ID::BSGraphics::SetDirtyStates,
			.offset = { 0x29F, 0x477, 0x477 },
			.target = { 0x118 }
		};

		void BlendBindThunk(ID3D11DeviceContext* a_context, ID3D11BlendState* a_state, const FLOAT* a_factor, UINT a_mask)
		{
			auto& layer = InterfaceLayer::Get();
			if (a_state && layer.IsDrawingInterface() && layer.DrawsToProxy(*a_context))
				a_state = layer.Translate(a_state);
			a_context->OMSetBlendState(a_state, a_factor, a_mask);
		}

		constexpr UINT8 kRgbMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;

		// The engine's states assume an opaque target; on a layer, alpha must accumulate coverage.
		// Returns false for a destination-dependent blend the layer cannot represent.
		bool TranslateTarget(D3D11_RENDER_TARGET_BLEND_DESC& a_target, bool& a_changed)
		{
			if (!a_target.BlendEnable || a_target.BlendOp != D3D11_BLEND_OP_ADD)
				return true;
			switch (a_target.DestBlend) {
			case D3D11_BLEND_INV_SRC_ALPHA:
				a_target.SrcBlendAlpha = D3D11_BLEND_ONE;
				a_target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
				a_target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
				if (a_target.RenderTargetWriteMask & kRgbMask)
					a_target.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
				a_changed = true;
				return true;
			case D3D11_BLEND_ONE:
				a_target.SrcBlendAlpha = D3D11_BLEND_ZERO;
				a_target.DestBlendAlpha = D3D11_BLEND_ONE;
				a_target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
				a_changed = true;
				return true;
			case D3D11_BLEND_ZERO:
				return true;
			default:
				return false;
			}
		}

		// ShaderFlags::DIRTY_BLEND_STATE, with the engine's last-draw compare defeated.
		void InvalidateEngineBlendState() noexcept
		{
			auto* context = engine::GetActiveContext();
			if (!context)
				return;
			context->shadowState.stateUpdateFlags |= std::to_underlying(RE::BSGraphics::ShaderFlags::DIRTY_BLEND_STATE);
			context->lastDrawCallShadowState.stateUpdateFlags = UINT32_MAX;
		}
	}

	InterfaceLayer& InterfaceLayer::Get() noexcept
	{
		static InterfaceLayer instance;
		return instance;
	}

	bool InterfaceLayer::Install()
	{
		static bool installed = false;
		if (installed)
			return true;
		const auto site = engine::ResolveCallSite(kBlendBind);
		if (!site) {
			L->error("Interface blend translation unavailable: {}", site.error());
			return false;
		}
		if (const auto redirected = engine::RedirectContextIndirectCall(*site, reinterpret_cast<std::uintptr_t>(&BlendBindThunk)); !redirected) {
			L->error("Interface blend translation unavailable: {}", redirected.error());
			return false;
		}
		installed = true;
		return true;
	}

	void InterfaceLayer::BeginScene(bool a_wanted)
	{
		auto* context = engine::GetImmediateContext();
		// A swap that survived its frame would leave the engine drawing into the scene texture.
		if (_sceneSwap.IsActive()) {
			L->error("The scene redirect outlived its frame; restoring it.");
			if (context)
				RestoreFrameTarget(*context);
			else
				_sceneSwap.Restore();
		}
		// Translated states may still be bound from the last interface; the scene wants its own.
		if (_blendTouched) {
			InvalidateEngineBlendState();
			_blendTouched = false;
		}
		if (!a_wanted || !context)
			return;
		const auto scene = TemporalPipeline::Get().GetHdrSceneTarget();
		auto* frameTarget = engine::GetRenderTargetRTV(RE::BSGraphics::RenderTargetID::kFrameBuffer);
		if (!scene || !frameTarget)
			return;
		_sceneSwap.Apply(RE::BSGraphics::RenderTargetID::kFrameBuffer, scene.texture, scene.srv, scene.rtv);
		if (_sceneSwap.IsActive())
			engine::ReplaceBoundRenderTargetView(*context, frameTarget, scene.rtv);
	}

	void InterfaceLayer::RestoreFrameTarget(ID3D11DeviceContext& a_context)
	{
		auto* sceneTarget = engine::GetRenderTargetRTV(RE::BSGraphics::RenderTargetID::kFrameBuffer);
		_sceneSwap.Restore();
		engine::ReplaceBoundRenderTargetView(
			a_context, sceneTarget, engine::GetRenderTargetRTV(RE::BSGraphics::RenderTargetID::kFrameBuffer));
	}

	void InterfaceLayer::EndScene()
	{
		if (!_sceneSwap.IsActive())
			return;
		auto* context = engine::GetImmediateContext();
		if (!context) {
			_sceneSwap.Restore();
			return;
		}
		RestoreFrameTarget(*context);
		auto* proxyTarget = engine::GetRenderTargetRTV(RE::BSGraphics::RenderTargetID::kFrameBuffer);
		if (!proxyTarget)
			return;
		// The interface composites over the scene with alpha, so it starts transparent.
		constexpr float kClear[4]{};
		context->ClearRenderTargetView(proxyTarget, kClear);
		// The scene's last blend bind would otherwise carry into the first interface draw.
		InvalidateEngineBlendState();
		_blendTouched = true;
		_proxyTarget.store(proxyTarget, std::memory_order_release);
		_separated.store(true, std::memory_order_release);
	}

	bool InterfaceLayer::ConsumeSeparated() noexcept
	{
		return _separated.exchange(false, std::memory_order_acq_rel);
	}

	bool InterfaceLayer::DrawsToProxy(ID3D11DeviceContext& a_context) const noexcept
	{
		const auto* proxy = _proxyTarget.load(std::memory_order_acquire);
		ID3D11RenderTargetView* bound = nullptr;
		a_context.OMGetRenderTargets(1, &bound, nullptr);
		const bool match = bound && bound == proxy;
		if (bound)
			bound->Release();
		return match;
	}

	ID3D11BlendState* InterfaceLayer::Translate(ID3D11BlendState* a_state)
	{
		const std::scoped_lock lock{ _translationLock };
		auto found = _translations.find(a_state);
		if (found == _translations.end())
			found = _translations.emplace(a_state, Build(a_state)).first;
		auto* translated = found->second.translated.get();
		return translated ? translated : a_state;
	}

	InterfaceLayer::Translation InterfaceLayer::Build(ID3D11BlendState* a_state)
	{
		Translation result;
		result.source.copy_from(a_state);
		D3D11_BLEND_DESC desc{};
		a_state->GetDesc(&desc);
		bool changed = false;
		for (auto& target : desc.RenderTarget) {
			if (!TranslateTarget(target, changed)) {
				L->warn("Blend state {} depends on the destination color and is drawn unchanged on the interface layer.",
					static_cast<const void*>(a_state));
			}
		}
		if (!changed)
			return result;
		winrt::com_ptr<ID3D11Device> device;
		a_state->GetDevice(device.put());
		if (FAILED(device->CreateBlendState(&desc, result.translated.put())))
			L->error("Could not create the translated blend state for {}.", static_cast<const void*>(a_state));
		return result;
	}
}
