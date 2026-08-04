#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace cs
{
	enum class EnbPolicy
	{
		kRunAnyway,
		kDeactivate
	};

	enum class FeatureRuntimeState
	{
		kPending,
		kInactive,
		kFailed,
		kActive,
		kDegraded
	};

	constexpr std::string_view FeatureRuntimeStateName(FeatureRuntimeState a_state) noexcept
	{
		switch (a_state) {
		case FeatureRuntimeState::kPending:
			return "pending";
		case FeatureRuntimeState::kInactive:
			return "inactive";
		case FeatureRuntimeState::kFailed:
			return "failed";
		case FeatureRuntimeState::kActive:
			return "active";
		case FeatureRuntimeState::kDegraded:
			return "degraded";
		}
		return "unknown";
	}

	enum class ActivationOutcome
	{
		kFailed,
		kActive,
		kDegraded
	};

	class ActivationResult
	{
	public:
		static ActivationResult Failed(std::string a_detail)
		{
			return { ActivationOutcome::kFailed, std::move(a_detail) };
		}

		static ActivationResult Active()
		{
			return { ActivationOutcome::kActive, {} };
		}

		static ActivationResult Degraded(std::string a_detail)
		{
			return { ActivationOutcome::kDegraded, std::move(a_detail) };
		}

		ActivationOutcome GetOutcome() const noexcept { return _outcome; }
		const std::string& GetDetail() const noexcept { return _detail; }

	private:
		ActivationResult(ActivationOutcome a_outcome, std::string a_detail) :
			_outcome(a_outcome),
			_detail(std::move(a_detail))
		{}

		ActivationOutcome _outcome;
		std::string       _detail;
	};

	struct FeatureState
	{
		bool installed{ false };
		bool desiredActive{ false };
		FeatureRuntimeState runtimeState{ FeatureRuntimeState::kPending };
		std::string detail;

		bool IsActive() const noexcept
		{
			return runtimeState == FeatureRuntimeState::kActive || runtimeState == FeatureRuntimeState::kDegraded;
		}

		bool IsDegraded() const noexcept
		{
			return runtimeState == FeatureRuntimeState::kDegraded;
		}

		bool IsHealthy() const noexcept
		{
			return runtimeState == FeatureRuntimeState::kActive;
		}

		void ApplyActivationResult(const ActivationResult& a_result)
		{
			switch (a_result.GetOutcome()) {
			case ActivationOutcome::kFailed:
				runtimeState = FeatureRuntimeState::kFailed;
				break;
			case ActivationOutcome::kActive:
				runtimeState = FeatureRuntimeState::kActive;
				break;
			case ActivationOutcome::kDegraded:
				runtimeState = FeatureRuntimeState::kDegraded;
				break;
			}
			detail = a_result.GetDetail();
		}
	};
}
