#pragma once

#include <d3d11.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include <winrt/base.h>

namespace cs::features
{
	class SamplerBias
	{
	public:
		bool Initialize(std::uintptr_t a_tableAddress) noexcept;
		bool Update(float a_mipBias);

		void Override();
		void Reset();

		void Release();

	private:
		static constexpr std::size_t kSamplerCount = 320;

		bool ValidateTable() noexcept;

		ID3D11SamplerState** _samplerTable = nullptr;
		std::array<ID3D11SamplerState*, kSamplerCount>                _originalSamplers{};
		std::array<winrt::com_ptr<ID3D11SamplerState>, kSamplerCount> _biasedSamplers{};

		float _mipBias = 0.0f;
		float _builtMipBias = 1.0f;  // sentinel above any real bias forces the first build
		bool  _originalsCached = false;
		bool  _hasBiased = false;
		bool  _overridden = false;
		bool  _tableValidated = false;
	};
}
