#include "Render/FrameProfiler.h"

#include "Log.h"
#include "Render/Annotation.h"
#include "Shared/PerfUtils.h"

#include <algorithm>
#include <map>
#include <mutex>

namespace cs::render::profiling
{
	namespace
	{
		struct ProfilerState
		{
			~ProfilerState()
			{
				profiler.EndFrame();
				profiler.Release();
				if (context)
					TracyD3D11Destroy(context);
			}

			std::mutex mutex;
			TracyD3D11Ctx context{};
			Profiler profiler;
			bool initialized{};
			bool enabled{};
			std::string activePass;
			std::map<int, ShaderTiming> shaderFrame;
			std::vector<ShaderTiming> shaderTimings;
			int shaderType = -1;
			double lastDraw{};
			std::uint64_t sequence{};
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
		if (!state.initialized) {
			// FO4: the engine device owns the unchanged shared query profiler.
			state.profiler.Initialize(a_device, a_context);
			state.initialized = true;
			annotation::SetProfilerCallbacks(BeginPass, EndPass);
		}
	}

	void MarkEngineFrame() noexcept
	{
		FrameMark;
		auto& state = State();
		const std::scoped_lock lock{ state.mutex };
		if (state.context)
			TracyD3D11Collect(state.context);
		if (!state.activePass.empty()) {
			cs::log::Get("cs.profiler")->error("Pass {} crosses the engine-frame boundary", state.activePass);
			state.profiler.EndPass();
			state.activePass.clear();
		}
		state.profiler.EndFrame();
		if (state.enabled)
			state.profiler.BeginFrame();
		++state.sequence;
		for (auto& timing : state.shaderTimings) {
			const auto& sample = state.shaderFrame[timing.type];
			timing.calls = timing.calls * 0.95f + sample.calls * 0.05f;
			timing.milliseconds = timing.milliseconds * 0.95f + sample.milliseconds * 0.05f;
		}
		state.shaderFrame.clear();
		state.shaderType = -1;
		state.lastDraw = Util::GetNowSecs();
	}

	Profiler& GetProfiler() noexcept
	{
		return State().profiler;
	}

	void SetEnabled(bool a_enabled) noexcept
	{
		auto& state = State();
		if (state.enabled == a_enabled)
			return;
		state.enabled = a_enabled;
		state.shaderFrame.clear();
		state.shaderType = -1;
		state.lastDraw = Util::GetNowSecs();
		if (!a_enabled) {
			if (!state.activePass.empty())
				state.profiler.EndPass();
			state.profiler.EndFrame();
			state.activePass.clear();
		}
	}

	bool BeginPass(std::string_view a_name)
	{
		auto& state = State();
		// FO4: annotation scopes can nest, but the shared profiler requires disjoint passes.
		if (!state.enabled || !state.initialized || !state.activePass.empty())
			return false;
		state.activePass = a_name;
		state.profiler.BeginPass(state.activePass);
		return true;
	}

	void EndPass() noexcept
	{
		auto& state = State();
		if (state.activePass.empty())
			return;
		state.profiler.EndPass();
		state.activePass.clear();
	}

	void SetShaderFamily(int a_type, std::string_view a_name)
	{
		auto& state = State();
		if (!state.enabled)
			return;
		state.shaderType = a_type;
		// FO4: deferred prepass and lighting share native batch ID 4.
		if (a_type == 4)
			a_name = "DFPrePass / DFLight";
		if (std::ranges::find(state.shaderTimings, a_type, &ShaderTiming::type) == state.shaderTimings.end())
			state.shaderTimings.push_back({ a_type, std::string(a_name) });
	}

	void RecordDraw()
	{
		auto& state = State();
		if (!state.enabled)
			return;
		const double now = Util::GetNowSecs();
		if (state.shaderType >= 0) {
			auto& timing = state.shaderFrame[state.shaderType];
			timing.calls += 1.0f;
			if (state.lastDraw > 0.0)
				timing.milliseconds += static_cast<float>((now - state.lastDraw) * 1000.0);
		}
		state.lastDraw = now;
	}

	std::span<const ShaderTiming> GetShaderTimings() noexcept
	{
		return State().shaderTimings;
	}

	std::uint64_t FrameSequence() noexcept
	{
		return State().sequence;
	}
}
