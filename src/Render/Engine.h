#pragma once

#include "RE/B/BSGraphics.h"
#include "RE/B/BSImagespaceShader.h"
#include "RE/B/BSShader.h"
#include "RE/B/BSShaderManager.h"
#include "RE/B/BSTriShape.h"
#include "RE/I/ImageSpaceEffect.h"
#include "RE/I/ImageSpaceEffectScalableAmbientObscuranceCS.h"
#include "RE/I/ImageSpaceManager.h"
#include "RE/I/Interface3D.h"
#include "RE/N/NiTexture.h"
#include "RE/S/SceneGraph.h"
#include "Render/FrameBuffer.h"
#include "Render/RendererContext.h"

#include <DirectXMath.h>
#include <d3d11.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

namespace cs::engine
{
	namespace native
	{
		struct ImageSpaceMacroSet
		{
			std::array<std::pair<std::string, std::string>, 7> values;
			std::size_t count = 0;
		};

		[[nodiscard]] inline std::optional<ImageSpaceMacroSet>
		GetImageSpaceMacros(RE::BSShader* a_shader) noexcept
		{
			if (!a_shader || a_shader->shaderType != static_cast<std::int32_t>(RE::BSShaderManager::ShaderEnum::kImageSpace))
				return std::nullopt;

			std::array<RE::BSImagespaceShader::ShaderMacro, 8> nativeMacros{};
			std::ignore = static_cast<RE::BSImagespaceShader*>(a_shader)->GetShaderMacros(nativeMacros.data());

			ImageSpaceMacroSet result;
			for (const auto& macro : nativeMacros) {
				if (!macro.name) {
					if (macro.value)
						return std::nullopt;
					return result;
				}
				if (!macro.value || result.count >= result.values.size()) {
					return std::nullopt;
				}
				result.values[result.count++] = {
					macro.name, macro.value
				};
			}
			return std::nullopt;
		}
	}

	// World-space directional ambient: channel c is pow(max(0, dot(rows[c], float4(N, 1))), 2.2).
	[[nodiscard]] inline bool TryGetDirectionalAmbientRows(DirectX::XMFLOAT4 (&a_rows)[3]) noexcept
	{
		const auto* state = RE::BSShaderManager::State::GetSingleton();
		if (!state)
			return false;
		const auto* transform = &state->GetDirectionalAmbientTransform();
		// BSDFLightShader::SetupGeometry scales rotation by scale, appends (translate, 1), and evaluates (N, 1) * M.
		const auto& rotate = transform->rotate.entry;
		const float scale = transform->scale;
		const float translate[3]{ transform->translate.x, transform->translate.y, transform->translate.z };
		const auto column = [&](const RE::NiPoint4& a_row, std::size_t a_channel) {
			return (a_channel == 0 ? a_row.x : a_channel == 1 ? a_row.y :
																a_row.z) *
			       scale;
		};
		for (std::size_t channel = 0; channel < 3; ++channel) {
			a_rows[channel] = {
				column(rotate[0], channel),
				column(rotate[1], channel),
				column(rotate[2], channel),
				translate[channel]
			};
		}
		return std::isfinite(scale);
	}

	// Index 0 is the world scene.
	[[nodiscard]] inline RE::ShadowSceneNode* GetWorldShadowSceneNode() noexcept
	{
		const auto* state = RE::BSShaderManager::State::GetSingleton();
		return state ? state->GetShadowSceneNode(0) : nullptr;
	}

	[[nodiscard]] inline RE::BSGraphics::State* GetGraphicsState()
	{
		return RE::BSGraphics::State::GetSingleton();
	}

	[[nodiscard]] inline std::uint32_t GetFrameCount() noexcept
	{
		const auto* state = RE::BSGraphics::State::GetSingleton();
		return state ? state->frameCount : 0u;
	}

	[[nodiscard]] inline RE::BSGraphics::RenderTargetManager* GetRenderTargetManager()
	{
		return RE::BSGraphics::RenderTargetManager::GetSingleton();
	}

	[[nodiscard]] inline RE::ImageSpaceEffect* GetImageSpaceEffect(
		RE::ImageSpaceManager::ImageSpaceEffectEnum a_effect)
	{
		auto* manager = RE::ImageSpaceManager::GetSingleton();
		return manager ? manager->GetEffect(a_effect) : nullptr;
	}

	// SAO_CS: saoToggleState gates the composite; isActive also gates SSLR Hi-Z.
	[[nodiscard]] inline RE::ImageSpaceEffectScalableAmbientObscuranceCS* GetScalableAOCompute()
	{
		return static_cast<RE::ImageSpaceEffectScalableAmbientObscuranceCS*>(GetImageSpaceEffect(
			RE::ImageSpaceManager::ImageSpaceEffectEnum::EFFECT_SAO_CS));
	}

	// Read by DeferredComposite (SSS flag) and DFLight (character light).
	struct NativeSubsurfaceControls
	{
		bool& screenSpaceSubsurfaceScatteringEnabled;
		bool& characterLightEnabled;
		RE::NiColorA& characterLightParams;
	};

	[[nodiscard]] inline std::optional<NativeSubsurfaceControls> GetNativeSubsurfaceControls() noexcept
	{
		auto* state = RE::BSShaderManager::State::GetSingleton();
		if (!state)
			return std::nullopt;
		return NativeSubsurfaceControls{
			state->GetScreenSpaceSubsurfaceScatteringEnabled(),
			state->GetCharacterLightEnabled(),
			state->GetCharacterLightParams()
		};
	}

	inline void SetDynamicResolutionRatios(float a_widthRatio, float a_heightRatio)
	{
		if (auto* renderTargetManager = GetRenderTargetManager()) {
			renderTargetManager->SetDynamicResolutionState(
				a_widthRatio,
				a_heightRatio,
				renderTargetManager->IsDynamicResolutionCurrentlyActivated());
		}
	}

	inline void SetDynamicResolution(float a_widthRatio, float a_heightRatio, bool a_activated)
	{
		if (auto* renderTargetManager = GetRenderTargetManager()) {
			renderTargetManager->SetDynamicResolutionState(a_widthRatio, a_heightRatio, a_activated);
		}
	}

	// Dynamic triangle UV is 2 * render ratio; full-size passes need the unit one.
	class ImageSpaceTriangleSwap
	{
	public:
		void Apply() noexcept
		{
			auto* manager = RE::ImageSpaceManager::GetSingleton();
			if (_dynamic || !manager || !manager->dynamicScreenTriShape || !manager->screenTriShape) {
				return;
			}
			_dynamic = manager->dynamicScreenTriShape;
			manager->dynamicScreenTriShape = manager->screenTriShape;
		}

		void Restore() noexcept
		{
			auto* manager = RE::ImageSpaceManager::GetSingleton();
			if (!_dynamic || !manager) {
				return;
			}
			manager->dynamicScreenTriShape = _dynamic;
			_dynamic.reset();
		}

	private:
		RE::NiPointer<RE::BSTriShape> _dynamic;
	};

	// Previs accepts preselected objects on every face; bypass for real culling.
	class ScopedPreCullBypass
	{
	public:
		ScopedPreCullBypass() noexcept :
			_wasDisabled(RE::BSPreCulledObjects::QTempDisabled())
		{
			RE::BSPreCulledObjects::SetTempDisabled(true, false);
		}

		~ScopedPreCullBypass() noexcept { RE::BSPreCulledObjects::SetTempDisabled(_wasDisabled, false); }

		ScopedPreCullBypass(const ScopedPreCullBypass&) = delete;
		ScopedPreCullBypass& operator=(const ScopedPreCullBypass&) = delete;

	private:
		const bool _wasDisabled;
	};

	// A default-constructed value is the native state.
	struct DynamicResolutionState
	{
		float widthRatio = 1.0f;
		float heightRatio = 1.0f;
		bool activated = false;
		bool viewportAsDefault = false;

		[[nodiscard]] static DynamicResolutionState Capture()
		{
			DynamicResolutionState state;
			if (const auto* manager = GetRenderTargetManager()) {
				state.widthRatio = manager->GetDynamicWidthRatio();
				state.heightRatio = manager->GetDynamicHeightRatio();
				state.activated = manager->IsDynamicResolutionCurrentlyActivated();
				state.viewportAsDefault = manager->IsUsingDynamicResolutionViewportAsDefaultViewport();
			}
			return state;
		}

		void Apply() const
		{
			if (auto* manager = GetRenderTargetManager()) {
				manager->SetDynamicResolutionState(widthRatio, heightRatio, activated);
				manager->SetUseDynamicResolutionViewportAsDefaultViewport(viewportAsDefault);
			}
		}
	};

	inline DirectX::XMFLOAT2 SetJitter(DirectX::XMFLOAT2 a_offsets)
	{
		DirectX::XMFLOAT2 previous{};
		if (auto* state = GetGraphicsState()) {
			previous = { state->offsetX, state->offsetY };
			state->offsetX = a_offsets.x;
			state->offsetY = a_offsets.y;
		}
		return previous;
	}

	// Ratio 1 without jitter; the viewport flag is left to the engine.
	inline void DropRenderScale()
	{
		SetJitter({});
		SetDynamicResolution(1.0f, 1.0f, false);
	}

	// Full-extent offscreen draws: no render scale, viewport remap or jitter.
	class UnscaledRenderScope
	{
	public:
		explicit UnscaledRenderScope(bool a_active = true) noexcept :
			_active(a_active)
		{
			if (_active) {
				_dynamicResolution = DynamicResolutionState::Capture();
				DynamicResolutionState{}.Apply();
				_jitter = SetJitter({});
			}
		}

		~UnscaledRenderScope() noexcept
		{
			if (_active) {
				_dynamicResolution.Apply();
				SetJitter(_jitter);
			}
		}

		UnscaledRenderScope(const UnscaledRenderScope&) = delete;
		UnscaledRenderScope(UnscaledRenderScope&&) = delete;
		UnscaledRenderScope& operator=(const UnscaledRenderScope&) = delete;
		UnscaledRenderScope& operator=(UnscaledRenderScope&&) = delete;

	private:
		const bool _active;
		DynamicResolutionState _dynamicResolution;
		DirectX::XMFLOAT2 _jitter{};
	};

	// OG/NG/AE prepass 2850CB0/2096710/21F38B0 writes the transposed near inverse at shadow +0x8A0.
	[[nodiscard]] inline std::optional<DirectX::XMFLOAT4X4> GetPrepassFirstPersonProjectionInverse() noexcept
	{
		static_assert(offsetof(RE::BSGraphics::RendererShadowState, cameraData) +
						  offsetof(RE::BSGraphics::ViewData, inv1stPersonProjMat) ==
					  0x8A0);
		const auto* context = GetActiveContext();
		if (!context)
			return std::nullopt;
		DirectX::XMFLOAT4X4 result{};
		std::memcpy(&result, context->shadowState.cameraData.inv1stPersonProjMat, sizeof(result));
		const auto matrix = DirectX::XMLoadFloat4x4(&result);
		const float determinant = DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(matrix));
		return HasFiniteMatrix(result) && std::isfinite(determinant) && determinant != 0.0f ? std::optional{ result } : std::nullopt;
	}

	// A UAV bind silently drops the PS SRV; dirty its slots for engine resubmit.
	inline std::uint32_t ResubmitPixelShaderResource(const ID3D11ShaderResourceView* a_srv) noexcept
	{
		auto* context = GetActiveContext();
		if (!context || !a_srv)
			return 0;
		auto& state = context->shadowState;
		std::uint32_t dirtied = 0;
		for (std::uint32_t slot = 0; slot < std::size(state.psTexture); ++slot) {
			if (state.psTexture[slot] == reinterpret_cast<const void*>(a_srv))
				dirtied |= 1u << slot;
		}
		state.psResourceModifiedBits |= dirtied;
		return dirtied;
	}

	// Mirrors Interface3D::Renderer::RenderPrepassesAndMenus target selection.
	[[nodiscard]] inline bool RendersToPipboyTarget(const RE::Interface3D::Renderer& a_renderer) noexcept
	{
		using RE::Interface3D::OffscreenMenuSize;
		using RE::Interface3D::PostEffect;

		if (a_renderer.customRenderTarget != -1) {
			const auto target = static_cast<RE::BSGraphics::RenderTargetID>(a_renderer.customRenderTarget);
			return target == RE::BSGraphics::RenderTargetID::kPipboy || target == RE::BSGraphics::RenderTargetID::kPipboySwap;
		}
		if (a_renderer.postfx == PostEffect::kHUDGlass || a_renderer.postfx == PostEffect::kHUDGlassWithMod) {
			return false;
		}
		return a_renderer.omsize == OffscreenMenuSize::kPipboy;
	}

	[[nodiscard]] inline ID3D11Device* GetDevice()
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		return rendererData ? reinterpret_cast<ID3D11Device*>(rendererData->device) : nullptr;
	}

	[[nodiscard]] inline ID3D11DeviceContext* GetImmediateContext()
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		return rendererData ? reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) : nullptr;
	}

	[[nodiscard]] inline RE::BSGraphics::DepthStencilTarget* ResolveDepthStencilTarget(RE::BSGraphics::DepthStencilTargetID a_target)
	{
		const auto logicalID = static_cast<std::int32_t>(a_target);
		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* renderTargetManager = GetRenderTargetManager();
		if (!rendererData || !renderTargetManager || logicalID < 0 || logicalID >= static_cast<std::int32_t>(RE::BSGraphics::DepthStencilTargetID::kCount)) {
			return nullptr;
		}
		const auto slot = renderTargetManager->GetDepthStencilTargetPlatformID(static_cast<std::size_t>(logicalID));
		return slot < std::size(rendererData->depthStencilTargets) ? &rendererData->depthStencilTargets[slot] : nullptr;
	}

	[[nodiscard]] inline ID3D11ShaderResourceView* GetSceneDepthSRV()
	{
		auto* target = ResolveDepthStencilTarget(RE::BSGraphics::DepthStencilTargetID::kMain);
		return target ? reinterpret_cast<ID3D11ShaderResourceView*>(target->srViewDepth) : nullptr;
	}

	// Resolve at each use; a cached slot goes stale when targets are recreated.
	[[nodiscard]] inline std::optional<std::uint32_t> ResolveRenderTargetSlot(RE::BSGraphics::RenderTargetID a_renderTarget)
	{
		const auto logicalID = static_cast<std::int32_t>(a_renderTarget);
		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* renderTargetManager = GetRenderTargetManager();
		if (!rendererData || !renderTargetManager || logicalID < 0 || logicalID >= static_cast<std::int32_t>(RE::BSGraphics::RenderTargetID::kCount)) {
			return std::nullopt;
		}
		const auto slot = renderTargetManager->GetRenderTargetPlatformID(static_cast<std::size_t>(logicalID));
		if (slot >= std::size(rendererData->renderTargets)) {
			return std::nullopt;
		}
		return slot;
	}

	[[nodiscard]] inline RE::BSGraphics::RenderTarget* ResolveRenderTarget(RE::BSGraphics::RenderTargetID a_renderTarget)
	{
		const auto slot = ResolveRenderTargetSlot(a_renderTarget);
		return slot ? &RE::BSGraphics::GetRendererData()->renderTargets[*slot] : nullptr;
	}

	// Points a logical target at another texture until restored.
	class RenderTargetSwap
	{
	public:
		void Apply(
			RE::BSGraphics::RenderTargetID a_logicalID,
			ID3D11Texture2D* a_texture,
			ID3D11ShaderResourceView* a_srv,
			ID3D11RenderTargetView* a_rtv) noexcept
		{
			auto* target = ResolveRenderTarget(a_logicalID);
			if (_target || !target || !a_texture || !a_srv || !a_rtv) {
				return;
			}
			_saved = *target;
			_target = target;
			target->texture = reinterpret_cast<decltype(target->texture)>(a_texture);
			target->srView = reinterpret_cast<decltype(target->srView)>(a_srv);
			target->rtView = reinterpret_cast<decltype(target->rtView)>(a_rtv);
		}

		void Restore() noexcept
		{
			if (_target) {
				*_target = _saved;
				_target = nullptr;
			}
		}

		[[nodiscard]] bool IsActive() const noexcept { return _target != nullptr; }

	private:
		RE::BSGraphics::RenderTarget* _target = nullptr;
		RE::BSGraphics::RenderTarget _saved{};
	};

	// The engine skips rebinding a target it thinks is current; fix OM after swaps.
	inline void ReplaceBoundRenderTargetView(
		ID3D11DeviceContext& a_context,
		ID3D11RenderTargetView* a_from,
		ID3D11RenderTargetView* a_to) noexcept
	{
		if (!a_from || !a_to || a_from == a_to) {
			return;
		}
		std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> bound{};
		ID3D11DepthStencilView* depth = nullptr;
		a_context.OMGetRenderTargets(static_cast<UINT>(bound.size()), bound.data(), &depth);
		auto rebind = bound;
		bool replaced = false;
		for (std::size_t i = 0; i < bound.size(); ++i) {
			if (bound[i] == a_from) {
				rebind[i] = a_to;
				replaced = true;
			}
		}
		if (replaced) {
			a_context.OMSetRenderTargets(static_cast<UINT>(rebind.size()), rebind.data(), depth);
		}
		for (auto* view : bound) {
			if (view) {
				view->Release();
			}
		}
		if (depth) {
			depth->Release();
		}
	}

	[[nodiscard]] inline ID3D11ShaderResourceView* GetRenderTargetSRV(RE::BSGraphics::RenderTargetID a_renderTarget)
	{
		auto* target = ResolveRenderTarget(a_renderTarget);
		return target ? reinterpret_cast<ID3D11ShaderResourceView*>(target->srView) : nullptr;
	}

	[[nodiscard]] inline ID3D11RenderTargetView* GetRenderTargetRTV(RE::BSGraphics::RenderTargetID a_renderTarget)
	{
		auto* target = ResolveRenderTarget(a_renderTarget);
		return target ? reinterpret_cast<ID3D11RenderTargetView*>(target->rtView) : nullptr;
	}

	[[nodiscard]] inline ID3D11UnorderedAccessView* GetRenderTargetUAV(RE::BSGraphics::RenderTargetID a_renderTarget)
	{
		auto* target = ResolveRenderTarget(a_renderTarget);
		return target ? reinterpret_cast<ID3D11UnorderedAccessView*>(target->uaView) : nullptr;
	}

	[[nodiscard]] inline DXGI_FORMAT GetTextureFormat(const RE::NiTexture* a_texture) noexcept
	{
		const auto* texture = a_texture ? a_texture->rendererTexture : nullptr;
		return texture ? static_cast<DXGI_FORMAT>(texture->header.format) : DXGI_FORMAT_UNKNOWN;
	}

	[[nodiscard]] inline ID3D11Texture2D* GetRenderTargetTexture(RE::BSGraphics::RenderTargetID a_renderTarget)
	{
		auto* target = ResolveRenderTarget(a_renderTarget);
		return target ? reinterpret_cast<ID3D11Texture2D*>(target->texture) : nullptr;
	}

	[[nodiscard]] inline ID3D11Texture2D* GetDepthStencilTexture(RE::BSGraphics::DepthStencilTargetID a_target)
	{
		auto* target = ResolveDepthStencilTarget(a_target);
		return target ? reinterpret_cast<ID3D11Texture2D*>(target->texture) : nullptr;
	}

	[[nodiscard]] inline ID3D11DepthStencilView* GetDepthStencilDSV(RE::BSGraphics::DepthStencilTargetID a_target)
	{
		auto* target = ResolveDepthStencilTarget(a_target);
		return target ? reinterpret_cast<ID3D11DepthStencilView*>(target->dsView[0]) : nullptr;
	}

	[[nodiscard]] inline ID3D11ShaderResourceView* GetDepthStencilDepthSRV(RE::BSGraphics::DepthStencilTargetID a_target)
	{
		auto* target = ResolveDepthStencilTarget(a_target);
		return target ? reinterpret_cast<ID3D11ShaderResourceView*>(target->srViewDepth) : nullptr;
	}
}
