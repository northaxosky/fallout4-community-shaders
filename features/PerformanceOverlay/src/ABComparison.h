#pragma once

#include "Feature.h"
#include "Settings/FeatureConfig.h"
#include <Features/PerformanceOverlay/ABTesting/ABTestAggregator.h>

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cs::features::performance_overlay
{
	// Alternates two states in timed windows; stopping always restores B.
	class ABComparison
	{
	public:
		enum class Validity : std::uint8_t
		{
			kValid,
			kMarginal,
			kInsufficient
		};

		// Total frame time; delta is B minus A, set only when comparable.
		struct Summary
		{
			bool comparable{};
			float meanA{}, meanB{}, medianA{}, medianB{}, delta{};
			float fpsA{}, fpsB{};
			int frames{}, excluded{}, windows{};
			float duration{}, validPercent{};
			Validity validity{ Validity::kInsufficient };
		};

		struct Progress
		{
			bool running{}, restorePending{}, costRun{};
			int window{}, windows{};
			bool variantB{};
			double windowSecondsLeft{};
		};

		static constexpr std::string_view ValidityName(Validity a_validity) noexcept
		{
			return a_validity == Validity::kValid ? "Valid" : a_validity == Validity::kMarginal ? "Marginal" :
			                                                                                      "Insufficient";
		}

		explicit ABComparison(const Feature& a_owner) :
			_owner(a_owner) {}

		bool Testing() const noexcept { return _testing; }
		bool CostRunning() const noexcept { return _testing && _costRun; }
		// The current or last run was a cost run, so its summary is a cost.
		bool LastRunWasCost() const noexcept { return _costRun; }
		bool RestorePending() const noexcept { return _restorePending; }
		// Per-type rows need draw profiling; a cost run reads only the Total row.
		bool WantsDrawTimings() const noexcept { return _testing && !_costRun; }
		Progress GetProgress() const noexcept;
		const std::string& Error() const noexcept { return _testError; }
		// Live aggregation; LastSummary is the one frozen when the run stopped.
		std::optional<Summary> Summarize() const;
		const std::optional<Summary>& LastSummary() const noexcept { return _lastSummary; }
		void CaptureBaseline();
		// Zero restores B and ends the run; throws if restoration fails.
		void SetInterval(int);
		// Custom shaders on (B) against off (A); throws when they are already off.
		void StartCostRun();
		void Abort(std::string_view a_reason) noexcept;
		void Fail(std::string a_message);
		// Runs the action, returning a thrown error's message.
		static std::expected<void, std::string> Try(const std::function<void()>& a_action);
		std::string CostHelp() const;
		// Throws when a compared feature stops being healthy or a state cannot apply.
		void Update(double a_now, const std::vector<DrawCallRow>& a_rows);
		void DrawControls();
		void DrawCostControls();
		void DrawResults();

	private:
		using LiveSettings = std::map<Feature*, toml::table>;
		struct State
		{
			LiveSettings live;
			feature_config::ShaderOwnershipConfig ownership;
			bool operator==(const State&) const = default;
		};
		static State Capture();
		static void Apply(const State&, bool a_restoring = false);
		// A zero window limit runs until stopped.
		void Begin(State a_a, State a_b, std::string a_labelA, std::string a_labelB, int a_interval, int a_windowLimit);
		void Attempt(const std::function<void()>& a_action);
		void LogCostSummary() const;

		const Feature& _owner;
		State _baseline, _a, _b;
		std::string _labelA = "USER", _labelB = "TEST";
		ABTestAggregator _aggregator;
		std::optional<Summary> _lastSummary;
		bool _testing{}, _variantB = true, _restorePending{}, _costRun{};
		int _testInterval{}, _windowsDone{}, _windowLimit{}, _framesSinceSwitch{};
		double _lastSwitch{};
		std::string _testError;
	};
}
