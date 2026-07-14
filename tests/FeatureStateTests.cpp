#include "FeatureState.h"

#include <iostream>
#include <string>
#include <string_view>

namespace
{
	int failures = 0;

	void Check(bool a_condition, std::string_view a_expression, int a_line)
	{
		if (!a_condition) {
			std::cerr << "CHECK failed at line " << a_line << ": " << a_expression << '\n';
			++failures;
		}
	}

#define CHECK(a_expression) Check(static_cast<bool>(a_expression), #a_expression, __LINE__)

	void TestStatePredicates()
	{
		using cs::FeatureRuntimeState;

		cs::FeatureState state;
		CHECK(!state.installed);
		CHECK(!state.desiredActive);
		CHECK(state.runtimeState == FeatureRuntimeState::kPending);
		CHECK(state.detail.empty());
		CHECK(!state.IsActive());
		CHECK(!state.IsDegraded());
		CHECK(!state.IsHealthy());

		state.installed = true;
		state.desiredActive = true;
		state.runtimeState = FeatureRuntimeState::kInactive;
		CHECK(!state.IsActive());
		CHECK(!state.IsDegraded());
		CHECK(!state.IsHealthy());

		state.runtimeState = FeatureRuntimeState::kFailed;
		CHECK(!state.IsActive());
		CHECK(!state.IsDegraded());
		CHECK(!state.IsHealthy());

		state.runtimeState = FeatureRuntimeState::kActive;
		CHECK(state.IsActive());
		CHECK(!state.IsDegraded());
		CHECK(state.IsHealthy());

		state.runtimeState = FeatureRuntimeState::kDegraded;
		CHECK(state.IsActive());
		CHECK(state.IsDegraded());
		CHECK(!state.IsHealthy());

		state.installed = false;
		state.desiredActive = false;
		CHECK(state.IsActive());
		CHECK(state.IsDegraded());
		CHECK(!state.IsHealthy());
	}

	void TestActivationResults()
	{
		using cs::ActivationOutcome;
		using cs::ActivationResult;

		const auto active = ActivationResult::Active();
		CHECK(active.GetOutcome() == ActivationOutcome::kActive);
		CHECK(active.GetDetail().empty());

		const auto failed = ActivationResult::Failed("missing dependency");
		CHECK(failed.GetOutcome() == ActivationOutcome::kFailed);
		CHECK(failed.GetDetail() == "missing dependency");

		const auto degraded = ActivationResult::Degraded("hooks may remain");
		CHECK(degraded.GetOutcome() == ActivationOutcome::kDegraded);
		CHECK(degraded.GetDetail() == "hooks may remain");
	}
}

int main()
{
	TestStatePredicates();
	TestActivationResults();

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "FeatureState tests passed\n";
	return 0;
}
