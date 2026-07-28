#include "RouteCaptureCoordinator.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace cs::features::catalog::route_capture
{
	namespace
	{
		constexpr std::uint32_t kStatusStateMask = 0x7u;
		constexpr std::uint32_t kStatusReasonShift = 3;
		constexpr std::uint32_t kStatusReasonMask = 0x7u;
		constexpr std::uint32_t kStatusDeadlineArmedBit = 1u << 6;
		constexpr std::uint32_t kStatusTimedOutBit = 1u << 7;
		constexpr std::uint32_t kStatusAuthoritativeBit = 1u << 8;
		constexpr std::uint32_t kStatusRemainingShift = 16;
		constexpr std::uint32_t kStatusRemainingMask = 0xFFFFu;

		std::uint32_t EncodeStatus(const Status& a_status) noexcept
		{
			const auto remaining = static_cast<std::uint32_t>(
				std::clamp<std::int64_t>(
					a_status.remainingCaptureSeconds, 0, kStatusRemainingMask));
			return (static_cast<std::uint32_t>(a_status.state)
					   & kStatusStateMask)
				| ((static_cast<std::uint32_t>(a_status.reason)
					   & kStatusReasonMask)
					<< kStatusReasonShift)
				| (a_status.deadlineArmed ? kStatusDeadlineArmedBit : 0u)
				| (a_status.finalizationTimedOut ? kStatusTimedOutBit : 0u)
				| (a_status.authoritative ? kStatusAuthoritativeBit : 0u)
				| (remaining << kStatusRemainingShift);
		}

		Status DecodeStatus(std::uint32_t a_encoded) noexcept
		{
			Status status;
			status.state = static_cast<State>(
				a_encoded & kStatusStateMask);
			status.reason = static_cast<CloseReason>(
				(a_encoded >> kStatusReasonShift) & kStatusReasonMask);
			status.deadlineArmed =
				(a_encoded & kStatusDeadlineArmedBit) != 0;
			status.finalizationTimedOut =
				(a_encoded & kStatusTimedOutBit) != 0;
			status.authoritative =
				(a_encoded & kStatusAuthoritativeBit) != 0;
			status.remainingCaptureSeconds =
				(a_encoded >> kStatusRemainingShift)
				& kStatusRemainingMask;
			return status;
		}

		class SteadyTimeSourceImpl final : public TimeSource
		{
		public:
			Clock::time_point Now() const noexcept override
			{
				return Clock::now();
			}

			void WaitUntil(
				std::unique_lock<std::mutex>& a_lock,
				std::condition_variable& a_signal,
				Clock::time_point a_deadline) override
			{
				if (a_deadline == Clock::time_point::max())
					a_signal.wait(a_lock);
				else
					a_signal.wait_until(a_lock, a_deadline);
			}
		};
	}

	const char* StateName(State a_state) noexcept
	{
		switch (a_state) {
		case State::kCapturing:
			return "capturing";
		case State::kClosing:
			return "closing";
		case State::kFinalizedInert:
			return "finalized_inert";
		case State::kAborted:
			return "aborted";
		case State::kInactive:
		default:
			return "inactive";
		}
	}

	const char* CloseReasonName(CloseReason a_reason) noexcept
	{
		switch (a_reason) {
		case CloseReason::kCaptureDeadline:
			return "capture-deadline";
		case CloseReason::kUserRequest:
			return "user-request";
		case CloseReason::kProcessExit:
			return "process-exit";
		case CloseReason::kNone:
		default:
			return "none";
		}
	}

	TimeSource& SteadyTimeSource() noexcept
	{
		static SteadyTimeSourceImpl source;
		return source;
	}

	Coordinator::Coordinator(TimeSource& a_time) :
		_time(a_time)
	{
		_time.Attach(_mutex, _signal);
	}

	Coordinator::~Coordinator()
	{
		{
			std::scoped_lock lock(_mutex);
			_abandoned = true;
		}
		_signal.notify_all();
		if (_worker.joinable())
			_worker.join();
	}

	Coordinator& Coordinator::Get()
	{
		// Leaked on purpose so process-exit requesters always see a live coordinator.
		static auto* instance = new Coordinator();
		return *instance;
	}

	bool Coordinator::Begin(CloseAction a_close)
	{
		if (!a_close)
			return false;
		std::scoped_lock lock(_mutex);
		if (_state != State::kInactive || _worker.joinable())
			return false;
		_close = std::move(a_close);
		try {
			_worker = std::thread(&Coordinator::Worker, this);
		} catch (...) {
			_close = {};
			return false;
		}
		_state = State::kCapturing;
		PublishStatusLocked();
		return true;
	}

	bool Coordinator::AbortBeforeClose() noexcept
	{
		std::thread worker;
		{
			std::scoped_lock lock(_mutex);
			if (_state == State::kClosing
				|| _state == State::kFinalizedInert)
				return false;
			_state = State::kAborted;
			_close = {};
			_abandoned = true;
			_deadlineArmed = false;
			_captureDeadline = Clock::time_point::max();
			worker = std::move(_worker);
			PublishStatusLocked();
		}
		_signal.notify_all();
		if (worker.joinable())
			worker.join();
		return true;
	}

	bool Coordinator::ArmCaptureDeadline(
		std::chrono::seconds a_duration) noexcept
	{
		if (!IsValidCaptureSeconds(a_duration.count()))
			return false;
		{
			std::scoped_lock lock(_mutex);
			if (_state != State::kCapturing || _deadlineArmed)
				return false;
			_captureDeadline = _time.Now() + a_duration;
			_deadlineArmed = true;
			PublishStatusLocked();
		}
		_signal.notify_all();
		return true;
	}

	void Coordinator::RequestClose(CloseReason a_reason) noexcept
	{
		{
			std::scoped_lock lock(_mutex);
			if (_state != State::kCapturing)
				return;
			BeginCloseLocked(
				a_reason == CloseReason::kNone
					? CloseReason::kUserRequest
					: a_reason);
		}
		_signal.notify_all();
	}

	bool Coordinator::WaitForTerminal(Clock::duration a_budget) noexcept
	{
		const auto pending = [this]() noexcept {
			return _state == State::kCapturing
				|| _state == State::kClosing;
		};
		std::unique_lock lock(_mutex);
		if (pending()) {
			const auto limit = _time.Now()
				+ std::max(a_budget, Clock::duration::zero());
			while (pending() && _time.Now() < limit)
				_time.WaitUntil(lock, _signal, limit);
			PublishStatusLocked();
		}
		return _state == State::kFinalizedInert;
	}

	bool Coordinator::SealFinalizationDecision() noexcept
	{
		std::scoped_lock lock(_mutex);
		if (_decision == Decision::kOpen) {
			_decision = _time.Now() >= _closeDeadline
				? Decision::kLatched
				: Decision::kSealed;
			PublishStatusLocked();
		}
		return _decision == Decision::kLatched;
	}

	Status Coordinator::Snapshot() const noexcept
	{
		Status status;
		try {
			std::scoped_lock lock(_mutex);
			status = StatusLocked();
			_telemetryStatus.store(
				EncodeStatus(status), std::memory_order_release);
		} catch (...) {
		}
		return status;
	}

	Status Coordinator::TelemetrySnapshot() const noexcept
	{
		return DecodeStatus(
			_telemetryStatus.load(std::memory_order_acquire));
	}

	Status Coordinator::StatusLocked() const noexcept
	{
		Status status;
		status.state = _state;
		status.reason = _reason;
		status.deadlineArmed = _deadlineArmed;
		status.authoritative = _authoritative;
		// Observers derive an overdue open decision; only the seal latches it.
		status.finalizationTimedOut =
			_decision == Decision::kLatched
			|| (_decision == Decision::kOpen
				&& _state == State::kClosing
				&& _time.Now() >= _closeDeadline);
		if (_state == State::kCapturing && _deadlineArmed) {
			const auto remaining = _captureDeadline - _time.Now();
			status.remainingCaptureSeconds = std::max<std::int64_t>(
				0,
				std::chrono::duration_cast<std::chrono::seconds>(
					remaining)
					.count());
		}
		return status;
	}

	void Coordinator::PublishStatusLocked() const noexcept
	{
		_telemetryStatus.store(
			EncodeStatus(StatusLocked()), std::memory_order_release);
	}

	void Coordinator::BeginCloseLocked(CloseReason a_reason) noexcept
	{
		_state = State::kClosing;
		_reason = a_reason;
		_closeDeadline = _time.Now() + kFinalizationBudget;
		PublishStatusLocked();
	}

	void Coordinator::Worker() noexcept
	{
		std::unique_lock lock(_mutex);
		while (_state == State::kCapturing && !_abandoned) {
			if (_deadlineArmed && _time.Now() >= _captureDeadline) {
				BeginCloseLocked(CloseReason::kCaptureDeadline);
				break;
			}
			_time.WaitUntil(
				lock,
				_signal,
				_deadlineArmed
					? _captureDeadline
					: Clock::time_point::max());
			PublishStatusLocked();
		}
		if (_state != State::kClosing)
			return;

		auto close = std::move(_close);
		_close = {};
		lock.unlock();
		bool authoritative = false;
		try {
			authoritative = close();
		} catch (...) {
			authoritative = false;
		}
		lock.lock();
		_authoritative = authoritative;
		_state = State::kFinalizedInert;
		PublishStatusLocked();
		lock.unlock();
		_signal.notify_all();
	}

	bool SealProcessFinalizationDecision() noexcept
	{
		return Coordinator::Get().SealFinalizationDecision();
	}
}
