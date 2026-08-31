#include "SamplerBias.h"

#include <format>

#include "Render/Annotation.h"

namespace cs::features
{
	bool SamplerBias::Initialize(std::uintptr_t a_tableAddress) noexcept
	{
		_samplerTable = reinterpret_cast<ID3D11SamplerState**>(a_tableAddress);
		return _samplerTable != nullptr;
	}

	bool SamplerBias::CacheOriginalsIfTableLive() noexcept
	{
		for (std::size_t i = 0; i < kSamplerCount; i++) {
			ID3D11SamplerState* sampler = nullptr;
			__try {
				sampler = _samplerTable[i];
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
			if (!sampler) {
				return false;
			}
			if (_originalsCached && sampler != _originalSamplers[i]) {
				return false;
			}
			if (!_originalsCached) {
				_originalSamplers[i] = sampler;
			}
		}
		_originalsCached = true;
		return true;
	}

	bool SamplerBias::Update(float a_mipBias)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData || !rendererData->device || !_samplerTable ||
			!CacheOriginalsIfTableLive()) {
			return false;
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
			cs::render::annotation::SetName(
				_biasedSamplers[i].get(),
				std::format("Upscaling/BiasedSampler[{}].Sampler", i));
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
		if (!CacheOriginalsIfTableLive()) {
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
	}
}
