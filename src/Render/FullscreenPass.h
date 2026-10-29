#pragma once

#include "Render/ScopedContextState.h"

#include <d3d11_1.h>

#include <cstdint>

#include <winrt/base.h>

namespace cs::render
{
	// Fullscreen triangle in an isolated context state; engine caches survive.
	class FullscreenPass
	{
	public:
		// Throws on device failure.
		void Create(ID3D11Device* a_device);
		[[nodiscard]] bool Ready() const noexcept { return _state && _vertexShader && _rasterizer; }

		class Scope
		{
		public:
			// Binds the vertex shader, cull-none raster state, viewport and one target.
			Scope(const FullscreenPass& a_pass, ID3D11DeviceContext* a_context, ID3D11RenderTargetView* a_target, std::uint32_t a_width, std::uint32_t a_height);
			[[nodiscard]] bool IsActive() const noexcept { return _state.IsActive(); }
			void Draw(ID3D11PixelShader* a_shader) const;

		private:
			ScopedContextState _state;
			ID3D11DeviceContext* _context;
		};

	private:
		winrt::com_ptr<ID3DDeviceContextState> _state;
		winrt::com_ptr<ID3D11VertexShader> _vertexShader;
		winrt::com_ptr<ID3D11RasterizerState> _rasterizer;
	};
}
