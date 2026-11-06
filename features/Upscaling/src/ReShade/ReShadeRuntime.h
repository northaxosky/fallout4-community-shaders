#pragma once

#include <memory>

#include <d3d11.h>
#include <dxgi1_6.h>

#include "Render/DXGISwapChainProxy.h"

namespace cs::features
{
	// Hosts a ReShade effect runtime on a copy of the proxy back buffer.
	class ReShadeRuntime
	{
	public:
		ReShadeRuntime() noexcept;
		~ReShadeRuntime();
		ReShadeRuntime(const ReShadeRuntime&) = delete;
		ReShadeRuntime& operator=(const ReShadeRuntime&) = delete;

		// False when no ReShade module is loaded; cheap after the first call.
		[[nodiscard]] static bool IsInstalled() noexcept;

		// Creates the runtime once per Destroy; a failed attempt is not retried.
		bool Ensure(render::IDXGISwapChainProxyOwner& a_host, IDXGISwapChain4& a_inner,
			ID3D11Device& a_device, UINT a_width, UINT a_height) noexcept;
		// Leaks the runtime on purpose: ReShade must not run after quarantine.
		void Abandon() noexcept;
		void Destroy() noexcept;
		void ReplaceInner(IDXGISwapChain4* a_inner) noexcept;

		[[nodiscard]] bool EffectsEnabled() const noexcept;
		// Depth is the canonical output-resolution depth, or null if unavailable.
		void RenderEffects(ID3D11DeviceContext& a_context, ID3D11Resource& a_color,
			ID3D11ShaderResourceView* a_depth) noexcept;
		void Present(ID3D11DeviceContext& a_context, ID3D11Resource& a_color) noexcept;

	private:
		struct Impl;

		template <class F>
		void WithColorCopy(ID3D11DeviceContext& a_context, ID3D11Resource& a_color,
			const char* a_event, F&& a_body) noexcept;

		std::unique_ptr<Impl> _impl;
		bool _attempted = false;
	};
}
