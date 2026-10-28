#include "Render/SharedData.h"

#include "Render/FrameBindings.h"

namespace cs::render
{
	void SubstrateBindingSnapshot::Save(ID3D11DeviceContext* a_context, engine::ShaderStage a_stage) noexcept
	{
		if (a_stage == engine::ShaderStage::kCount)
			return;
		ID3D11Buffer* buffers[kSubstrateBufferCount]{};
		ID3D11ShaderResourceView* depth = nullptr;
		engine::GetStageBindings(a_context, a_stage, kFrameDataSlot, kSubstrateBufferCount, buffers);
		engine::GetStageBindings(a_context, a_stage, kCanonicalDepthSlot, 1, &depth);
		if (a_stage == engine::ShaderStage::kPixel) {
			_debugTexture = nullptr;
			engine::GetStageBindings(a_context, a_stage, kFullscreenDebugTextureSlot, 1, _debugTexture.put());
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
		engine::SetStageBindings(a_context, a_stage, kFrameDataSlot, kSubstrateBufferCount, buffers);
		engine::SetStageBindings(a_context, a_stage, kCanonicalDepthSlot, 1, &depth);
		if (a_stage == engine::ShaderStage::kPixel)
			engine::SetStageBindings(a_context, a_stage, kFullscreenDebugTextureSlot, 1, &debugTexture);
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
