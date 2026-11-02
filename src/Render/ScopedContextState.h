#pragma once

#include <d3d11_1.h>
#include <winrt/base.h>

namespace cs::render
{
	// A blank state at the device's feature level; the pass binds everything.
	[[nodiscard]] inline winrt::com_ptr<ID3DDeviceContextState> CreateContextState(ID3D11Device* a_device)
	{
		winrt::com_ptr<ID3D11Device1> device;
		DX::ThrowIfFailed(a_device->QueryInterface(IID_PPV_ARGS(device.put())));
		const auto level = a_device->GetFeatureLevel();
		D3D_FEATURE_LEVEL selected{};
		winrt::com_ptr<ID3DDeviceContextState> state;
		DX::ThrowIfFailed(device->CreateDeviceContextState(
			0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), &selected, state.put()));
		return state;
	}

	class ScopedContextState
	{
	public:
		ScopedContextState(ID3D11DeviceContext* a_context, ID3DDeviceContextState* a_state)
		{
			if (a_context && a_state && SUCCEEDED(a_context->QueryInterface(IID_PPV_ARGS(_context.put()))))
				_context->SwapDeviceContextState(a_state, _previous.put());
		}
		~ScopedContextState()
		{
			if (_previous)
				_context->SwapDeviceContextState(_previous.get(), nullptr);
		}
		ScopedContextState(const ScopedContextState&) = delete;
		ScopedContextState& operator=(const ScopedContextState&) = delete;
		[[nodiscard]] bool IsActive() const noexcept { return _previous != nullptr; }

	private:
		winrt::com_ptr<ID3D11DeviceContext1> _context;
		winrt::com_ptr<ID3DDeviceContextState> _previous;
	};
}
