#pragma once

#include "Feature.h"
#include <Features/PerformanceOverlay/ABTesting/ABTestAggregator.h>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace cs::features::performance_overlay
{
	// Alternates two live-settings snapshots and aggregates frame rows per variant.
	class ABComparison
	{
	public:
		explicit ABComparison(const Feature& a_owner) :
			_owner(a_owner) {}

		bool Testing() const noexcept { return _testing; }
		bool RestorePending() const noexcept { return _restorePending; }
		// Rebases USER on the current live settings and drops the last run.
		void CaptureBaseline();
		// Zero restores TEST and ends the run; throws if restoration fails.
		void SetInterval(int);
		void Abort(std::string_view a_reason) noexcept;
		// Throws when a compared feature stops being healthy or settings cannot apply.
		void Update(double a_now, const std::vector<DrawCallRow>& a_rows);
		void DrawControls();
		void DrawResults();

	private:
		using Snapshot = std::map<Feature*, toml::table>;
		static Snapshot Capture();
		static void Apply(const Snapshot&, bool a_restoring = false);

		const Feature& _owner;
		Snapshot _baseline, _test;
		ABTestAggregator _aggregator;
		bool _testing{}, _variantB = true, _restorePending{};
		int _testInterval{};
		double _lastSwitch{};
		std::string _testError;
	};
}
