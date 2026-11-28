#pragma once

#include "Utils/CSBuffer.h"

#include <d3d11.h>

#include <array>
#include <memory>
#include <span>
#include <string_view>
#include <winrt/base.h>

namespace cs::render
{
	// SRV and UAV texture; mips above one get no UAV, so make per-mip views.
	[[nodiscard]] std::unique_ptr<buffer::Texture2D> CreateComputeTexture(
		UINT a_width, UINT a_height, DXGI_FORMAT a_format,
		std::string_view a_name, UINT a_mips = 1);

	// Preserves CS t8-t15 (ComputeScope owns t0-t7) and an optional cbuffer slot.
	class ComputePasses
	{
	public:
		static constexpr UINT kSrvSlots = 16;
		static constexpr UINT kUavSlots = 8;

		explicit ComputePasses(ID3D11DeviceContext* a_context);
		ComputePasses(ID3D11DeviceContext* a_context, UINT a_preservedSlot, ID3D11Buffer* a_buffer);
		~ComputePasses();

		ComputePasses(const ComputePasses&) = delete;
		ComputePasses& operator=(const ComputePasses&) = delete;

		template <std::size_t Srvs, std::size_t Uavs>
		void Run(ID3D11ComputeShader* a_shader,
			ID3D11ShaderResourceView* const (&a_srvs)[Srvs],
			ID3D11UnorderedAccessView* const (&a_uavs)[Uavs],
			UINT a_width, UINT a_height, UINT a_group, std::string_view a_name)
		{
			static_assert(Srvs <= kSrvSlots && Uavs <= kUavSlots);
			Dispatch(a_shader, a_srvs, a_uavs, a_width, a_height, a_group, a_name);
		}

	private:
		void Dispatch(ID3D11ComputeShader* a_shader,
			std::span<ID3D11ShaderResourceView* const> a_srvs,
			std::span<ID3D11UnorderedAccessView* const> a_uavs,
			UINT a_width, UINT a_height, UINT a_group, std::string_view a_name);

		static constexpr UINT kScopeSrvs = 8;
		static constexpr UINT kPreservedSrvs = kSrvSlots - kScopeSrvs;

		ID3D11DeviceContext* _context;
		static constexpr UINT kNoSlot = ~0u;
		UINT _bufferSlot = kNoSlot;
		winrt::com_ptr<ID3D11Buffer> _engineBuffer;
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, kPreservedSrvs> _engineSRVs;
	};
}
