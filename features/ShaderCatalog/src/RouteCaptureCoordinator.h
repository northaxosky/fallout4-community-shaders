#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace cs::features::catalog::route_capture
{
	using Clock = std::chrono::steady_clock;

	inline constexpr int kDefaultCaptureSeconds = 90;
	inline constexpr int kMinCaptureSeconds = 1;
	inline constexpr int kMaxCaptureSeconds = 3600;
	// One budget for reaching the publication decision and for bounding every fallback wait.
	inline constexpr Clock::duration kFinalizationBudget =
		std::chrono::seconds(30);

	[[nodiscard]] constexpr bool IsValidCaptureSeconds(
		std::int64_t a_seconds) noexcept
	{
		return a_seconds >= kMinCaptureSeconds
			&& a_seconds <= kMaxCaptureSeconds;
	}

	enum class State
	{
		kInactive,
		kCapturing,
		kClosing,
		kFinalizedInert,
		kAborted
	};

	enum class CloseReason
	{
		kNone,
		kCaptureDeadline,
		kUserRequest,
		kProcessExit
	};

	const char* StateName(State a_state) noexcept;
	const char* CloseReasonName(CloseReason a_reason) noexcept;

	struct Status
	{
		State state = State::kInactive;
		CloseReason reason = CloseReason::kNone;
		bool deadlineArmed = false;
		bool finalizationTimedOut = false;
		bool authoritative = false;
		std::int64_t remainingCaptureSeconds = 0;
	};

	// Time and wait seam so capture timing stays deterministic under test.
	class TimeSource
	{
	public:
		virtual ~TimeSource() = default;
		virtual Clock::time_point Now() const noexcept = 0;
		// Publishes the coordinator wait channel; only a deterministic seam needs it.
		virtual void Attach(
			std::mutex& a_mutex,
			std::condition_variable& a_signal) noexcept
		{
			(void)a_mutex;
			(void)a_signal;
		}
		virtual void WaitUntil(
			std::unique_lock<std::mutex>& a_lock,
			std::condition_variable& a_signal,
			Clock::time_point a_deadline) = 0;
	};

	TimeSource& SteadyTimeSource() noexcept;

	// Performs the sole CatalogDB::Stop() plus feature teardown and reports run authority.
	using CloseAction = std::function<bool()>;

	class Coordinator
	{
	public:
		explicit Coordinator(TimeSource& a_time = SteadyTimeSource());
		~Coordinator();
		Coordinator(const Coordinator&) = delete;
		Coordinator& operator=(const Coordinator&) = delete;

		static Coordinator& Get();

		bool Begin(CloseAction a_close);
		// Refuses all later service after a failed startup; only valid before a close begins.
		bool AbortBeforeClose() noexcept;
		// Arms once, and only after complete hook coverage is established.
		bool ArmCaptureDeadline(std::chrono::seconds a_duration) noexcept;
		// Requesters only coalesce; the close never runs on a requester thread.
		void RequestClose(CloseReason a_reason) noexcept;
		bool WaitForTerminal(Clock::duration a_budget) noexcept;
		// One-way veto decision: the only authority latch, and it never reopens.
		bool SealFinalizationDecision() noexcept;
		[[nodiscard]] Status Snapshot() const noexcept;
		// Lock-free coherent publication; the only status a telemetry reader may use.
		[[nodiscard]] Status TelemetrySnapshot() const noexcept;

	private:
		enum class Decision
		{
			kOpen,
			kLatched,
			kSealed
		};

		void Worker() noexcept;
		void BeginCloseLocked(CloseReason a_reason) noexcept;
		[[nodiscard]] Status StatusLocked() const noexcept;
		void PublishStatusLocked() const noexcept;

		TimeSource& _time;
		mutable std::mutex _mutex;
		std::condition_variable _signal;
		std::thread _worker;
		CloseAction _close;
		State _state = State::kInactive;
		CloseReason _reason = CloseReason::kNone;
		Decision _decision = Decision::kOpen;
		bool _deadlineArmed = false;
		bool _abandoned = false;
		bool _authoritative = false;
		Clock::time_point _captureDeadline = Clock::time_point::max();
		Clock::time_point _closeDeadline = Clock::time_point::max();
		// One packed word so telemetry never locks or reads mutable state.
		mutable std::atomic<std::uint32_t> _telemetryStatus{ 0 };
	};

	bool SealProcessFinalizationDecision() noexcept;
}
