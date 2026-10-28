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
		using Index = decltype(manager->effectList)::size_type;
		const auto index = static_cast<Index>(a_effect);
		return manager && index < manager->effectList.size() ? manager->effectList[index] : nullptr;
	}

	// SAO_CS: saoToggleState gates the composite; isActive also gates SSLR Hi-Z.
	[[nodiscard]] inline RE::ImageSpaceEffectScalableAmbientObscuranceCS* GetScalableAOCompute()
	{
		return static_cast<RE::ImageSpaceEffectScalableAmbientObscuranceCS*>(GetImageSpaceEffect(
			RE::ImageSpaceManager::ImageSpaceEffectEnum::EFFECT_SAO_CS));
	}

	// Native SSS and character-light controls; DeferredComposite and DFLight read them.
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

	// Full-extent offscreen draws: no render scale, viewport remap or jitter.
	class UnscaledRenderScope
	{
	public:
		explicit UnscaledRenderScope(bool a_active = true) noexcept :
			_manager(a_active ? GetRenderTargetManager() : nullptr),
			_state(a_active ? GetGraphicsState() : nullptr)
		{
			if (_manager) {
				_widthRatio = _manager->GetDynamicWidthRatio();
				_heightRatio = _manager->GetDynamicHeightRatio();
				_activated = _manager->IsDynamicResolutionCurrentlyActivated();
				_viewportAsDefault = _manager->IsUsingDynamicResolutionViewportAsDefaultViewport();
				SetDynamicResolution(1.0f, 1.0f, false);
				_manager->SetUseDynamicResolutionViewportAsDefaultViewport(false);
			}
			if (_state) {
				_offsetX = _state->offsetX;
				_offsetY = _state->offsetY;
				_state->offsetX = 0.0f;
				_state->offsetY = 0.0f;
			}
		}

		~UnscaledRenderScope() noexcept
		{
			if (_manager) {
				SetDynamicResolution(_widthRatio, _heightRatio, _activated);
				_manager->SetUseDynamicResolutionViewportAsDefaultViewport(_viewportAsDefault);
			}
			if (_state) {
				_state->offsetX = _offsetX;
				_state->offsetY = _offsetY;
			}
		}

		UnscaledRenderScope(const UnscaledRenderScope&) = delete;
		UnscaledRenderScope(UnscaledRenderScope&&) = delete;
		UnscaledRenderScope& operator=(const UnscaledRenderScope&) = delete;
		UnscaledRenderScope& operator=(UnscaledRenderScope&&) = delete;

	private:
		RE::BSGraphics::RenderTargetManager* _manager;
		RE::BSGraphics::State* _state;
		float _widthRatio = 1.0f;
		float _heightRatio = 1.0f;
		bool _activated = false;
		bool _viewportAsDefault = false;
		float _offsetX = 0.0f;
		float _offsetY = 0.0f;
	};

	[[nodiscard]] inline bool TryGetWorldSceneProjection(
		DirectX::XMFLOAT4X4& a_outProj,
		DirectX::XMFLOAT4X4& a_outInvProj,
		DirectX::XMFLOAT4& a_outNdcToViewMul,
		DirectX::XMFLOAT4& a_outNdcToViewAdd)
	{
		const auto camera = GetWorldCameraRecord();
		if (!camera) {
			return false;
		}

		GetWorldSceneProjection(*camera, a_outProj, a_outInvProj, a_outNdcToViewMul, a_outNdcToViewAdd);
		return true;
	}

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

	// Dirties every PS shadow slot holding a_srv so the next engine draw resubmits it;
	// D3D11 unbinds an SRV when its resource is bound as a UAV behind the engine.
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

	[[nodiscard]] inline ID3D11ShaderResourceView* GetDepthStencilStencilSRV(RE::BSGraphics::DepthStencilTargetID a_target)
	{
		auto* target = ResolveDepthStencilTarget(a_target);
		return target ? reinterpret_cast<ID3D11ShaderResourceView*>(target->srViewStencil) : nullptr;
	}
}
