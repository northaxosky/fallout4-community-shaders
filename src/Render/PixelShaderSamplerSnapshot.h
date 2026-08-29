#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <d3d11.h>
#include <winrt/base.h>

namespace cs::render
{
	template <std::size_t Count>
	class PixelShaderSamplerSnapshot
	{
	public:
		static_assert(Count > 0);

		PixelShaderSamplerSnapshot() = default;
		PixelShaderSamplerSnapshot(const PixelShaderSamplerSnapshot&) = delete;
		PixelShaderSamplerSnapshot& operator=(const PixelShaderSamplerSnapshot&) = delete;
		PixelShaderSamplerSnapshot(PixelShaderSamplerSnapshot&&) = delete;
		PixelShaderSamplerSnapshot& operator=(PixelShaderSamplerSnapshot&&) = delete;

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

			std::array<ID3D11SamplerState*, Count> samplers{};
			a_context->PSGetSamplers(
				a_startSlot,
				static_cast<std::uint32_t>(Count),
				samplers.data());
			for (std::size_t index = 0; index < Count; ++index)
				_samplers[index].attach(samplers[index]);

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
				std::array<ID3D11SamplerState*, Count> samplers{};
				for (std::size_t index = 0; index < Count; ++index)
					samplers[index] = _samplers[index].get();
				a_context->PSSetSamplers(
					_startSlot,
					static_cast<std::uint32_t>(Count),
					samplers.data());
			}

			for (auto& sampler : _samplers)
				sampler = nullptr;
			_depth = 0;
			return a_context != nullptr;
		}

	private:
		std::array<winrt::com_ptr<ID3D11SamplerState>, Count> _samplers;
		std::uint32_t _startSlot = 0;
		std::uint32_t _depth = 0;
	};
}
