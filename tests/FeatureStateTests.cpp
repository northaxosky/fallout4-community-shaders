#include "FeatureState.h"
#include "ScreenSpaceGILifecycle.h"

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

	void TestFeatureRequirements()
	{
		using cs::FeatureCapability;
		using cs::FeatureRequirement;

		constexpr FeatureRequirement requirement{
			"ShaderCatalog",
			FeatureCapability::kPixelShaderSwapBroker
		};
		constexpr FeatureRequirement same = requirement;
		constexpr FeatureRequirement other{
			"OtherProvider",
			FeatureCapability::kPixelShaderSwapBroker
		};

		static_assert(requirement == same);
		static_assert(requirement != other);
		CHECK(requirement.provider == "ShaderCatalog");
		CHECK(requirement.capability == FeatureCapability::kPixelShaderSwapBroker);
	}

	void TestRuntimeStateNames()
	{
		using cs::FeatureRuntimeState;
		using cs::FeatureRuntimeStateName;

		static_assert(FeatureRuntimeStateName(FeatureRuntimeState::kPending) == "pending");
		static_assert(FeatureRuntimeStateName(FeatureRuntimeState::kInactive) == "inactive");
		static_assert(FeatureRuntimeStateName(FeatureRuntimeState::kFailed) == "failed");
		static_assert(FeatureRuntimeStateName(FeatureRuntimeState::kActive) == "active");
		static_assert(FeatureRuntimeStateName(FeatureRuntimeState::kDegraded) == "degraded");
		CHECK(FeatureRuntimeStateName(static_cast<FeatureRuntimeState>(-1)) == "unknown");
	}

	void TestActivationResultApplication()
	{
		using cs::ActivationResult;
		using cs::FeatureRuntimeState;

		cs::FeatureState state{
			.installed = true,
			.desiredActive = true
		};

		state.ApplyActivationResult(ActivationResult::Active());
		CHECK(state.installed);
		CHECK(state.desiredActive);
		CHECK(state.runtimeState == FeatureRuntimeState::kActive);
		CHECK(state.detail.empty());

		state.ApplyActivationResult(ActivationResult::Failed("activation failed"));
		CHECK(state.runtimeState == FeatureRuntimeState::kFailed);
		CHECK(state.detail == "activation failed");

		state.ApplyActivationResult(ActivationResult::Degraded("hooks may remain"));
		CHECK(state.runtimeState == FeatureRuntimeState::kDegraded);
		CHECK(state.detail == "hooks may remain");
	}

	void TestScreenSpaceGILifecycle()
	{
		using namespace cs::features::ssgi_lifecycle;

		for (const bool enabled : { false, true }) {
			for (int delivery = 0; delivery <= 2; ++delivery) {
				CHECK(CanBakeAmbientInjection(true, true, true, enabled, delivery));
			}
		}
		CHECK(!CanBakeAmbientInjection(false, true, true, true, 1));
		CHECK(!CanBakeAmbientInjection(true, false, true, true, 1));
		CHECK(!CanBakeAmbientInjection(true, true, false, true, 1));

		CHECK(!UsesDirectAmbientBounce(false, 1));
		CHECK(!UsesDirectAmbientBounce(true, 0));
		CHECK(UsesDirectAmbientBounce(true, 1));
		CHECK(!UsesDirectAmbientBounce(true, 2));
		CHECK(ResolveBounceDelivery(0, false) == 0);
		CHECK(ResolveBounceDelivery(1, false) == 2);
		CHECK(ResolveBounceDelivery(1, true) == 1);
		CHECK(ResolveBounceDelivery(2, false) == 2);

		constexpr auto disabledPlan = InitialTexturePlan(false);
		CHECK(disabledPlan.bakeCritical);
		CHECK(!disabledPlan.enableOnly);
		constexpr auto enabledPlan = InitialTexturePlan(true);
		CHECK(enabledPlan.bakeCritical);
		CHECK(enabledPlan.enableOnly);

		constexpr std::array aoIdentity{ 1.0f, 1.0f, 1.0f, 1.0f };
		constexpr std::array bounceIdentity{ 0.0f, 0.0f, 0.0f, 0.0f };
		CHECK(kAOIdentity == aoIdentity);
		CHECK(kBounceIdentity == bounceIdentity);
	}
}

int main()
{
	TestStatePredicates();
	TestActivationResults();
	TestFeatureRequirements();
	TestRuntimeStateNames();
	TestActivationResultApplication();
	TestScreenSpaceGILifecycle();

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "FeatureState tests passed\n";
	return 0;
}
