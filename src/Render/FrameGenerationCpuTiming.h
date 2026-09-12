#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace cs::render
{
	enum class FrameGenerationCpuPhase : std::uint8_t
	{
		kLatencySleep,
		kAcquirePresentInputs,
		kAllocatorFenceWait,
		kCopyRecord,
		kPrepareFrame,
		kSdkPresent,
		kCollectPresentStatus,
		kCount
	};

	struct CpuPhaseTimingStats
	{
		std::uint64_t sampleCount = 0;
		std::uint32_t windowSampleCount = 0;
		double windowMeanMilliseconds = 0.0;
		double windowMaxMilliseconds = 0.0;
	};

	template <std::size_t Capacity = 64>
	class FrameGenerationCpuTimingCollector
	{
	public:
		static_assert(Capacity > 0);
		static constexpr std::size_t kCapacity = Capacity;
		static constexpr std::size_t kPhaseCount =
			static_cast<std::size_t>(FrameGenerationCpuPhase::kCount);
		using Timestamp = std::uint64_t;
		using Clock = Timestamp (*)() noexcept;

		struct Snapshot
		{
			bool available = false;
			std::array<CpuPhaseTimingStats, kPhaseCount> phases{};
			bool frameTimeInputAvailable = false;
			double lastFrameTimeInputMilliseconds = 0.0;
		};

		class Scope
		{
		public:
			Scope() = default;
			Scope(const Scope&) = delete;
			Scope& operator=(const Scope&) = delete;
			~Scope() { Finish(); }

		private:
			friend class FrameGenerationCpuTimingCollector;

			Scope(
				FrameGenerationCpuTimingCollector& a_owner,
				FrameGenerationCpuPhase a_phase,
				Timestamp a_start) noexcept :
				_owner(&a_owner),
				_phase(a_phase),
				_start(a_start)
			{}

			void Finish() noexcept
			{
				if (_owner) {
					_owner->Finish(_phase, _start);
					_owner = nullptr;
				}
			}

			FrameGenerationCpuTimingCollector* _owner = nullptr;
			FrameGenerationCpuPhase _phase =
				FrameGenerationCpuPhase::kLatencySleep;
			Timestamp _start = 0;
		};

		explicit FrameGenerationCpuTimingCollector(
			Clock a_clock = &SteadyClockNow) noexcept :
			_clock(a_clock)
		{}

		void SetEnabled(bool a_enabled) noexcept
		{
			if (_enabled.load(std::memory_order_acquire) == a_enabled) {
				return;
			}
			if (!a_enabled) {
				_enabled.store(false, std::memory_order_release);
			}
			{
				std::scoped_lock lock(_mutex);
				_samples = {};
				_frameTimeInputAvailable = false;
				_lastFrameTimeInputMilliseconds = 0.0;
			}
			if (a_enabled) {
				_enabled.store(true, std::memory_order_release);
			}
		}

		[[nodiscard]] Scope Measure(FrameGenerationCpuPhase a_phase) noexcept
		{
			if (!_enabled.load(std::memory_order_acquire)) {
				return {};
			}
			return Scope(*this, a_phase, _clock());
		}

		void RecordNanoseconds(
			FrameGenerationCpuPhase a_phase,
			Timestamp a_nanoseconds) noexcept
		{
			if (!_enabled.load(std::memory_order_acquire)) {
				return;
			}
			std::scoped_lock lock(_mutex);
			if (!_enabled.load(std::memory_order_relaxed)) {
				return;
			}
			auto& samples = _samples[static_cast<std::size_t>(a_phase)];
			const double milliseconds =
				static_cast<double>(a_nanoseconds) / 1'000'000.0;
			if (samples.count == Capacity) {
				samples.sumMilliseconds -= samples.values[samples.next];
			} else {
				++samples.count;
			}
			samples.values[samples.next] = milliseconds;
			samples.sumMilliseconds += milliseconds;
			samples.next = (samples.next + 1) % Capacity;
			++samples.total;
		}

		void RecordFrameTimeInput(double a_milliseconds) noexcept
		{
			if (!_enabled.load(std::memory_order_acquire)) {
				return;
			}
			std::scoped_lock lock(_mutex);
			if (_enabled.load(std::memory_order_relaxed)) {
				_lastFrameTimeInputMilliseconds = a_milliseconds;
				_frameTimeInputAvailable = true;
			}
		}

		[[nodiscard]] Snapshot GetSnapshot() const noexcept
		{
			std::scoped_lock lock(_mutex);
			Snapshot result;
			if (!_enabled.load(std::memory_order_relaxed)) {
				return result;
			}
			result.available = true;
			result.frameTimeInputAvailable = _frameTimeInputAvailable;
			result.lastFrameTimeInputMilliseconds =
				_lastFrameTimeInputMilliseconds;
			for (std::size_t phase = 0; phase < kPhaseCount; ++phase) {
				const auto& samples = _samples[phase];
				auto& stats = result.phases[phase];
				stats.sampleCount = samples.total;
				stats.windowSampleCount =
					static_cast<std::uint32_t>(samples.count);
				if (samples.count == 0) {
					continue;
				}
				stats.windowMeanMilliseconds =
					samples.sumMilliseconds /
					static_cast<double>(samples.count);
				for (std::size_t index = 0; index < samples.count; ++index) {
					stats.windowMaxMilliseconds =
						(std::max)(
							stats.windowMaxMilliseconds,
							samples.values[index]);
				}
			}
			return result;
		}

	private:
		struct PhaseSamples
		{
			std::array<double, Capacity> values{};
			double sumMilliseconds = 0.0;
			std::uint64_t total = 0;
			std::size_t count = 0;
			std::size_t next = 0;
		};

		static Timestamp SteadyClockNow() noexcept
		{
			return static_cast<Timestamp>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now().time_since_epoch())
					.count());
		}

		void Finish(
			FrameGenerationCpuPhase a_phase,
			Timestamp a_start) noexcept
		{
			if (!_enabled.load(std::memory_order_acquire)) {
				return;
			}
			const auto end = _clock();
			RecordNanoseconds(a_phase, end >= a_start ? end - a_start : 0);
		}

		Clock _clock;
		std::atomic_bool _enabled{ false };
		mutable std::mutex _mutex;
		std::array<PhaseSamples, kPhaseCount> _samples{};
		bool _frameTimeInputAvailable = false;
		double _lastFrameTimeInputMilliseconds = 0.0;
	};
}
