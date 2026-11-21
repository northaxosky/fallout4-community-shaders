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
			kInsufficient,
			// Most frames were limited by vsync, a cap or frame generation.
			kCapped
		};

		// Total frame time; delta is B minus A, set only when comparable.
		struct Summary
		{
			bool comparable{};
			float meanA{}, meanB{}, medianA{}, medianB{}, delta{};
			// Standard error of delta over paired B/A windows; set from two pairs.
			std::optional<float> deltaStdErr;
			int pairs{};
			float fpsA{}, fpsB{};
			int frames{}, excluded{}, windows{};
			float duration{}, validPercent{};
			Validity validity{ Validity::kInsufficient };
		};

		struct Progress
		{
			bool running{}, restorePending{}, scripted{};
			int window{}, windows{};
			bool variantB{};
			double windowSecondsLeft{};
		};

		static constexpr std::string_view ValidityName(Validity a_validity) noexcept
		{
			return a_validity == Validity::kValid ? "Valid" : a_validity == Validity::kMarginal ? "Marginal" :
			                                              a_validity == Validity::kCapped       ? "Frame-capped" :
			                                                                                      "Insufficient";
		}

		// Differences from the current live state; A is the current state plus these.
		struct Variant
		{
			std::map<Feature*, toml::table> settings;
			std::optional<feature_config::ShaderOwnershipConfig> ownership;
			bool Empty() const noexcept { return settings.empty() && !ownership; }
		};

		// windowSeconds includes the settle period.
		static constexpr double kMinSettleSeconds = 0.25;
		struct RunOptions
		{
			int windowSeconds = 3;
			int cycles = 10;
			double settleSeconds = kMinSettleSeconds;
		};

		explicit ABComparison(const Feature& a_owner) :
			_owner(a_owner) {}

		bool Testing() const noexcept { return _testing; }
		bool ScriptedRunning() const noexcept { return _testing && _scripted; }
		// The current or last run was scripted (fixed length), not manual USER/TEST.
		bool LastRunWasScripted() const noexcept { return _scripted; }
		bool RestorePending() const noexcept { return _restorePending; }
		// Per-type rows need draw profiling; a scripted run reads only the Total row.
		bool WantsDrawTimings() const noexcept { return _testing && !_scripted; }
		Progress GetProgress() const noexcept;
		const std::string& Error() const noexcept { return _testError; }
		// Live aggregation; LastSummary is the one frozen when the run stopped.
		std::optional<Summary> Summarize() const;
		const std::optional<Summary>& LastSummary() const noexcept { return _lastSummary; }
		void CaptureBaseline();
		// Zero restores B and ends the run; throws if restoration fails.
		void SetInterval(int);
		// Current state (B) vs current plus the variant (A); throws if invalid.
		void StartRun(const Variant&, const RunOptions&, std::string a_labelA, std::string a_labelB);
		// Custom shaders on (B) against off (A); throws when they are already off.
		void StartCostRun(const RunOptions&);
		void Abort(std::string_view a_reason) noexcept;
		void Fail(std::string a_message);
		// Runs the action, returning a thrown error's message.
		static std::expected<void, std::string> Try(const std::function<void()>& a_action);
		std::string CostHelp() const;
		// Throws when a compared feature stops being healthy or a state cannot apply.
		void Update(double a_now, const std::vector<DrawCallRow>& a_rows, bool a_capped);
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
		// A null a_from applies everything; else only features that differ from it.
		static void Apply(const State&, const State* a_from = nullptr, bool a_restoring = false);
		// A zero window limit runs until stopped.
		void Begin(State a_a, State a_b, std::string a_labelA, std::string a_labelB, int a_interval, int a_windowLimit, double a_settleSeconds);
		void Attempt(std::function<void()> a_action);
		void LogSummary() const;

		const Feature& _owner;
		State _baseline, _a, _b;
		std::string _labelA = "USER", _labelB = "TEST";
		ABTestAggregator _aggregator;
		std::optional<Summary> _lastSummary;
		bool _testing{}, _variantB = true, _restorePending{}, _scripted{};
		int _testInterval{}, _windowsDone{}, _windowLimit{}, _framesSinceSwitch{}, _measuredFrames{}, _cappedFrames{};
		double _lastSwitch{}, _settleSeconds{ RunOptions{}.settleSeconds };
		std::string _testError;
	};
}
