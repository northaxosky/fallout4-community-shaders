#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <d3d11.h>
#include <winrt/base.h>

namespace cs::render
{
	template <std::size_t Count>
	class PixelShaderResourceSnapshot
	{
	public:
		static_assert(Count > 0);

		PixelShaderResourceSnapshot() = default;
		PixelShaderResourceSnapshot(const PixelShaderResourceSnapshot&) = delete;
		PixelShaderResourceSnapshot& operator=(const PixelShaderResourceSnapshot&) = delete;
		PixelShaderResourceSnapshot(PixelShaderResourceSnapshot&&) = delete;
		PixelShaderResourceSnapshot& operator=(PixelShaderResourceSnapshot&&) = delete;

		bool IsSaved() const noexcept
		{
			return _depth != 0;
		}

		bool Save(
			ID3D11DeviceContext* a_context,
			std::uint32_t a_startSlot) noexcept
		{
			if (_depth != 0) {
				++_depth;
				return false;
			}
			if (!a_context)
				return false;

			std::array<ID3D11ShaderResourceView*, Count> resources{};
			a_context->PSGetShaderResources(
				a_startSlot,
				static_cast<std::uint32_t>(Count),
				resources.data());
			for (std::size_t index = 0; index < Count; ++index)
				_resources[index].attach(resources[index]);

			_startSlot = a_startSlot;
			_depth = 1;
			return true;
		}

		bool Restore(ID3D11DeviceContext* a_context) noexcept
		{
			if (_depth == 0)
				return false;
			if (_depth > 1) {
				--_depth;
				return true;
			}

			if (a_context) {
				std::array<ID3D11ShaderResourceView*, Count> resources{};
				for (std::size_t index = 0; index < Count; ++index)
					resources[index] = _resources[index].get();
				a_context->PSSetShaderResources(
					_startSlot,
					static_cast<std::uint32_t>(Count),
					resources.data());
			}

			for (auto& resource : _resources)
				resource = nullptr;
			_depth = 0;
			return a_context != nullptr;
		}

	private:
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, Count> _resources;
		std::uint32_t _startSlot = 0;
		std::uint32_t _depth = 0;
	};
}
