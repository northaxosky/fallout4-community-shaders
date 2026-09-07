#include "Host/HostPageCatalog.h"
#include "Host/HostRuntimeModel.h"
#include "Menu/DebugViewSelection.h"
#include "Utils/PhysicalFile.h"

#include <DearModdingUI/Client.h>
#include <DearModdingUI/IconGlyphs.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
	int failures{};

	void Check(bool a_condition, std::string_view a_expression, int a_line)
	{
		if (!a_condition) {
			std::cerr << "CHECK failed at line " << a_line << ": "
					  << a_expression << '\n';
			++failures;
		}
	}

#define CHECK(a_expression) Check(static_cast<bool>(a_expression), #a_expression, __LINE__)

	constexpr DMUI_HostServices kRequiredServices =
		DMUI_HOST_SERVICE_FRAME_CONTROL |
		DMUI_HOST_SERVICE_EDIT_LIFECYCLE |
		DMUI_HOST_SERVICE_CONTEXTUAL_HOTKEYS |
		DMUI_HOST_SERVICE_IMAGE_RESOURCES |
		DMUI_HOST_SERVICE_MANAGED_OVERLAYS |
		DMUI_HOST_SERVICE_NOTIFICATIONS |
		DMUI_HOST_SERVICE_ANNOTATED_PLOTS |
		DMUI_HOST_SERVICE_DIALOGS;

	DMUI_HostServices supportedServices = kRequiredServices;
	std::uint32_t forwardingVersion = DMUI_FORWARDING_VERSION_1_1;

	DMUI_Result DMUI_CALL RegisterClient(
		const DMUI_ClientDescriptor*,
		DMUI_ClientHandle*) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL QueryServices(DMUI_HostServicesInfo* a_info) noexcept
	{
		a_info->forwardingVersion = forwardingVersion;
		a_info->supportedServices = supportedServices;
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL RequestFrame(
		DMUI_ClientHandle,
		DMUI_PageHandle) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL ReleaseFrame(
		DMUI_ClientHandle,
		DMUI_PageHandle) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL RegisterHotkey(
		DMUI_ClientHandle,
		const DMUI_HotkeyActionDescriptor*,
		DMUI_HotkeyActionHandle*) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL SetHotkeyEnabled(
		DMUI_ClientHandle,
		DMUI_HotkeyActionHandle,
		std::uint32_t) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL ImportImage(
		DMUI_ClientHandle,
		const DMUI_D3D11ImageDescriptor*,
		DMUI_ImageHandle*) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL DrawImage(
		DMUI_ClientHandle,
		DMUI_ImageHandle,
		const DMUI_ImageDrawOptions*) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL ReleaseImage(
		DMUI_ClientHandle,
		DMUI_ImageHandle) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL QueryImage(
		DMUI_ClientHandle,
		DMUI_ImageHandle,
		DMUI_ImageInfo*) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL ConfigureOverlay(
		DMUI_ClientHandle,
		DMUI_PageHandle,
		const DMUI_ManagedOverlayOptions*) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL QueryOverlay(
		DMUI_ClientHandle,
		DMUI_PageHandle,
		DMUI_ManagedOverlayPlacement*) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL PostNotification(
		DMUI_ClientHandle,
		const DMUI_NotificationDescriptor*) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL DrawPlot(
		DMUI_ClientHandle,
		const char*,
		const DMUI_AnnotatedPlotDescriptor*) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL RequestDialog(
		DMUI_ClientHandle,
		const DMUI_DialogDescriptor*,
		DMUI_DialogHandle*) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL PollDialog(
		DMUI_ClientHandle,
		DMUI_DialogHandle,
		DMUI_DialogEvent*,
		char*,
		std::uint32_t) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL ResolveDialog(
		DMUI_ClientHandle,
		DMUI_DialogHandle,
		std::uint64_t,
		std::uint32_t,
		const char*) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL CancelDialog(
		DMUI_ClientHandle,
		DMUI_DialogHandle) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_HostAPI MakeHost()
	{
		DMUI_HostAPI api{};
		api.structSize = sizeof(api);
		api.apiVersion = DMUI_API_VERSION_CURRENT;
		api.registerClient = &RegisterClient;
		api.requestFrame = &RequestFrame;
		api.releaseFrame = &ReleaseFrame;
		api.registerHotkeyAction = &RegisterHotkey;
		api.setHotkeyActionEnabled = &SetHotkeyEnabled;
		api.importD3D11Image = &ImportImage;
		api.drawImage = &DrawImage;
		api.releaseImage = &ReleaseImage;
		api.queryImage = &QueryImage;
		api.configureOverlay = &ConfigureOverlay;
		api.queryOverlay = &QueryOverlay;
		api.postNotification = &PostNotification;
		api.drawAnnotatedPlot = &DrawPlot;
		api.requestDialog = &RequestDialog;
		api.pollDialogEvent = &PollDialog;
		api.resolveDialogSubmission = &ResolveDialog;
		api.cancelDialog = &CancelDialog;
		api.queryServices = &QueryServices;
		return api;
	}

	void TestForwardingPreflight()
	{
		const dmui::ClientOptions options{
			.capabilities = DMUI_CLIENT_CAPABILITY_RENDERER_REPLACEMENT,
			.requiredServices = kRequiredServices,
			.minimumForwardingVersion = DMUI_FORWARDING_VERSION_1_1
		};
		auto api = MakeHost();
		CHECK(
			dmui::PreflightHostAPI(nullptr, options) ==
			DMUI_RESULT_UNSUPPORTED_ABI);
		CHECK(dmui::PreflightHostAPI(&api, options) == DMUI_RESULT_OK);

		supportedServices &= ~DMUI_HOST_SERVICE_DIALOGS;
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_SERVICE_UNAVAILABLE);
		supportedServices = kRequiredServices;

		forwardingVersion = DMUI_FORWARDING_VERSION_1_0;
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_FORWARDING_VERSION_MISMATCH);
		forwardingVersion = DMUI_FORWARDING_VERSION_1_1;

		api.queryServices = nullptr;
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_SERVICE_UNAVAILABLE);
	}

	void TestPageCatalog()
	{
		const std::vector<cs::host::FeaturePageInput> features{
			{ "WetnessEffects", "Wetness Effects", "Lighting", "Rain film.", true, true },
			{ "RenderDoc", "RenderDoc", "Dev Tools", "Capture.", false, true },
			{ "Performance Overlay!", "Performance Overlay", "Performance", "FPS.", true, true },
			{ "Upscaling", "Upscaling", "Performance", "Temporal SR.", true, true },
			{ "FrameGeneration", "Frame Generation", "Performance", "Temporal FG.", false, true }
		};
		const auto pages = cs::host::BuildPageCatalog(features);
		CHECK(pages.size() == features.size() + 4);
		CHECK(pages.front().id == "home");
		CHECK(pages.front().category == "General");
		CHECK(DearModdingUI::ResolveClientIconGlyph(
			cs::host::kClientIconName, {}, "Community Shaders") ==
			DearModdingUI::ResolveNamedIconGlyphOrZero("lightbulb"));
		CHECK(DearModdingUI::ResolveCategoryIconGlyph(
			cs::host::kBuiltInCategory, "Community Shaders",
			"dearmodding.community-shaders", cs::host::kClientIconName) ==
			DearModdingUI::ResolveNamedIconGlyphOrZero("gear"));
		CHECK(std::ranges::none_of(pages, [](const auto& page) {
			return page.id == "general";
		}));
		CHECK(pages.back().id == cs::host::kOverlayPageId);
		CHECK(std::ranges::any_of(pages, [](const auto& page) {
			return page.id == "feature-renderdoc" &&
				page.category == cs::host::kUnloadedCategory;
		}));
		CHECK(std::ranges::any_of(pages, [](const auto& page) {
			return page.id == "feature-performance-overlay";
		}));
		CHECK(std::ranges::any_of(pages, [](const auto& page) {
			return page.id == "feature-upscaling" &&
				page.category == "Performance";
		}));
		CHECK(std::ranges::any_of(pages, [](const auto& page) {
			return page.id == "feature-framegeneration" &&
				page.category == cs::host::kUnloadedCategory;
		}));
	}

	void TestDebugSelectionSeparation()
	{
		cs::debug_view::SelectionState state;
		state.Select(
			"ScreenSpaceGI",
			"radiance",
			cs::FeatureDebugViewKind::kTexturePreview);
		state.Select(
			"Skylighting",
			"occlusion",
			cs::FeatureDebugViewKind::kTexturePreview);
		state.Select(
			"WetnessEffects",
			"wetness",
			cs::FeatureDebugViewKind::kFullscreen);
		CHECK(state.Previews().size() == 2);
		CHECK(state.Fullscreen().feature == "WetnessEffects");
		state.Select(
			"InverseSquareLighting",
			"comparison",
			cs::FeatureDebugViewKind::kFullscreen);
		CHECK(state.Previews().size() == 2);
		CHECK(state.Fullscreen().feature == "InverseSquareLighting");
	}

	void TestPhysicalFileLocation()
	{
		const auto source = std::filesystem::path(__FILE__);
		const auto resolved = cs::files::PhysicalFilePath(source);
		CHECK(resolved.has_value());
		if (resolved) {
			CHECK(resolved->is_absolute());
			CHECK(!resolved->native().starts_with(L"\\Device\\"));
			std::error_code error;
			CHECK(std::filesystem::equivalent(*resolved, source, error));
			CHECK(!error);
		}
		const auto missing = cs::files::PhysicalFilePath(
			source.parent_path() / L"missing-physical-path-test.no-such-file");
		CHECK(!missing);
		CHECK(missing.error().value() != 0);
	}

	void TestSnapshotRefresh()
	{
		cs::DebugSnapshotRequest snapshot;
		CHECK(!snapshot.Ready());
		CHECK(snapshot.Pending() == 0);
		snapshot.Refresh();
		const auto first = snapshot.Pending();
		CHECK(first != 0);
		// A failed/unavailable capture leaves the request pending.
		CHECK(snapshot.Pending() == first);
		snapshot.Captured(first);
		CHECK(snapshot.Ready());
		CHECK(snapshot.Pending() == 0);
		for (int frame = 0; frame < 100; ++frame)
			CHECK(snapshot.Pending() == 0);
		snapshot.Refresh();
		const auto second = snapshot.Pending();
		CHECK(second != first);
		CHECK(snapshot.Ready());
		snapshot.Refresh();
		snapshot.Captured(second);
		CHECK(snapshot.Pending() != 0);
		snapshot.Captured(snapshot.Pending());
		CHECK(snapshot.Pending() == 0);
		snapshot.Invalidate();
		CHECK(!snapshot.Ready());
		CHECK(snapshot.Pending() != 0);
		snapshot.Reset();
		CHECK(snapshot.Pending() == 0);
		CHECK(!snapshot.Ready());
	}

	void TestSharedSnapshotSelection()
	{
		cs::DebugSnapshotRequest snapshot;
		snapshot.Select(true);
		const auto request = snapshot.Pending();
		CHECK(request != 0);
		snapshot.Captured(request);
		CHECK(snapshot.Revision() == request);
		snapshot.Select(true);
		CHECK(snapshot.Pending() == 0);
		CHECK(snapshot.Revision() == request);
		snapshot.Refresh();
		CHECK(snapshot.Ready());
		CHECK(snapshot.Revision() == request);
		const auto refreshed = snapshot.Pending();
		snapshot.Captured(refreshed);
		snapshot.Select(true);
		CHECK(snapshot.Revision() == refreshed);
		CHECK(snapshot.Pending() == 0);
		snapshot.Select(false);
		CHECK(!snapshot.Ready());
		snapshot.Select(true);
		CHECK(snapshot.Pending() != 0);
	}

	void TestStartupLoadIntent()
	{
		auto root = toml::parse(
			"[features.loaded]\nload = true\n"
			"[features.disabled]\nload = false\n");
		cs::host::StartupLoadSnapshot startup;
		startup.Capture(root);
		// Failure or ENB deactivation must not change the recorded startup configuration.
		CHECK(!startup.RequiresRestart("loaded", true));
		CHECK(!startup.RequiresRestart("disabled", false));
		CHECK(startup.RequiresRestart("loaded", false));
		CHECK(startup.RequiresRestart("disabled", true));
		root["features"]["loaded"].as_table()->insert_or_assign("load", false);
		CHECK(startup.RequiresRestart("loaded", false));
		CHECK(!startup.RequiresRestart("loaded", true));
		CHECK(!startup.RequiresRestart("missing", false));
	}

	void TestFrameDemandBalancing()
	{
		cs::host::FrameDemandTracker demand;
		CHECK(demand.Next(false) == cs::host::FrameDemandAction::kNone);
		CHECK(demand.Next(true) == cs::host::FrameDemandAction::kRequest);
		demand.Complete(cs::host::FrameDemandAction::kRequest, false);
		CHECK(!demand.Requested());
		CHECK(demand.Next(true) == cs::host::FrameDemandAction::kRequest);
		demand.Complete(cs::host::FrameDemandAction::kRequest, true);
		CHECK(demand.Requested());
		CHECK(demand.Next(true) == cs::host::FrameDemandAction::kNone);
		CHECK(demand.Next(false) == cs::host::FrameDemandAction::kRelease);
		demand.Complete(cs::host::FrameDemandAction::kRelease, true);
		CHECK(!demand.Requested());
	}

	void TestDialogSubmissionDeduplication()
	{
		cs::host::DialogSubmissionTracker submissions;
		cs::host::DialogCallRetry retries;
		CHECK(!submissions.ShouldExecute(0));
		CHECK(submissions.ShouldExecute(17));
		int operations{};
		if (submissions.ShouldExecute(17)) {
			++operations;
			submissions.RecordOutcome(17, true, {});
		}
		CHECK(operations == 1);
		CHECK(!submissions.ShouldExecute(17));
		CHECK(submissions.Pending(17));
		// A failed host resolution retries only resolution, never the disk action.
		CHECK(retries.Ready(10));
		CHECK(retries.Failed(DMUI_RESULT_HOST_NOT_READY, 10));
		CHECK(!retries.Ready(69));
		CHECK(retries.Ready(70));
		CHECK(!retries.Failed(DMUI_RESULT_HOST_NOT_READY, 70));
		CHECK(!cs::host::DialogCallRetry::HandleLost(DMUI_RESULT_HOST_NOT_READY));
		CHECK(!submissions.ShouldExecute(17));
		CHECK(operations == 1);
		retries.Succeeded();
		CHECK(retries.Ready(71));
		submissions.ResolutionSucceeded(17);
		CHECK(!submissions.ShouldExecute(17));
		CHECK(submissions.ShouldExecute(18));
		submissions.RecordOutcome(18, false, "rejected");
		CHECK(submissions.Pending(18));
		submissions.ResolutionSucceeded(18);
		CHECK(submissions.ShouldExecute(19));
		// Cancelling/resetting a dialog does not manufacture an operation.
		submissions.Reset();
		CHECK(operations == 1);
		CHECK(submissions.ShouldExecute(17));
		CHECK(cs::host::DialogCallRetry::HandleLost(DMUI_RESULT_STALE_HANDLE));
		CHECK(cs::host::DialogCallRetry::HandleLost(DMUI_RESULT_CLIENT_NOT_FOUND));
		CHECK(!cs::host::DialogCallRetry::HandleLost(DMUI_RESULT_BUSY));
	}

	void TestImageImportBookkeeping()
	{
		using cs::host::ImageImportFailure;
		CHECK(cs::host::ShouldImportImage(
			false, ImageImportFailure::kNone, false, true, false, false));
		CHECK(!cs::host::ShouldImportImage(
			false, ImageImportFailure::kTransient, false, true, false, false));
		CHECK(cs::host::ShouldImportImage(
			false, ImageImportFailure::kTransient, false, true, false, true));
		CHECK(!cs::host::ShouldImportImage(
			false, ImageImportFailure::kPermanent, false, true, false, true));
		CHECK(cs::host::ShouldImportImage(
			false, ImageImportFailure::kPermanent, true, true, false, false));
		CHECK(!cs::host::ShouldImportImage(
			true, ImageImportFailure::kNone, false, true, true, false));
		CHECK(cs::host::ShouldImportImage(
			true, ImageImportFailure::kNone, false, true, false, false));
		CHECK(cs::host::ShouldImportImage(
			true, ImageImportFailure::kNone, false, false, false, false));
	}

	void TestLegacyHotkeySeed()
	{
		const auto integer = toml::parse(
			"[menu]\n"
			"overlay_toggle_key = 121\n");
		const auto integerSeed = cs::host::ParseLegacyKeyboardHotkey(
			integer["menu"].as_table()->get("overlay_toggle_key"));
		CHECK(integerSeed.present);
		CHECK(integerSeed.valid);
		CHECK(integerSeed.chord == "F10");

		const auto chord = toml::parse(
			"[menu]\n"
			"overlay_toggle_key = [17, 121]\n");
		const auto chordSeed = cs::host::ParseLegacyKeyboardHotkey(
			chord["menu"].as_table()->get("overlay_toggle_key"));
		CHECK(chordSeed.valid);
		CHECK(chordSeed.chord == "Ctrl+F10");

		const auto unbound = toml::parse(
			"[menu]\n"
			"overlay_toggle_key = 0\n");
		const auto unboundSeed = cs::host::ParseLegacyKeyboardHotkey(
			unbound["menu"].as_table()->get("overlay_toggle_key"));
		CHECK(unboundSeed.valid);
		CHECK(unboundSeed.chord == "None");

		const auto invalid = toml::parse(
			"[menu]\n"
			"overlay_toggle_key = [17, 121, 120]\n");
		const auto invalidSeed = cs::host::ParseLegacyKeyboardHotkey(
			invalid["menu"].as_table()->get("overlay_toggle_key"));
		CHECK(invalidSeed.present);
		CHECK(!invalidSeed.valid);
		const auto mixedUnbound = toml::parse(
			"[menu]\n"
			"overlay_toggle_key = [0, 121]\n");
		CHECK(!cs::host::ParseLegacyKeyboardHotkey(
			mixedUnbound["menu"].as_table()->get("overlay_toggle_key")).valid);

		CHECK(cs::host::ResolveHotkeySeed(
			"F10", false, chordSeed.chord) == "Ctrl+F10");
		CHECK(cs::host::ResolveHotkeySeed(
			"Alt+8", true, chordSeed.chord) == "Alt+8");
		CHECK(cs::host::ResolveHotkeySeed("F10", false, {}) == "F10");
		CHECK(cs::host::ResolveHotkeySeed(
			"F10", false, unboundSeed.chord) == "None");
	}
}

int main()
{
	TestForwardingPreflight();
	TestPageCatalog();
	TestPhysicalFileLocation();
	TestSnapshotRefresh();
	TestSharedSnapshotSelection();
	TestStartupLoadIntent();
	TestDebugSelectionSeparation();
	TestFrameDemandBalancing();
	TestDialogSubmissionDeduplication();
	TestImageImportBookkeeping();
	TestLegacyHotkeySeed();
	if (failures)
		std::cerr << failures << " failure(s)\n";
	return failures == 0 ? 0 : 1;
}
