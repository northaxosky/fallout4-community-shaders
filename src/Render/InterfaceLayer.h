#pragma once

#include <d3d11.h>
#include <winrt/base.h>

#include <atomic>

#include "Render/DerivedBlendStates.h"
#include "Render/Engine.h"

namespace cs::render
{
	// Splits a frame into a scene texture and an interface layer in the proxy.
	class InterfaceLayer
	{
	public:
		[[nodiscard]] static InterfaceLayer& Get() noexcept;

		// Redirects the engine's draw-time blend binds; false if the site is unproven.
		[[nodiscard]] bool Install();

		// Scene start; a_wanted false only restores a swap that outlived its frame.
		void BeginScene(bool a_wanted);
		[[nodiscard]] bool IsSceneActive() const noexcept { return _sceneSwap.IsActive(); }
		// Scene final: the target returns to the proxy, cleared for the interface.
		void EndScene();

		// True from EndScene until the presenter consumes the frame.
		[[nodiscard]] bool IsDrawingInterface() const noexcept { return _separated.load(std::memory_order_acquire); }
		[[nodiscard]] bool ConsumeSeparated() noexcept;

		// Blend state to bind for a draw on the interface layer.
		[[nodiscard]] ID3D11BlendState* Translate(ID3D11BlendState* a_state);
		[[nodiscard]] bool DrawsToProxy(ID3D11DeviceContext& a_context) const noexcept;

	private:
		void RestoreFrameTarget(ID3D11DeviceContext& a_context);

		engine::RenderTargetSwap _sceneSwap;
		std::atomic_bool _separated{ false };
		bool _blendTouched = false;  // Render thread only.
		std::atomic<ID3D11RenderTargetView*> _proxyTarget{ nullptr };

		DerivedBlendStateCache _translations;
	};
}
