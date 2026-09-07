#include "Render/FrameProfiler.h"

#include <mutex>

namespace cs::render::profiling
{
	namespace
	{
		struct ProfilerState
		{
			~ProfilerState()
			{
				if (context)
					TracyD3D11Destroy(context);
			}

			std::mutex mutex;
			TracyD3D11Ctx context{};
		};

		ProfilerState& State()
		{
			static ProfilerState state;
			return state;
		}
	}

	void InitializeD3D11(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context) noexcept
	{
		if (!a_device || !a_context)
			return;
		auto& state = State();
		const std::scoped_lock lock{ state.mutex };
		if (!state.context)
			state.context = TracyD3D11Context(a_device, a_context);
	}

	void MarkEngineFrame() noexcept
	{
		FrameMark;
		auto& state = State();
		const std::scoped_lock lock{ state.mutex };
		if (state.context)
			TracyD3D11Collect(state.context);
	}
}
