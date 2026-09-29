#include "ScreenSpaceGIHistory.h"

#include <cmath>
#include <iostream>

namespace
{
	using cs::features::ssgi::Float2;
	using cs::features::ssgi::HistoryResetReason;
	using cs::features::ssgi::HistoryState;
	int failures = 0;

	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
			++failures;
		}
	}

	bool Near(float a_left, float a_right)
	{
		return std::abs(a_left - a_right) < 1.0e-5f;
	}

	void Seed(HistoryState& a_history)
	{
		a_history.Publish();
	}

	void TestInvalidation()
	{
		HistoryState history;
		Check(!history.Prepare().useHistory, "an unpublished history must not be read");
		Seed(history);
		Check(history.Prepare().useHistory, "a skipped frame must keep history, as upstream");

		history.Reset(HistoryResetReason::kResize);
		Check(
			!history.Valid() && !history.Prepare().useHistory && history.LastResetReason() == HistoryResetReason::kResize && history.ConsumeClearPending(),
			"resize must force a cleared current-frame seed");
		history.Publish();
		Check(history.Prepare().useHistory, "a valid replacement frame must restore history use");
	}

	void TestReprojectionSign()
	{
		using cs::features::ssgi::MotionFromNDC;
		using cs::features::ssgi::NDCToUV;
		using cs::features::ssgi::PreviousUV;
		constexpr Float2 previousNdc{ -0.5f, -0.25f };
		constexpr Float2 currentNdc{ 0.25f, 0.5f };
		constexpr Float2 currentUv = NDCToUV(currentNdc);
		constexpr Float2 expected = NDCToUV(previousNdc);
		constexpr Float2 motion = MotionFromNDC(currentNdc, previousNdc);
		constexpr Float2 reprojected = PreviousUV(currentUv, motion);
		Check(
			Near(reprojected.x, expected.x) && Near(reprojected.y, expected.y),
			"stored motion must reproject to the previous surface");
		Check(
			!Near(currentUv.x - motion.x, expected.x) && !Near(currentUv.y - motion.y, expected.y),
			"the opposite motion sign must not appear valid");
	}
}

int main()
{
	TestInvalidation();
	TestReprojectionSign();
	return failures == 0 ? 0 : 1;
}
