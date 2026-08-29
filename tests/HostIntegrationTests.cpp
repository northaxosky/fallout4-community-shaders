#include "Host/HostDiscoveryModel.h"
#include "Host/HostPageCatalog.h"
#include "Host/IntegrationState.h"
#include "Host/OverlayDemandModel.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

	using namespace cs::host;

	DMUI_ImGuiFingerprint MakeFingerprint()
	{
		DMUI_ImGuiFingerprint fingerprint{};
		fingerprint.structSize = sizeof(fingerprint);
		std::memcpy(
			fingerprint.upstreamCommit,
			DMUI_IMGUI_UPSTREAM_COMMIT,
			sizeof(fingerprint.upstreamCommit));
		fingerprint.imguiVersionNum = DMUI_IMGUI_VERSION_NUM;
		fingerprint.flags = DMUI_IMGUI_FINGERPRINT_DOCKING;
		fingerprint.sizeOfImGuiIO = 1024;
		fingerprint.sizeOfImGuiStyle = 512;
		fingerprint.sizeOfImVec2 = 8;
		fingerprint.sizeOfImVec4 = 16;
		fingerprint.sizeOfImDrawVert = 20;
		fingerprint.sizeOfImDrawIdx = 2;
		return fingerprint;
	}

	HostApiView MakeApiView(const DMUI_ImGuiFingerprint& a_fingerprint)
	{
		HostApiView view{};
		view.present = true;
		view.structSize = sizeof(DMUI_HostAPI);
		view.apiVersion = DMUI_API_VERSION_CURRENT;
		view.hasFingerprint = true;
		view.fingerprint = a_fingerprint;
		view.hasRegisterClient = true;
		view.hasRegisterPage = true;
		view.hasQueryState = true;
		view.hasRequestFrame = true;
		view.hasReleaseFrame = true;
		view.hasIsMenuVisible = true;
		view.hasSelectPage = true;
		return view;
	}

	std::vector<FeaturePageInput> MakeFeatures()
	{
		return {
			FeaturePageInput{ .name = "WetnessEffects", .displayName = "Wetness Effects", .category = "Lighting", .summary = "Rain-driven water film.", .active = true, .installed = true },
			FeaturePageInput{ .name = "Performance Overlay!", .displayName = "Performance Overlay", .category = "Performance", .summary = "FPS counter.", .active = true, .installed = true },
			FeaturePageInput{ .name = "RenderDoc", .displayName = "RenderDoc", .category = "Dev Tools", .summary = "Frame capture.", .active = false, .installed = true },
			FeaturePageInput{ .name = "Upscaling", .displayName = "Upscaling", .category = "Performance", .summary = "DLSS and FSR.", .active = false, .installed = false }
		};
	}

	const HostPageDescriptor* FindPage(const std::vector<HostPageDescriptor>& a_pages, std::string_view a_id)
	{
		for (const auto& page : a_pages) {
			if (page.id == a_id)
				return &page;
		}
		return nullptr;
	}

	void TestAbsentHost()
	{
		std::vector<HostCandidate> candidates;
		const auto selection = SelectHost(candidates);
		CHECK(!selection.selected);
		CHECK(selection.exporterCount == 0);
		CHECK(!selection.HasAmbiguousExporters());

		IntegrationStateMachine machine;
		CHECK(machine.Get() == IntegrationState::kUndecided);
		CHECK(machine.ChooseStandalone());
		CHECK(!machine.IsHosted());
		CHECK(DecideBootstrap(machine.Get()) == BootstrapAction::kStandaloneNow);
	}

	void TestCandidateSelectionIsDeterministic()
	{
		std::vector<HostCandidate> candidates{
			HostCandidate{ "c:/game/data/f4se/plugins/zzz.dll", "C:/Game/Data/F4SE/Plugins/ZZZ.dll", HostCompatibility::kCompatible },
			HostCandidate{ "c:/game/data/f4se/plugins/aaa.dll", "C:/Game/Data/F4SE/Plugins/AAA.dll", HostCompatibility::kFingerprintMismatch },
			HostCandidate{ "c:/game/data/f4se/plugins/mmm.dll", "C:/Game/Data/F4SE/Plugins/MMM.dll", HostCompatibility::kCompatible }
		};

		auto shuffled = candidates;
		std::swap(shuffled[0], shuffled[2]);

		const auto first = SelectHost(candidates);
		const auto second = SelectHost(shuffled);

		CHECK(candidates[0].displayPath == "C:/Game/Data/F4SE/Plugins/AAA.dll");
		CHECK(candidates[1].displayPath == "C:/Game/Data/F4SE/Plugins/MMM.dll");
		CHECK(candidates[2].displayPath == "C:/Game/Data/F4SE/Plugins/ZZZ.dll");
		CHECK(first.selected == second.selected);
		CHECK(first.selected.has_value() && *first.selected == 1);
		CHECK(first.exporterCount == 3);
		CHECK(first.compatibleCount == 2);
		CHECK(first.HasAmbiguousExporters());
		CHECK(first.HasAmbiguousHosts());
	}

	void TestIncompatibleHosts()
	{
		const auto expected = MakeFingerprint();

		CHECK(EvaluateHost(HostApiView{}, expected) == HostCompatibility::kNoApi);

		auto tooSmall = MakeApiView(expected);
		tooSmall.structSize = sizeof(DMUI_HostAPI) - 1;
		CHECK(EvaluateHost(tooSmall, expected) == HostCompatibility::kStructTooSmall);

		auto wrongVersion = MakeApiView(expected);
		wrongVersion.apiVersion = DMUI_MAKE_VERSION(2u, 0u);
		CHECK(EvaluateHost(wrongVersion, expected) == HostCompatibility::kUnsupportedVersion);

		auto missingFunctions = MakeApiView(expected);
		missingFunctions.hasSelectPage = false;
		CHECK(EvaluateHost(missingFunctions, expected) == HostCompatibility::kMissingFunctions);

		auto noFingerprint = MakeApiView(expected);
		noFingerprint.hasFingerprint = false;
		CHECK(EvaluateHost(noFingerprint, expected) == HostCompatibility::kMissingFingerprint);

		auto shortFingerprint = MakeApiView(expected);
		shortFingerprint.fingerprint.structSize = sizeof(DMUI_ImGuiFingerprint) - 1;
		CHECK(EvaluateHost(shortFingerprint, expected) == HostCompatibility::kMissingFingerprint);

		auto otherCommit = MakeApiView(expected);
		otherCommit.fingerprint.upstreamCommit[0] = 'z';
		CHECK(EvaluateHost(otherCommit, expected) == HostCompatibility::kFingerprintMismatch);

		auto otherVersionNum = MakeApiView(expected);
		otherVersionNum.fingerprint.imguiVersionNum = DMUI_IMGUI_VERSION_NUM + 1u;
		CHECK(EvaluateHost(otherVersionNum, expected) == HostCompatibility::kFingerprintMismatch);

		auto noDocking = MakeApiView(expected);
		noDocking.fingerprint.flags = 0;
		CHECK(EvaluateHost(noDocking, expected) == HostCompatibility::kFingerprintMismatch);

		auto otherLayout = MakeApiView(expected);
		otherLayout.fingerprint.sizeOfImGuiIO += 8;
		CHECK(EvaluateHost(otherLayout, expected) == HostCompatibility::kFingerprintMismatch);

		CHECK(EvaluateHost(MakeApiView(expected), expected) == HostCompatibility::kCompatible);
	}

	void TestRegistrationFailurePolicy()
	{
		IntegrationStateMachine machine;
		CHECK(machine.ChooseRegistered());
		CHECK(!machine.ChooseStandalone());
		CHECK(machine.ChooseStandaloneFromRegistered());
		CHECK(machine.Get() == IntegrationState::kStandalone);
		CHECK(!machine.IsHosted());
		CHECK(DecideBootstrap(machine.Get()) == BootstrapAction::kStandaloneNow);
		CHECK(DecideFallback(machine.Get(), false) == FallbackAction::kNone);
	}

	void TestUnavailableBeforeAndAfterD3D()
	{
		IntegrationStateMachine before;
		CHECK(before.ChooseRegistered());
		CHECK(DecideBootstrap(before.Get()) == BootstrapAction::kDeferForHost);
		CHECK(before.ChooseStandaloneFromRegistered());
		CHECK(DecideFallback(before.Get(), false) == FallbackAction::kNone);
		CHECK(DecideBootstrap(before.Get()) == BootstrapAction::kStandaloneNow);

		IntegrationStateMachine after;
		CHECK(after.ChooseRegistered());
		CHECK(after.MarkUnavailable());
		CHECK(DecideFallback(after.Get(), true) == FallbackAction::kStandaloneFromSavedResources);

		IntegrationStateMachine waiting;
		CHECK(waiting.ChooseRegistered());
		CHECK(DecideFallback(waiting.Get(), true) == FallbackAction::kNone);
	}

	void TestReadyIsOnceAndTerminal()
	{
		IntegrationStateMachine machine;
		CHECK(machine.ChooseRegistered());
		CHECK(machine.MarkReady());
		CHECK(!machine.MarkReady());
		CHECK(machine.IsReady());
		CHECK(machine.IsHosted());

		CHECK(!machine.MarkUnavailable());
		CHECK(!machine.ChooseStandalone());
		CHECK(machine.Get() == IntegrationState::kHostedReady);
		CHECK(DecideBootstrap(machine.Get()) == BootstrapAction::kDeferForHost);
		CHECK(DecideFallback(machine.Get(), true) == FallbackAction::kNone);
	}

	void TestBootstrapDecisions()
	{
		CHECK(DecideBootstrap(IntegrationState::kUndecided) == BootstrapAction::kStandaloneNow);
		CHECK(DecideBootstrap(IntegrationState::kStandalone) == BootstrapAction::kStandaloneNow);
		CHECK(DecideBootstrap(IntegrationState::kRegisteredWaiting) == BootstrapAction::kDeferForHost);
		CHECK(DecideBootstrap(IntegrationState::kHostedReady) == BootstrapAction::kDeferForHost);
		CHECK(DecideBootstrap(IntegrationState::kHostedUnavailable) == BootstrapAction::kStandaloneNow);
	}

	void TestPageCatalog()
	{
		const auto features = MakeFeatures();
		const auto pages = BuildPageCatalog(features);

		CHECK(pages.size() == features.size() + 5);
		CHECK(pages[0].id == "home");
		CHECK(pages[1].id == "general");
		CHECK(pages[2].id == "advanced");
		CHECK(pages[3].id == "presets");
		for (std::size_t i = 0; i < 4; ++i) {
			CHECK(pages[i].category == kBuiltInCategory);
			CHECK(pages[i].kind != HostPageKind::kFeature);
		}
		CHECK(pages[0].sortKey < pages[1].sortKey);
		CHECK(pages[1].sortKey < pages[2].sortKey);
		CHECK(pages[2].sortKey < pages[3].sortKey);

		CHECK(pages[4].displayName == "Performance Overlay");
		CHECK(pages[5].displayName == "RenderDoc");
		CHECK(pages[6].displayName == "Upscaling");
		CHECK(pages[7].displayName == "Wetness Effects");

		const auto* overlayFeature = FindPage(pages, "feature-performance-overlay");
		CHECK(overlayFeature != nullptr);
		CHECK(overlayFeature->kind == HostPageKind::kFeature);
		CHECK(overlayFeature->category == "Performance");
		CHECK(overlayFeature->summary == "FPS counter.");
		CHECK(overlayFeature->featureIndex == 1);

		const auto* renderDoc = FindPage(pages, "feature-renderdoc");
		CHECK(renderDoc != nullptr);
		CHECK(renderDoc->category == kUnloadedCategory);
		const auto* upscaling = FindPage(pages, "feature-upscaling");
		CHECK(upscaling != nullptr);
		CHECK(upscaling->category == kUnloadedCategory);
		CHECK(upscaling->summary == "DLSS and FSR.");

		const auto& overlayPage = pages.back();
		CHECK(overlayPage.id == kOverlayPageId);
		CHECK(overlayPage.kind == HostPageKind::kOverlay);
		CHECK(overlayPage.category == kOverlayCategory);

		auto reversed = features;
		std::reverse(reversed.begin(), reversed.end());
		const auto reorderedPages = BuildPageCatalog(reversed);
		CHECK(reorderedPages.size() == pages.size());
		for (std::size_t i = 0; i < pages.size(); ++i) {
			CHECK(reorderedPages[i].id == pages[i].id);
			CHECK(reorderedPages[i].displayName == pages[i].displayName);
			CHECK(reorderedPages[i].category == pages[i].category);
			CHECK(reorderedPages[i].sortKey == pages[i].sortKey);
		}
	}

	void TestPageIdSanitization()
	{
		CHECK(MakeAsciiId("Wetness Effects") == "wetness-effects");
		CHECK(MakeAsciiId("Screen Space GI") == "screen-space-gi");
		CHECK(MakeAsciiId("  ") == "page");
		CHECK(MakeAsciiId("a.b_c-d") == "a.b_c-d");
		CHECK(MakeAsciiId("Ünicode Name") == "nicode-name");

		const std::vector<FeaturePageInput> colliding{
			FeaturePageInput{ .name = "My Feature", .displayName = "A", .category = "Misc", .active = true },
			FeaturePageInput{ .name = "My/Feature", .displayName = "B", .category = "Misc", .active = true }
		};
		const auto pages = BuildPageCatalog(colliding);
		CHECK(FindPage(pages, "feature-my-feature") != nullptr);
		CHECK(FindPage(pages, "feature-my-feature-2") != nullptr);
	}

	void TestOverlayDemandBalance()
	{
		OverlayDemandModel model;
		CHECK(!model.Held());
		CHECK(model.Plan(false) == OverlayDemandModel::Action::kNone);

		CHECK(model.Plan(true) == OverlayDemandModel::Action::kRequest);
		CHECK(model.Plan(true) == OverlayDemandModel::Action::kRequest);
		model.Confirm(OverlayDemandModel::Action::kRequest);
		CHECK(model.Held());
		CHECK(model.Plan(true) == OverlayDemandModel::Action::kNone);

		CHECK(model.Plan(false) == OverlayDemandModel::Action::kRelease);
		model.Confirm(OverlayDemandModel::Action::kRelease);
		CHECK(!model.Held());
		CHECK(model.Plan(false) == OverlayDemandModel::Action::kNone);

		int held = 0;
		bool wanted = false;
		for (int i = 0; i < 32; ++i) {
			wanted = (i % 3) != 0;
			const auto action = model.Plan(wanted);
			if (action == OverlayDemandModel::Action::kRequest)
				++held;
			else if (action == OverlayDemandModel::Action::kRelease)
				--held;
			model.Confirm(action);
			CHECK(held == (model.Held() ? 1 : 0));
			CHECK(held >= 0 && held <= 1);
		}
	}
}

int main()
{
	TestAbsentHost();
	TestCandidateSelectionIsDeterministic();
	TestIncompatibleHosts();
	TestRegistrationFailurePolicy();
	TestUnavailableBeforeAndAfterD3D();
	TestReadyIsOnceAndTerminal();
	TestBootstrapDecisions();
	TestPageCatalog();
	TestPageIdSanitization();
	TestOverlayDemandBalance();

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "Host integration tests passed\n";
	return 0;
}
