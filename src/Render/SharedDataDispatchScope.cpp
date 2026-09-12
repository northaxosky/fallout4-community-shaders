#include "Render/SharedData.h"

#include "Render/PixelShaderSwapBroker.h"

namespace cs::render
{
	ScopedComputeSharedDataBinding::ScopedComputeSharedDataBinding(
		ID3D11DeviceContext* a_context) noexcept :
		_context(a_context)
	{
		if (!_context || !IsSharedDataReady())
			return;

		ID3D11Buffer* buffers[3]{};
		_context->CSGetConstantBuffers(kSharedDataSlot, 3, buffers);
		for (std::size_t index = 0; index < _buffers.size(); ++index)
			_buffers[index].attach(buffers[index]);

		ID3D11ShaderResourceView* srv = nullptr;
		_context->CSGetShaderResources(
			kSkylightingComputeTextureSlot, 1, &srv);
		_skylightingSRV.attach(srv);
		_active = true;

		BindSharedData(_context, engine::ShaderStage::kCompute);
	}

	ScopedComputeSharedDataBinding::~ScopedComputeSharedDataBinding() noexcept
	{
		if (!_active)
			return;

		ID3D11Buffer* buffers[3] = {
			_buffers[0].get(),
			_buffers[1].get(),
			_buffers[2].get()
		};
		_context->CSSetConstantBuffers(kSharedDataSlot, 3, buffers);
		ID3D11ShaderResourceView* srv = _skylightingSRV.get();
		_context->CSSetShaderResources(
			kSkylightingComputeTextureSlot, 1, &srv);
	}
}
