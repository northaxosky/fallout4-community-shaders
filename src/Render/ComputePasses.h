#pragma once

#include "Utils/CSBuffer.h"

#include <d3d11.h>

#include <array>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <winrt/base.h>

namespace cs::render
{
	// SRV and UAV texture; mips above one get no UAV, so make per-mip views.
	[[nodiscard]] std::unique_ptr<buffer::Texture2D> CreateComputeTexture(
		UINT a_width, UINT a_height, DXGI_FORMAT a_format,
		std::string_view a_name, UINT a_mips = 1);

	// Preserves CS t8-t9 and an optional cbuffer slot across engine cleanup.
	class ComputePasses
	{
	public:
		explicit ComputePasses(ID3D11DeviceContext* a_context);
		ComputePasses(ID3D11DeviceContext* a_context, UINT a_preservedSlot, ID3D11Buffer* a_buffer);
		~ComputePasses();

		ComputePasses(const ComputePasses&) = delete;
		ComputePasses& operator=(const ComputePasses&) = delete;

		void Run(ID3D11ComputeShader* a_shader,
			std::initializer_list<ID3D11ShaderResourceView*> a_srvs,
			std::initializer_list<ID3D11UnorderedAccessView*> a_uavs,
			UINT a_width, UINT a_height, UINT a_group, std::string_view a_name);

	private:
		ID3D11DeviceContext* _context;
		static constexpr UINT kNoSlot = ~0u;
		UINT _bufferSlot = kNoSlot;
		winrt::com_ptr<ID3D11Buffer> _engineBuffer;
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 2> _tail;
	};
}
