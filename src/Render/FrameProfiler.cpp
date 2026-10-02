#include "Render/FrameProfiler.h"

#include "Log.h"
#include "Render/Annotation.h"
#include "Shared/PerfUtils.h"

#include <algorithm>
#include <map>
#include <mutex>

#ifdef TRACY_SUPPORT
#	include <tracy/Tracy.hpp>
#	include <tracy/TracyD3D11.hpp>
#endif

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
#ifdef TRACY_SUPPORT
				if (tracyContext)
					TracyD3D11Destroy(tracyContext);
#endif
			}

			std::mutex mutex;
#ifdef TRACY_SUPPORT
			TracyD3D11Ctx tracyContext{};
#endif
			ID3D11Device* device{};
			ID3D11DeviceContext* context{};
			Profiler profiler;
			bool initialized{};
			bool enabled{};
			bool queriesWanted{};
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
		state.device = a_device;
		state.context = a_context;
#ifdef TRACY_SUPPORT
		if (!state.tracyContext)
			state.tracyContext = TracyD3D11Context(a_device, a_context);
#endif
		annotation::SetProfilerCallbacks(BeginPass, EndPass);
	}

	void MarkEngineFrame(bool a_telemetryEnabled) noexcept
	{
#ifdef TRACY_SUPPORT
		FrameMark;
#endif
		auto& state = State();
		const std::scoped_lock lock{ state.mutex };
#ifdef TRACY_SUPPORT
		if (state.tracyContext)
			TracyD3D11Collect(state.tracyContext);
#endif
		if (!state.activePass.empty()) {
			cs::log::Get("cs.profiler")->error("Pass {} crosses the engine-frame boundary", state.activePass);
			state.profiler.EndPass();
			state.activePass.clear();
		}
		state.profiler.EndFrame();
		state.queriesWanted = a_telemetryEnabled || state.enabled;
		if (state.queriesWanted && state.device && !state.initialized) {
			// Allocate timestamp queries only on the render thread while a consumer needs them.
			state.profiler.Initialize(state.device, state.context);
			state.initialized = true;
		}
		if (state.queriesWanted && state.initialized)
			state.profiler.BeginFrame();
		else if (state.initialized) {
			state.profiler.Release();
			state.initialized = false;
		}
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
	}

	bool BeginPass(std::string_view a_name)
	{
		auto& state = State();
		// FO4: annotation scopes can nest, but the shared profiler requires disjoint passes.
		if (!state.queriesWanted || !state.initialized || !state.activePass.empty())
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
