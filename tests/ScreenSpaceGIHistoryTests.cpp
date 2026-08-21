#include "ScreenSpaceGIHistory.h"

#include <cmath>
#include <iostream>
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

	using cs::features::ssgi::Float2;
	using cs::features::ssgi::HistoryResetReason;
	using cs::features::ssgi::HistoryState;

	bool Near(float a_lhs, float a_rhs)
	{
		return std::abs(a_lhs - a_rhs) < 1e-5f;
	}

	void TestInitialState()
	{
		HistoryState history;

		CHECK(!history.Valid());
		CHECK(!history.Published());
		CHECK(history.ResetCount() == 0);
		CHECK(history.LastResetReason() == HistoryResetReason::kFirstFrame);
		CHECK(history.ReadIndex() == 0);
		CHECK(history.WriteIndex() == 1);

		const auto frame = history.Prepare(1);
		CHECK(!frame.useHistory);
		CHECK(frame.readIndex == 0);
		CHECK(frame.writeIndex == 1);
		CHECK(history.ConsumeClearPending());
		CHECK(!history.ConsumeClearPending());
	}

	void TestPingPongAlternation()
	{
		HistoryState history;

		auto frame = history.Prepare(1);
		CHECK(!frame.useHistory);
		history.Publish(1);
		CHECK(history.Valid());
		CHECK(history.Published());
		CHECK(history.ReadIndex() == 1);
		CHECK(history.WriteIndex() == 0);

		frame = history.Prepare(2);
		CHECK(frame.useHistory);
		CHECK(frame.readIndex == 1);
		CHECK(frame.writeIndex == 0);
		history.Publish(2);
		CHECK(history.ReadIndex() == 0);

		frame = history.Prepare(3);
		CHECK(frame.readIndex == 0);
		CHECK(frame.writeIndex == 1);
		history.Publish(3);
		CHECK(history.ReadIndex() == 1);
		CHECK(history.ResetCount() == 0);
	}

	void TestFrameGapInvalidation()
	{
		HistoryState history;

		history.Prepare(10);
		history.Publish(10);
		CHECK(history.Valid());

		const auto frame = history.Prepare(14);
		CHECK(!frame.useHistory);
		CHECK(history.LastResetReason() == HistoryResetReason::kFrameGap);
		CHECK(history.ResetCount() == 1);
		CHECK(history.ConsumeClearPending());

		// The write index survives an invalidation, so the pair keeps alternating.
		CHECK(frame.readIndex == 1);
		CHECK(frame.writeIndex == 0);
	}

	void TestResizeReset()
	{
		HistoryState history;

		history.Prepare(4);
		history.Publish(4);
		CHECK(history.Valid());

		history.Reset(HistoryResetReason::kResize);
		CHECK(!history.Valid());
		CHECK(history.Published());
		CHECK(history.ResetCount() == 1);
		CHECK(history.LastResetReason() == HistoryResetReason::kResize);
		CHECK(history.ConsumeClearPending());

		const auto frame = history.Prepare(5);
		CHECK(!frame.useHistory);
	}

	void TestLoadingScreenReset()
	{
		HistoryState history;

		history.Prepare(20);
		history.Publish(20);
		history.Reset(HistoryResetReason::kLoadingScreenClosed);
		CHECK(history.LastResetReason() == HistoryResetReason::kLoadingScreenClosed);
		CHECK(history.ResetCount() == 1);

		// Reseeding a current frame may invalidate again.
		history.Reset(HistoryResetReason::kMissingInputs);
		CHECK(history.ResetCount() == 2);
		CHECK(history.LastResetReason() == HistoryResetReason::kMissingInputs);
		CHECK(!history.Prepare(21).useHistory);
	}

	void TestMissingMotionSeedsCurrentOnly()
	{
		HistoryState history;

		history.Prepare(30);
		history.Publish(30);
		CHECK(history.Valid());

		// The render thread invalidates before Prepare when motion vectors are absent.
		history.Reset(HistoryResetReason::kMissingMotion);
		const auto frame = history.Prepare(31);
		CHECK(!frame.useHistory);
		CHECK(history.LastResetReason() == HistoryResetReason::kMissingMotion);

		history.Publish(31);
		CHECK(history.Valid());
		CHECK(history.Prepare(32).useHistory);
	}

	void TestReprojectionSign()
	{
		using cs::features::ssgi::MotionFromNDC;
		using cs::features::ssgi::NDCToUV;
		using cs::features::ssgi::PreviousUV;

		// The surface moved right and up in NDC between the two frames.
		constexpr Float2 prevNDC{ -0.5f, -0.25f };
		constexpr Float2 currNDC{ 0.25f, 0.5f };

		constexpr Float2 currUV = NDCToUV(currNDC);
		constexpr Float2 expectedPrevUV = NDCToUV(prevNDC);
		constexpr Float2 motion = MotionFromNDC(currNDC, prevNDC);
		constexpr Float2 reprojected = PreviousUV(currUV, motion);

		CHECK(Near(currUV.x, 0.625f));
		CHECK(Near(currUV.y, 0.25f));
		CHECK(Near(expectedPrevUV.x, 0.25f));
		CHECK(Near(expectedPrevUV.y, 0.625f));
		CHECK(Near(motion.x, -0.375f));
		CHECK(Near(motion.y, 0.375f));

		CHECK(Near(reprojected.x, expectedPrevUV.x));
		CHECK(Near(reprojected.y, expectedPrevUV.y));

		// Subtracting the stored motion lands on the wrong surface.
		constexpr Float2 subtracted{ currUV.x - motion.x, currUV.y - motion.y };
		CHECK(!Near(subtracted.x, expectedPrevUV.x));
		CHECK(!Near(subtracted.y, expectedPrevUV.y));
	}

	void TestResetReasonNames()
	{
		using cs::features::ssgi::HistoryResetReasonName;

		CHECK(std::string_view(HistoryResetReasonName(HistoryResetReason::kFirstFrame)) == "first_frame");
		CHECK(std::string_view(HistoryResetReasonName(HistoryResetReason::kFrameGap)) == "frame_gap");
		CHECK(std::string_view(HistoryResetReasonName(HistoryResetReason::kSourceModeChanged)) == "source_mode_change");
		CHECK(std::string_view(HistoryResetReasonName(HistoryResetReason::kGenerationFailed)) == "generation_failed");
	}
}

int main()
{
	TestInitialState();
	TestPingPongAlternation();
	TestFrameGapInvalidation();
	TestResizeReset();
	TestLoadingScreenReset();
	TestMissingMotionSeedsCurrentOnly();
	TestReprojectionSign();
	TestResetReasonNames();

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "ScreenSpaceGI history tests passed\n";
	return 0;
}
