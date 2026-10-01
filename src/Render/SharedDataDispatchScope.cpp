#include "Render/SharedData.h"

#include "Render/PixelShaderSwapBroker.h"

namespace cs::render
{
	void SubstrateBindingSnapshot::Save(ID3D11DeviceContext* a_context, engine::ShaderStage a_stage) noexcept
	{
		ID3D11Buffer* buffers[kSubstrateBufferCount]{};
		ID3D11ShaderResourceView* depth = nullptr;
		switch (a_stage) {
		case engine::ShaderStage::kVertex:
			a_context->VSGetConstantBuffers(kFrameDataSlot, kSubstrateBufferCount, buffers);
			a_context->VSGetShaderResources(kCanonicalDepthSlot, 1, &depth);
			break;
		case engine::ShaderStage::kPixel:
			a_context->PSGetConstantBuffers(kFrameDataSlot, kSubstrateBufferCount, buffers);
			a_context->PSGetShaderResources(kCanonicalDepthSlot, 1, &depth);
			_debugTexture = nullptr;
			a_context->PSGetShaderResources(kFullscreenDebugTextureSlot, 1, _debugTexture.put());
			break;
		case engine::ShaderStage::kCompute:
			a_context->CSGetConstantBuffers(kFrameDataSlot, kSubstrateBufferCount, buffers);
			a_context->CSGetShaderResources(kCanonicalDepthSlot, 1, &depth);
			break;
		case engine::ShaderStage::kCount:
			return;
		}
		for (std::size_t index = 0; index < _buffers.size(); ++index)
			_buffers[index].attach(buffers[index]);
		_depth.attach(depth);
	}

	void SubstrateBindingSnapshot::Restore(ID3D11DeviceContext* a_context, engine::ShaderStage a_stage) noexcept
	{
		ID3D11Buffer* buffers[kSubstrateBufferCount]{};
		for (std::size_t index = 0; index < _buffers.size(); ++index)
			buffers[index] = _buffers[index].get();
		auto* depth = _depth.get();
		auto* debugTexture = _debugTexture.get();
		switch (a_stage) {
		case engine::ShaderStage::kVertex:
			a_context->VSSetConstantBuffers(kFrameDataSlot, kSubstrateBufferCount, buffers);
			a_context->VSSetShaderResources(kCanonicalDepthSlot, 1, &depth);
			break;
		case engine::ShaderStage::kPixel:
			a_context->PSSetConstantBuffers(kFrameDataSlot, kSubstrateBufferCount, buffers);
			a_context->PSSetShaderResources(kCanonicalDepthSlot, 1, &depth);
			a_context->PSSetShaderResources(kFullscreenDebugTextureSlot, 1, &debugTexture);
			break;
		case engine::ShaderStage::kCompute:
			a_context->CSSetConstantBuffers(kFrameDataSlot, kSubstrateBufferCount, buffers);
			a_context->CSSetShaderResources(kCanonicalDepthSlot, 1, &depth);
			break;
		case engine::ShaderStage::kCount:
			break;
		}
		for (auto& buffer : _buffers)
			buffer = nullptr;
		_depth = nullptr;
		_debugTexture = nullptr;
	}

	ScopedComputeSharedDataBinding::ScopedComputeSharedDataBinding(
		ID3D11DeviceContext* a_context) noexcept :
		_context(a_context)
	{
		if (!_context || !IsSharedDataReady())
			return;

		_bindings.Save(_context, engine::ShaderStage::kCompute);
		_active = true;

		BindSharedData(_context, engine::ShaderStage::kCompute);
	}

	ScopedComputeSharedDataBinding::~ScopedComputeSharedDataBinding() noexcept
	{
		if (!_active)
			return;

		_bindings.Restore(_context, engine::ShaderStage::kCompute);
	}
}
