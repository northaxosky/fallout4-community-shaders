#include "SamplerBias.h"

namespace cs::features
{
	namespace
	{
		bool ProbeSamplerState(ID3D11SamplerState* a_sampler) noexcept
		{
			ID3D11SamplerState* queried = nullptr;
			HRESULT result = E_FAIL;
			__try {
				result = a_sampler->QueryInterface(IID_PPV_ARGS(&queried));
				if (queried) {
					queried->Release();
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
			return SUCCEEDED(result) && queried;
		}
	}

	bool SamplerBias::Initialize(std::uintptr_t a_tableAddress) noexcept
	{
		_samplerTable = reinterpret_cast<ID3D11SamplerState**>(a_tableAddress);
		_tableValidated = false;
		return _samplerTable != nullptr;
	}

	bool SamplerBias::ValidateTable() noexcept
	{
		if (_tableValidated) {
			return true;
		}

		std::size_t probes = 0;
		for (std::size_t i = 0; i < kSamplerCount; i++) {
			ID3D11SamplerState* sampler = nullptr;
			__try {
				sampler = _samplerTable[i];
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
			if (!sampler) {
				continue;
			}
			if (!ProbeSamplerState(sampler)) {
				return false;
			}
			if (++probes == 2) {
				_tableValidated = true;
				return true;
			}
		}
		return false;
	}

	bool SamplerBias::Update(float a_mipBias)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return true;
		}
		if (!_samplerTable || !ValidateTable()) {
			return false;
		}

		// Cache the engine originals once so a later call can never capture our biased pointers.
		if (!_originalsCached) {
			for (std::size_t i = 0; i < kSamplerCount; i++) {
				_originalSamplers[i] = _samplerTable[i];
			}
			_originalsCached = true;
		}

		_mipBias = a_mipBias;
		if (_hasBiased && _builtMipBias == a_mipBias) {
			return true;
		}
		_builtMipBias = a_mipBias;

		auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
		for (std::size_t i = 0; i < kSamplerCount; i++) {
			_biasedSamplers[i] = nullptr;
			auto* original = _originalSamplers[i];
			if (!original) {
				continue;
			}
			D3D11_SAMPLER_DESC desc{};
			original->GetDesc(&desc);
			// Leave MaxAnisotropy alone; upstream drops it to 8x for performance, which we did not want.
			if (desc.Filter == D3D11_FILTER_ANISOTROPIC) {
				desc.MipLODBias = a_mipBias;
			}
			DX::ThrowIfFailed(device->CreateSamplerState(&desc, _biasedSamplers[i].put()));
		}
		_hasBiased = true;
		return true;
	}

	void SamplerBias::Override()
	{
		if (!_hasBiased || _mipBias == 0.0f) {
			return;
		}
		if (!_samplerTable) {
			return;
		}
		for (std::size_t i = 0; i < kSamplerCount; i++) {
			_samplerTable[i] = _biasedSamplers[i].get();
		}
		_overridden = true;
	}

	void SamplerBias::Reset()
	{
		if (!_overridden) {
			return;
		}
		_overridden = false;
		if (!_samplerTable) {
			return;
		}
		for (std::size_t i = 0; i < kSamplerCount; i++) {
			_samplerTable[i] = _originalSamplers[i];
		}
	}

	void SamplerBias::Release()
	{
		Reset();
		for (auto& sampler : _biasedSamplers) {
			sampler = nullptr;
		}
		_originalSamplers = {};
		_mipBias = 0.0f;
		_builtMipBias = 1.0f;
		_originalsCached = false;
		_hasBiased = false;
		_tableValidated = false;
	}
}
