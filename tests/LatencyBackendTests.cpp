#include "Render/LatencyBackend.h"

#include "Check.h"

#include <iostream>

namespace
{
	using cs::render::temporal::AntiLag2Diagnostics;
	using cs::render::temporal::AntiLag2Status;
	using cs::render::temporal::GetAntiLag2Status;
	using cs::render::temporal::kAmdVendorId;
	using cs::render::temporal::LatencyBackend;
	using cs::render::temporal::ReflexOwnsLatency;
	using cs::render::temporal::SelectLatencyBackend;
	using cs::render::temporal::ShouldInitializeAntiLag2;

	constexpr std::uint32_t kNvidiaVendorId = 0x10DE;
	constexpr std::uint32_t kIntelVendorId = 0x8086;

	void TestReflexNeedsDriverLowLatency()
	{
		CHECK(ReflexOwnsLatency(true, true));
		// Streamline loads its Reflex plugin on AMD, but the driver reports no low-latency support.
		CHECK(!ReflexOwnsLatency(true, false));
		CHECK(!ReflexOwnsLatency(false, true));
	}

	void TestReflexAlwaysWins()
	{
		CHECK(SelectLatencyBackend(true, false, false) == LatencyBackend::kReflex);
		CHECK(SelectLatencyBackend(true, true, false) == LatencyBackend::kReflex);
		CHECK(SelectLatencyBackend(true, true, true) == LatencyBackend::kReflex);
		CHECK(SelectLatencyBackend(true, false, true) == LatencyBackend::kReflex);
		CHECK(SelectLatencyBackend(false, true, false) == LatencyBackend::kAntiLag2);
		CHECK(SelectLatencyBackend(false, false, false) == LatencyBackend::kNone);
	}

	void TestAntiLag2InitializesOnlyWhenRequestedOnAmd()
	{
		CHECK(ShouldInitializeAntiLag2(true, false, kAmdVendorId, true, false, false, false));
		CHECK(!ShouldInitializeAntiLag2(false, false, kAmdVendorId, true, false, false, false));
		CHECK(!ShouldInitializeAntiLag2(true, true, kAmdVendorId, true, false, false, false));
		CHECK(!ShouldInitializeAntiLag2(true, false, kNvidiaVendorId, true, false, false, false));
		CHECK(!ShouldInitializeAntiLag2(true, false, kIntelVendorId, true, false, false, false));
		CHECK(!ShouldInitializeAntiLag2(true, false, 0, true, false, false, false));
		CHECK(!ShouldInitializeAntiLag2(true, false, kAmdVendorId, false, false, false, false));
		// Once per device, and no retry after a failure on it.
		CHECK(!ShouldInitializeAntiLag2(true, false, kAmdVendorId, true, false, true, false));
		CHECK(!ShouldInitializeAntiLag2(true, false, kAmdVendorId, true, false, false, true));
	}

	void TestQuarantinedPresentationStopsAntiLag2()
	{
		// The context stays alive for a leaked presenter, but the quarantined device gets no further calls.
		CHECK(!ShouldInitializeAntiLag2(true, false, kAmdVendorId, true, true, false, false));
		CHECK(SelectLatencyBackend(false, true, true) == LatencyBackend::kNone);
		CHECK(SelectLatencyBackend(false, false, true) == LatencyBackend::kNone);

		constexpr std::int32_t kFail = static_cast<std::int32_t>(0x80004005);
		const AntiLag2Diagnostics quarantined{
			.deviceAttached = true,
			.presentationQuarantined = true,
			.initialized = true,
			.enabled = true,
			.lastUpdateResult = kFail
		};
		CHECK(GetAntiLag2Status(false, quarantined) == AntiLag2Status::kPresentationFailed);
		CHECK(GetAntiLag2Status(true, quarantined) == AntiLag2Status::kReflexOwnsLatency);
		CHECK(GetAntiLag2Status(false, { .deviceAttached = true, .presentationQuarantined = true, .initializationFailed = true }) ==
			  AntiLag2Status::kPresentationFailed);
	}

	void TestAntiLag2StatusReportsFailingUpdates()
	{
		constexpr std::int32_t kFail = static_cast<std::int32_t>(0x80004005);
		AntiLag2Diagnostics active{ .deviceAttached = true, .initialized = true, .enabled = true };
		CHECK(GetAntiLag2Status(false, active) == AntiLag2Status::kReady);

		active.lastUpdateResult = kFail;
		CHECK(GetAntiLag2Status(false, active) == AntiLag2Status::kUpdateFailing);
		// A failure left over from before the toggle was turned off is not reported.
		AntiLag2Diagnostics disabled = active;
		disabled.enabled = false;
		CHECK(GetAntiLag2Status(false, disabled) == AntiLag2Status::kReady);
		active.lastUpdateResult = 0;
		CHECK(GetAntiLag2Status(false, active) == AntiLag2Status::kReady);

		CHECK(GetAntiLag2Status(true, active) == AntiLag2Status::kReflexOwnsLatency);
		CHECK(GetAntiLag2Status(false, {}) == AntiLag2Status::kNeedsD3D12Presentation);
		CHECK(GetAntiLag2Status(false, { .deviceAttached = true, .initializationFailed = true }) ==
			  AntiLag2Status::kUnavailable);
	}
}

int main()
{
	TestReflexNeedsDriverLowLatency();
	TestReflexAlwaysWins();
	TestAntiLag2InitializesOnlyWhenRequestedOnAmd();
	TestQuarantinedPresentationStopsAntiLag2();
	TestAntiLag2StatusReportsFailingUpdates();
	if (cs::tests::failures)
		return 1;
	std::cout << "Latency backend tests passed\n";
	return 0;
}
