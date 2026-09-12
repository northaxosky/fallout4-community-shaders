#include "Host/HostPageCatalog.h"
#include "Host/HostClientOptions.h"
#include "Host/HostRuntimeModel.h"
#include "Menu/DebugViewSelection.h"

#include <DearModdingUI/Client.h>
#include <DearModdingUI/IconGlyphs.h>

#include <algorithm>
#include <iostream>
#include <ranges>
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

	DMUI_HostServices supportedServices = cs::host::kClientOptions.requiredServices;
	std::uint32_t uiRevision = DMUI_UI_REVISION_CURRENT;
	std::uint32_t uiTableSize = DMUI_UI_API_REQUIRED_SIZE;
	DMUI_Result uiQueryResult = DMUI_RESULT_OK;
	bool missingRequiredUIOperation = false;
	bool missingOptionalPlotLines = false;

	template <class>
	struct UIStub;

	template <class Result, class... Arguments>
	struct UIStub<Result (*)(Arguments...) noexcept>
	{
		static Result Call(Arguments...) noexcept
		{
			return static_cast<Result>(DMUI_RESULT_OK);
		}
	};

	DMUI_UIAPI MakeUIAPI()
	{
		DMUI_UIAPI api{};
		api.structSize = DMUI_UI_API_CURRENT_SIZE;
		api.abiVersion = DMUI_UI_ABI_CURRENT;
		api.revision = DMUI_UI_REVISION_CURRENT;
#define CS_UI_STUB(a_member) \
	api.a_member = &UIStub<decltype(api.a_member)>::Call
		CS_UI_STUB(getStyleMetrics);
		CS_UI_STUB(beginCombo);
		CS_UI_STUB(endCombo);
		CS_UI_STUB(beginDisabled);
		CS_UI_STUB(endDisabled);
		CS_UI_STUB(beginTable);
		CS_UI_STUB(endTable);
		CS_UI_STUB(beginTooltip);
		CS_UI_STUB(endTooltip);
		CS_UI_STUB(button);
		CS_UI_STUB(calcTextSize);
		CS_UI_STUB(checkbox);
		CS_UI_STUB(collapsingHeader);
		CS_UI_STUB(collapsingHeaderVisible);
		CS_UI_STUB(dragScalar);
		CS_UI_STUB(dummy);
		CS_UI_STUB(getContentRegionAvail);
		CS_UI_STUB(getCursorScreenPos);
		CS_UI_STUB(getFontSize);
		CS_UI_STUB(getFrameHeight);
		CS_UI_STUB(getStyleColor);
		CS_UI_STUB(getTextLineHeightWithSpacing);
		CS_UI_STUB(indent);
		CS_UI_STUB(inputScalar);
		CS_UI_STUB(inputText);
		CS_UI_STUB(inputTextMultiline);
		CS_UI_STUB(inputTextWithHint);
		CS_UI_STUB(isItemDeactivatedAfterEdit);
		CS_UI_STUB(isItemHovered);
		CS_UI_STUB(popID);
		CS_UI_STUB(popStyleColor);
		CS_UI_STUB(popTextWrapPos);
		CS_UI_STUB(progressBar);
		CS_UI_STUB(pushIDString);
		CS_UI_STUB(pushIDRange);
		CS_UI_STUB(pushIDValue);
		CS_UI_STUB(pushStyleColorU32);
		CS_UI_STUB(pushStyleColor);
		CS_UI_STUB(pushTextWrapPos);
		CS_UI_STUB(sameLine);
		CS_UI_STUB(selectable);
		CS_UI_STUB(selectableToggle);
		CS_UI_STUB(separator);
		CS_UI_STUB(setClipboardText);
		CS_UI_STUB(setCursorScreenPos);
		CS_UI_STUB(setItemDefaultFocus);
		CS_UI_STUB(setNextItemWidth);
		CS_UI_STUB(setTooltipText);
		CS_UI_STUB(sliderScalar);
		CS_UI_STUB(spacing);
		CS_UI_STUB(tableHeadersRow);
		CS_UI_STUB(tableNextColumn);
		CS_UI_STUB(tableNextRow);
		CS_UI_STUB(tableSetColumnIndex);
		CS_UI_STUB(tableSetupColumn);
		CS_UI_STUB(tableSetupScrollFreeze);
		CS_UI_STUB(text);
		CS_UI_STUB(textColored);
		CS_UI_STUB(textDisabled);
		CS_UI_STUB(textWrapped);
		CS_UI_STUB(unindent);
		CS_UI_STUB(newLine);
		CS_UI_STUB(plotLines);
#undef CS_UI_STUB
		return api;
	}

	DMUI_Result DMUI_CALL RegisterClient(
		const DMUI_ClientDescriptor*,
		DMUI_ClientHandle*) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL RegisterPage(
		DMUI_ClientHandle,
		const DMUI_PageDescriptor*,
		DMUI_PageHandle*) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL RegisterCategory(
		DMUI_ClientHandle,
		const DMUI_CategoryDescriptor*) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL QueryServices(DMUI_HostServicesInfo* a_info) noexcept
	{
		a_info->supportedServices = supportedServices;
		return DMUI_RESULT_OK;
	}

	DMUI_Result DMUI_CALL QueryUIAPI(
		std::uint32_t a_requestedUIAbi,
		std::uint32_t a_minimumRevision,
		std::uint32_t a_minimumTableSize,
		DMUI_UIAPIInfo* a_info) noexcept
	{
		static DMUI_UIAPI api = MakeUIAPI();
		api = MakeUIAPI();
		api.structSize = uiTableSize;
		api.revision = uiRevision;
		if (missingRequiredUIOperation)
			api.endCombo = nullptr;
		if (missingOptionalPlotLines)
			api.plotLines = nullptr;
		if (!a_info || a_info->structSize < DMUI_UI_API_INFO_1_SIZE)
			return DMUI_RESULT_STRUCT_TOO_SMALL;
		a_info->abiVersion = api.abiVersion;
		a_info->revision = api.revision;
		a_info->tableSize = api.structSize;
		a_info->api = nullptr;
		if (uiQueryResult != DMUI_RESULT_OK)
			return uiQueryResult;
		if (a_requestedUIAbi != api.abiVersion ||
			a_minimumRevision > api.revision ||
			a_minimumTableSize > api.structSize)
			return DMUI_RESULT_UNSUPPORTED_ABI;
		a_info->api = &api;
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

	DMUI_Result DMUI_CALL OpenExternal(
		DMUI_ClientHandle,
		const DMUI_ExternalOpenDescriptor*,
		std::uint32_t*) noexcept
	{
		return DMUI_RESULT_OK;
	}

	DMUI_HostAPI MakeHost()
	{
		DMUI_HostAPI api{};
		api.structSize = sizeof(api);
		api.hostAbiVersion = DMUI_HOST_ABI_CURRENT;
		api.apiVersion = DMUI_API_VERSION_CURRENT;
		api.registerClient = &RegisterClient;
		api.registerPage = &RegisterPage;
		api.registerCategory = &RegisterCategory;
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
		api.openExternal = &OpenExternal;
		api.queryUIAPI = &QueryUIAPI;
		return api;
	}

	void TestUIPreflight()
	{
		const auto& options = cs::host::kClientOptions;
		CHECK(options.minimumUIRevision == DMUI_UI_REVISION_1);
		CHECK(options.minimumUIAPISize == DMUI_UI_API_REQUIRED_SIZE);
		auto api = MakeHost();
		CHECK(
			dmui::PreflightHostAPI(nullptr, options) ==
			DMUI_RESULT_UNSUPPORTED_ABI);
		CHECK(dmui::PreflightHostAPI(&api, options) == DMUI_RESULT_OK);

		supportedServices &= ~DMUI_HOST_SERVICE_EXTERNAL_OPEN;
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_SERVICE_UNAVAILABLE);
		supportedServices = options.requiredServices;

		supportedServices &= ~DMUI_HOST_SERVICE_VIRTUAL_FILE_TARGETS;
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_SERVICE_UNAVAILABLE);
		supportedServices = options.requiredServices;

		supportedServices &= ~DMUI_HOST_SERVICE_NAVIGATION_ICONS;
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_SERVICE_UNAVAILABLE);
		supportedServices = options.requiredServices;

		api.registerPage = nullptr;
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_SERVICE_UNAVAILABLE);
		api.registerPage = &RegisterPage;

		api.registerCategory = nullptr;
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_SERVICE_UNAVAILABLE);
		api.registerCategory = &RegisterCategory;

		api.openExternal = nullptr;
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_SERVICE_UNAVAILABLE);
		api.openExternal = &OpenExternal;

		api.structSize = DMUI_HOST_API_REGISTER_CATEGORY_SIZE;
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_UNSUPPORTED_ABI);
		api.structSize = sizeof(api);

		api.hostAbiVersion = 0;
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_UNSUPPORTED_ABI);
		api.hostAbiVersion = DMUI_HOST_ABI_CURRENT;

		api.apiVersion = 0;
		CHECK(dmui::PreflightHostAPI(&api, options) == DMUI_RESULT_OK);
		api.apiVersion = DMUI_API_VERSION_CURRENT;

		uiRevision = 0;
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_UNSUPPORTED_ABI);
		uiRevision = DMUI_UI_REVISION_CURRENT;

		uiTableSize = DMUI_UI_API_REQUIRED_SIZE - sizeof(void*);
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_UNSUPPORTED_ABI);
		uiTableSize = DMUI_UI_API_REQUIRED_SIZE;

		missingRequiredUIOperation = true;
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_UNSUPPORTED_ABI);
		missingRequiredUIOperation = false;

		uiTableSize = DMUI_UI_API_CURRENT_SIZE;
		missingOptionalPlotLines = true;
		CHECK(dmui::PreflightHostAPI(&api, options) == DMUI_RESULT_OK);
		auto plotOptions = options;
		plotOptions.minimumUIAPISize = DMUI_UI_API_PLOT_LINES_SIZE;
		CHECK(
			dmui::PreflightHostAPI(&api, plotOptions) ==
			DMUI_RESULT_UNSUPPORTED_ABI);
		missingOptionalPlotLines = false;
		CHECK(dmui::PreflightHostAPI(&api, plotOptions) == DMUI_RESULT_OK);
		uiTableSize = DMUI_UI_API_REQUIRED_SIZE;

		uiQueryResult = DMUI_RESULT_HOST_NOT_READY;
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_HOST_NOT_READY);
		uiQueryResult = DMUI_RESULT_OK;

		api.queryUIAPI = nullptr;
		CHECK(
			dmui::PreflightHostAPI(&api, options) ==
			DMUI_RESULT_UNSUPPORTED_ABI);
		api.queryUIAPI = &QueryUIAPI;

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
			{ "FrameGeneration", "Frame Generation", "Performance", "Temporal FG.", false, true },
			{ "BlankCategory", "Blank Category", "", "No category.", true, true },
			{ "MiscFeature", "Misc Feature", "Misc", "Miscellaneous.", true, true },
			{ "Collision!", "Collision A", "Effects!", "Custom.", true, true },
			{ "Collision?", "Collision B", "Effects!", "Custom.", true, true },
			{ "CustomTwo", "Custom Two", "Effects?", "Custom.", true, true },
			{ "ReservedCollision", "Reserved Collision", "General!", "Custom.", true, true },
			{ "PostProcess", "Post Process", "Post-process", "Post.", true, true }
		};
		const auto catalog = cs::host::BuildPageCatalog(features);
		const auto& pages = catalog.pages;
		const auto& categories = catalog.categories;
		CHECK(pages.size() == features.size() + 4);
		CHECK(pages.front().id == "home");
		CHECK(pages.front().categoryId == cs::host::kGeneralCategoryId);
		CHECK(DearModdingUI::ResolveClientIconGlyph(
			cs::host::kClientIconName, {}, "Community Shaders") ==
			DearModdingUI::ResolveNamedIconGlyphOrZero("cloud-sun"));
		CHECK(DearModdingUI::ResolveCategoryIconGlyph(
			cs::host::kGeneralCategory, "Community Shaders",
			"dearmodding.community-shaders", cs::host::kClientIconName) ==
			DearModdingUI::ResolveNamedIconGlyphOrZero("gear"));
		CHECK(std::ranges::none_of(pages, [](const auto& page) {
			return page.id == "general";
		}));
		CHECK(pages.back().id == cs::host::kOverlayPageId);
		CHECK(pages.back().categoryId == cs::host::kOverlayCategoryId);
		CHECK(std::ranges::any_of(pages, [](const auto& page) {
			return page.id == "feature-renderdoc" &&
				page.categoryId == cs::host::kUnloadedCategoryId;
		}));
		CHECK(std::ranges::any_of(pages, [](const auto& page) {
			return page.id == "feature-performance-overlay";
		}));
		CHECK(std::ranges::any_of(pages, [](const auto& page) {
			return page.id == "feature-upscaling" &&
				page.categoryId == "performance";
		}));
		CHECK(std::ranges::any_of(pages, [](const auto& page) {
			return page.id == "feature-framegeneration" &&
				page.categoryId == cs::host::kUnloadedCategoryId;
		}));

		const std::vector<std::string_view> expectedCategoryIds{
			"general",
			"lighting",
			"post-process",
			"performance",
			"misc",
			"other",
			"effects",
			"effects-2",
			"general-2",
			"unloaded",
			"overlay"
		};
		CHECK(categories.size() == expectedCategoryIds.size());
		for (std::size_t index = 0;
			 index < (std::min)(categories.size(), expectedCategoryIds.size());
			 ++index) {
			CHECK(categories[index].id == expectedCategoryIds[index]);
		}
		CHECK(std::ranges::none_of(categories, [](const auto& category) {
			return category.displayName == "Dev Tools";
		}));
		const auto lighting = std::ranges::find(
			categories, "lighting", &cs::host::HostCategoryDescriptor::id);
		CHECK(lighting != categories.end());
		if (lighting != categories.end()) {
			CHECK(lighting->displayName == "Lighting");
			CHECK(lighting->iconName == "sun-horizon");
			CHECK(DearModdingUI::ResolveCategoryIconGlyph(
				lighting->displayName,
				"Community Shaders",
				"dearmodding.community-shaders",
				cs::host::kClientIconName,
				lighting->iconName) ==
				DearModdingUI::ResolveNamedIconGlyphOrZero("sun-horizon"));
		}
		CHECK(std::ranges::all_of(categories, [](const auto& category) {
			return category.id == "lighting" || category.iconName.empty();
		}));

		const auto findPage = [&pages](std::string_view a_id) {
			return std::ranges::find(pages, a_id, &cs::host::HostPageDescriptor::id);
		};
		const auto checkPageCategory =
			[&pages, &findPage](std::string_view a_id, std::string_view a_categoryId) {
				const auto page = findPage(a_id);
				CHECK(page != pages.end());
				if (page != pages.end())
					CHECK(page->categoryId == a_categoryId);
			};
		checkPageCategory("feature-blankcategory", "other");
		checkPageCategory("feature-collision", "effects");
		checkPageCategory("feature-collision-2", "effects");
		checkPageCategory("feature-customtwo", "effects-2");
		checkPageCategory("feature-reservedcollision", "general-2");
		CHECK(std::ranges::all_of(pages, [&categories](const auto& page) {
			return std::ranges::any_of(
				categories,
				[&page](const auto& category) {
					return category.id == page.categoryId;
				});
		}));
		for (const auto& page : pages) {
			if (page.kind == cs::host::HostPageKind::kFeature) {
				CHECK(page.featureIndex < features.size());
				if (page.featureIndex < features.size())
					CHECK(features[page.featureIndex].displayName == page.displayName);
			}
		}

		auto reordered = features;
		std::ranges::reverse(reordered);
		const auto reorderedCatalog = cs::host::BuildPageCatalog(reordered);
		CHECK(reorderedCatalog.categories.size() == categories.size());
		for (std::size_t index = 0;
			 index < (std::min)(categories.size(), reorderedCatalog.categories.size());
			 ++index) {
			CHECK(reorderedCatalog.categories[index].id == categories[index].id);
			CHECK(
				reorderedCatalog.categories[index].displayName ==
				categories[index].displayName);
			CHECK(
				reorderedCatalog.categories[index].sortKey ==
				categories[index].sortKey);
			CHECK(
				reorderedCatalog.categories[index].iconName ==
				categories[index].iconName);
		}
		CHECK(reorderedCatalog.pages.size() == pages.size());
		for (std::size_t index = 0;
			 index < (std::min)(pages.size(), reorderedCatalog.pages.size());
			 ++index) {
			CHECK(reorderedCatalog.pages[index].id == pages[index].id);
			CHECK(
				reorderedCatalog.pages[index].displayName ==
				pages[index].displayName);
			CHECK(
				reorderedCatalog.pages[index].categoryId ==
				pages[index].categoryId);
			if (reorderedCatalog.pages[index].kind ==
				cs::host::HostPageKind::kFeature) {
				CHECK(
					reorderedCatalog.pages[index].featureIndex <
					reordered.size());
				if (reorderedCatalog.pages[index].featureIndex <
					reordered.size()) {
					CHECK(
						reordered[reorderedCatalog.pages[index].featureIndex]
							.displayName ==
						reorderedCatalog.pages[index].displayName);
				}
			}
		}
	}

	void TestCategoryIdValidity()
	{
		const std::string longCategory(200, 'A');
		const std::vector<cs::host::FeaturePageInput> features{
			{ "LongCategory", "Long Category", longCategory, "", true, true },
			{ "PunctuationCategory", "Punctuation Category", "!!!", "", true, true }
		};
		const auto catalog = cs::host::BuildPageCatalog(features);
		for (const auto& category : catalog.categories) {
			CHECK(!category.id.empty());
			CHECK(category.id.size() <= 128);
			CHECK(std::ranges::all_of(category.id, [](char character) {
				return
					(character >= 'a' && character <= 'z') ||
					(character >= '0' && character <= '9') ||
					character == '.' ||
					character == '_' ||
					character == '-';
			}));
		}

	}

	void TestChoiceActivation()
	{
		const dmui::ChoiceOption<int> selected{ 2, "Balanced", "balanced" };
		const auto noOp =
			dmui::presentation_detail::ResolveChoiceActivation(2, selected, true);
		CHECK(!noOp.changed);
		CHECK(!noOp.completed);
		CHECK(!noOp.selected.has_value());

		const auto changed =
			dmui::presentation_detail::ResolveChoiceActivation(1, selected, true);
		CHECK(changed.changed);
		CHECK(changed.completed);
		CHECK(changed.selected == 2);

		auto disabled = selected;
		disabled.enabled = false;
		const auto disabledResult =
			dmui::presentation_detail::ResolveChoiceActivation(1, disabled, true);
		CHECK(!disabledResult.changed);
		CHECK(!disabledResult.selected.has_value());

		const auto inactive =
			dmui::presentation_detail::ResolveChoiceActivation(1, selected, false);
		CHECK(!inactive.changed);
		CHECK(!inactive.selected.has_value());
	}

	void TestEmptyPageCatalog()
	{
		const auto catalog =
			cs::host::BuildPageCatalog(std::vector<cs::host::FeaturePageInput>{});
		CHECK(catalog.categories.size() == 2);
		CHECK(catalog.categories[0].id == cs::host::kGeneralCategoryId);
		CHECK(catalog.categories[1].id == cs::host::kOverlayCategoryId);
		CHECK(catalog.pages.size() == 4);
		CHECK(std::ranges::all_of(catalog.pages, [&catalog](const auto& page) {
			return std::ranges::any_of(
				catalog.categories,
				[&page](const auto& category) {
					return category.id == page.categoryId;
				});
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
	TestUIPreflight();
	TestPageCatalog();
	TestChoiceActivation();
	TestCategoryIdValidity();
	TestEmptyPageCatalog();
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
