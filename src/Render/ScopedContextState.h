#pragma once

#include <d3d11_1.h>
#include <winrt/base.h>

namespace cs::render
{
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
