#include "Render/FrameProfiler.h"

#include "Feature.h"
#include "Log.h"
#include "Render/Annotation.h"
#include "Render/RenderHooks.h"
#include "Utils/PerfUtils.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <utility>

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

#ifdef TRACY_SUPPORT
			TracyD3D11Ctx tracyContext{};
#endif
			ID3D11Device* device{};
			ID3D11DeviceContext* context{};
			Profiler profiler;
			bool initialized{};
			bool enabled{};
			std::vector<std::string> passStack;
			std::map<int, ShaderTiming> shaderFrame;
			std::vector<ShaderTiming> shaderTimings;
			int shaderType = -1;
			double lastDraw{};
			std::atomic<std::uint64_t> sequence{};
		};

		ProfilerState& State()
		{
			static ProfilerState state;
			return state;
		}

		// Rows of switched-off or suspended features read 0 instead of their last cost.
		void MarkSwitchedOffPasses(Profiler& a_profiler)
		{
			const auto& features = FeatureManager::Get().GetRegisteredFeatures();
			for (const auto& result : a_profiler.GetResults()) {
				const auto group = GroupOf(result.name);
				const auto feature = std::ranges::find_if(features, [&](const Feature* a_feature) {
					return a_feature && a_feature->GetName() == group;
				});
				if (feature != features.end() && !(*feature)->IsEnabled())
					a_profiler.MarkPassSkipped(result.name);
			}
		}
	}

	void InitializeD3D11(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context) noexcept
	{
		if (!a_device || !a_context)
			return;
		auto& state = State();
		state.device = a_device;
		state.context = a_context;
#ifdef TRACY_SUPPORT
		if (!state.tracyContext)
			state.tracyContext = TracyD3D11Context(a_device, a_context);
#endif
		annotation::SetProfilerCallbacks(BeginPass, EndPass);
	}

	void MarkEngineFrame() noexcept
	{
#ifdef TRACY_SUPPORT
		FrameMark;
#endif
		auto& state = State();
#ifdef TRACY_SUPPORT
		if (state.tracyContext)
			TracyD3D11Collect(state.tracyContext);
#endif
		if (!state.passStack.empty()) {
			for (const auto& pass : state.passStack)
				cs::log::Get("cs.profiler")->error("Pass {} crosses the engine-frame boundary", pass);
			// Only the top pass is open; its parents already ended.
			state.profiler.EndPass();
			state.passStack.clear();
		}
		state.profiler.EndFrame();
		const bool wanted = state.profiler.IsActive();
		if (wanted && state.device && !state.initialized) {
			// Allocate timestamp queries only on the render thread while a consumer needs them.
			state.profiler.Initialize(state.device, state.context);
			state.initialized = true;
		}
		if (wanted && state.initialized) {
			state.profiler.BeginFrame();
			MarkSwitchedOffPasses(state.profiler);
		} else if (state.initialized) {
			state.profiler.Release();
			state.initialized = false;
		}
		++state.sequence;
		state.shaderType = -1;
		// Per-shader timings are collected only while the overlay wants them.
		if (!state.enabled)
			return;
		for (auto& timing : state.shaderTimings) {
			const auto& sample = state.shaderFrame[timing.type];
			timing.calls = timing.calls * 0.95f + sample.calls * 0.05f;
			timing.milliseconds = timing.milliseconds * 0.95f + sample.milliseconds * 0.05f;
		}
		state.shaderFrame.clear();
		state.lastDraw = Util::GetNowSecs();
	}

	Profiler& GetProfiler() noexcept
	{
		return State().profiler;
	}

	void KeepActive(std::uint32_t a_frames) noexcept
	{
		State().profiler.KeepActive(a_frames);
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
		if (!state.initialized || !state.profiler.IsActive())
			return false;
		// Exclusive time: the parent pauses and resumes under its own name.
		if (!state.passStack.empty())
			state.profiler.EndPass();
		state.passStack.emplace_back(a_name);
		state.profiler.BeginPass(state.passStack.back());
		return true;
	}

	void EndPass() noexcept
	{
		auto& state = State();
		if (state.passStack.empty())
			return;
		state.profiler.EndPass();
		state.passStack.pop_back();
		if (!state.passStack.empty())
			state.profiler.BeginPass(state.passStack.back());
	}

	bool InstallDeferredCompositePass()
	{
		// Stock composite only; feature callbacks stay outside the pass.
		// Timestamp queries touch no bindings, so both hooks only observe.
		static annotation::SpannedEvent event;
		constexpr auto kPass = "DeferredComposite";
		return engine::RegisterPostDeferredComposite(
				   [] {
					   event.End();
				   },
				   engine::HookPriority::InnermostPost) &&
		       engine::RegisterPreDeferredComposite(
				   [] {
					   event.Begin(kPass);
				   },
				   engine::HookPriority::InnermostPre);
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
		return State().sequence.load(std::memory_order_relaxed);
	}
}
