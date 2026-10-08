#include "RenderDoc.h"

#include <renderdoc_app.h>

#include <DearModdingUI/Client.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <system_error>

#include "F4SE/API.h"
#include "Host/HostClient.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "REX/CONVERT.h"
#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.renderdoc");
	}

	constexpr std::string_view kLegacyCaptureFolder = "Data\\F4SE\\Plugins\\RenderDoc\\captures";
	using renderdoc_settings::kEngineD3D11Target;
	using renderdoc_settings::kTemporalD3D12Target;

	RenderDoc* RenderDoc::GetSingleton()
	{
		static RenderDoc singleton;
		return &singleton;
	}

	namespace
	{
		int ClampCaptureFrameCount(int a_value)
		{
			return std::clamp(a_value, renderdoc::kMinCaptureFrameCount, renderdoc::kMaxCaptureFrameCount);
		}

		std::string RuntimeName()
		{
			switch (REX::FModule::GetRuntimeIndex()) {
			case REX::FModule::Runtime::kOG:
				return "OG";
			case REX::FModule::Runtime::kNG:
				return "NG";
			default:
				return "AE";
			}
		}

		std::string_view CaptureTargetConfigName(RenderDoc::CaptureTarget a_target)
		{
			switch (a_target) {
			case RenderDoc::CaptureTarget::kTemporalD3D12:
				return kTemporalD3D12Target;
			case RenderDoc::CaptureTarget::kEngineD3D11:
			default:
				return kEngineD3D11Target;
			}
		}

		const char* CaptureTargetDisplayName(RenderDoc::CaptureTarget a_target)
		{
			switch (a_target) {
			case RenderDoc::CaptureTarget::kTemporalD3D12:
				return "Temporal D3D12";
			case RenderDoc::CaptureTarget::kEngineD3D11:
			default:
				return "Engine D3D11";
			}
		}

		std::string PathToUtf8(const std::filesystem::path& a_path)
		{
			std::string utf8;
			if (REX::UTF16_TO_UTF8(a_path.native(), utf8)) {
				return utf8;
			}
			return a_path.string();
		}

		// FO4: the host owns native external opening, including failure reporting.
		void OpenCaptureLocation(const std::filesystem::path& a_path, DMUI_ExternalTargetKind a_kind)
		{
			const auto utf8 = PathToUtf8(std::filesystem::absolute(a_path));
			auto& client = host::HostClient::Get().Client();
			std::uint32_t nativeError{};
			if (!client.OpenExternal({ .targetKind = a_kind, .target = utf8.c_str() }, &nativeError)) {
				L->warn("Failed to open '{}': {} (native error {})",
					utf8, DMUI_ResultToString(client.LastResult()), nativeError);
				cs::Menu::ShowToast("Could not open the capture location; see log.", 4.0, DMUI_STATUS_SEVERITY_ERROR, "RenderDoc");
			}
		}

		std::filesystem::path ExpandPathEnvironment(const std::wstring& a_configured)
		{
			const auto requiredSize = REX::W32::ExpandEnvironmentStringsW(a_configured.c_str(), nullptr, 0);
			if (requiredSize == 0)
				throw std::system_error(static_cast<int>(REX::W32::GetLastError()), std::system_category());

			std::wstring expanded(requiredSize, L'\0');
			const auto expandedSize = REX::W32::ExpandEnvironmentStringsW(
				a_configured.c_str(), expanded.data(), requiredSize);
			if (expandedSize == 0)
				throw std::system_error(static_cast<int>(REX::W32::GetLastError()), std::system_category());
			if (expandedSize > requiredSize)
				throw std::runtime_error("environment changed during path expansion");

			expanded.resize(expandedSize - 1);
			return expanded;
		}

		// FO4: the registered .rdc handler locates renderdoc.dll beside qrenderdoc.exe.
		std::filesystem::path InstalledRuntimePath()
		{
			constexpr auto* key = L"RenderDoc.RDCCapture.1\\shell\\open\\command";
			DWORD bytes = 0;
			if (::RegGetValueW(HKEY_CLASSES_ROOT, key, nullptr, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) == ERROR_SUCCESS) {
				std::wstring command(bytes / sizeof(wchar_t), L'\0');
				if (::RegGetValueW(HKEY_CLASSES_ROOT, key, nullptr, RRF_RT_REG_SZ, nullptr, command.data(), &bytes) == ERROR_SUCCESS &&
					command.starts_with(L'"')) {
					const std::filesystem::path editor(command.substr(1, command.find(L'"', 1) - 1));
					if (editor.is_absolute())
						return editor.parent_path() / L"renderdoc.dll";
				}
			}
			return ExpandPathEnvironment(L"%ProgramFiles%\\RenderDoc\\renderdoc.dll");
		}

		std::filesystem::path ResolveRuntimePath(const std::string& a_configured)
		{
			if (a_configured.empty())
				return InstalledRuntimePath();
			std::wstring configured;
			if (!REX::UTF8_TO_UTF16(a_configured, configured))
				throw std::invalid_argument("DLL path is not valid UTF-8");
			return std::filesystem::absolute(ExpandPathEnvironment(configured)).lexically_normal();
		}

		std::filesystem::path ExpandCaptureFolderEnvironment(const std::string& a_configured)
		{
			std::wstring configured;
			if (!REX::UTF8_TO_UTF16(a_configured, configured)) {
				L->warn("Failed to decode RenderDoc capture folder as UTF-8; using it without environment expansion");
				return a_configured;
			}

			try {
				return ExpandPathEnvironment(configured);
			} catch (const std::exception&) {
				L->warn("Failed to expand environment variables in RenderDoc capture folder; using the configured path");
				return configured;
			}
		}

		std::filesystem::path ResolveCaptureFolder(const std::string& a_configured)
		{
			// FO4: captures follow the F4SE save-folder identity rather than the game Data tree.
			if (!a_configured.empty()) {
				return ExpandCaptureFolderEnvironment(a_configured);
			}

			auto saveFolderName = F4SE::GetSaveFolderName();
			if (saveFolderName.empty()) {
				saveFolderName = "Fallout4";
			}

			wchar_t* knownBuffer = nullptr;
			const auto knownResult = REX::W32::SHGetKnownFolderPath(
				REX::W32::FOLDERID_Documents,
				REX::W32::KF_FLAG_DEFAULT,
				nullptr,
				std::addressof(knownBuffer));
			std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> knownPath(
				knownBuffer, REX::W32::CoTaskMemFree);
			if (!knownPath || knownResult != 0) {
				L->warn("Failed to resolve the Documents folder for RenderDoc captures; using {}", kLegacyCaptureFolder);
				return kLegacyCaptureFolder;
			}

			std::filesystem::path path = knownPath.get();
			path /= std::format("My Games/{}/F4SE/FO4CommunityShaders/captures", saveFolderName);
			return path;
		}

	}

	bool RenderDoc::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!renderdoc_settings::Parse(a_config, candidate, a_error)) {
			return false;
		}

		_settings = candidate;
		_bootSettings = candidate;
		return true;
	}

	void RenderDoc::Load()
	{
		L->info("Settings: dll={} folder={} capture_frame_count={} capture_target={} capture={} alternate_capture={}",
			_settings.dllPath, _settings.captureFolder,
			_settings.captureFrameCount,
			CaptureTargetConfigName(_settings.captureTarget),
			_settings.captureHotkey, _settings.multiCaptureHotkey);

		// FO4: startup feature loading is the only runtime enable switch.
		std::string error;
		if (!TryLoadRuntime(error))
			FailLoad(std::move(error));
	}

	bool RenderDoc::SaveSettings()
	{
		return settings::SaveDelta(renderdoc_settings::kSchema, GetConfigKey(), _settings, *L);
	}

	bool RenderDoc::TryLoadRuntime(std::string& a_error)
	{
		if (_api)
			return true;
		if (_attemptedLoad)
			return false;
		_attemptedLoad = true;

		std::filesystem::path path;
		try {
			path = ResolveRuntimePath(_settings.dllPath);
		} catch (const std::exception& error) {
			a_error = std::format("RenderDoc runtime path resolution failed for settings.dll_path '{}': {}", _settings.dllPath, error.what());
			return false;
		}
		const auto pathUtf8 = PathToUtf8(path);
		// Post-D3D loading can crash.
		_module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!_module) {
			const auto error = GetLastError();
			auto reason = std::system_category().message(static_cast<int>(error));
			reason.erase(reason.find_last_not_of("\r\n") + 1);
			a_error = std::format("RenderDoc runtime load failed for '{}': LoadLibraryExW error {} ({})", pathUtf8, error, reason);
			return false;
		}

		auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(_module, "RENDERDOC_GetAPI"));
		if (!getApi) {
			a_error = std::format("RenderDoc runtime load failed for '{}': missing RENDERDOC_GetAPI export", pathUtf8);
			FreeLibrary(_module);
			_module = nullptr;
			return false;
		}

		if (getApi(eRENDERDOC_API_Version_1_7_0, reinterpret_cast<void**>(&_api)) != 1 || !_api) {
			a_error = std::format("RenderDoc runtime load failed for '{}': RENDERDOC_GetAPI rejected API version 1.7.0", pathUtf8);
			_api = nullptr;
			FreeLibrary(_module);
			_module = nullptr;
			return false;
		}

		_api->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
		_api->SetCaptureKeys(nullptr, 0);

		// Without NvAPI passthrough the driver removes the device at the first Present.
		if (_api->SetCaptureOptionU32(
				eRENDERDOC_Option_AllowUnsupportedVendorExtensions, 0x10DE) != 1)
			L->warn("renderdoc.dll rejected NvAPI passthrough; captures may remove the device");

		ApplyCapturePath();
		_captures.Attach(_api);
		_captureCount.store(_captures.Count(), std::memory_order_relaxed);
		L->info("RenderDoc runtime loaded from '{}'", pathUtf8);
		return true;
	}

	void RenderDoc::ApplyCapturePath()
	{
		if (!_api)
			return;

		_resolvedCaptureFolder = ResolveCaptureFolder(_settings.captureFolder);
		_resolvedCaptureFolderUtf8 = PathToUtf8(_resolvedCaptureFolder);
		L->info("RenderDoc capture folder: {}", _resolvedCaptureFolderUtf8);

		std::error_code ec;
		std::filesystem::create_directories(_resolvedCaptureFolder, ec);
		if (ec)
			L->warn("Failed to prepare RenderDoc capture folder: {}", ec.message());
		_captures.SetDirectory(_resolvedCaptureFolder);
		// FO4: runtime identity comes from CommonLibF4; the session stamp keeps names unique.
		static const auto sessionStart = std::chrono::current_zone()->to_local(
			std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
		auto pathTemplate = PathToUtf8(_resolvedCaptureFolder /
									   std::format("Fallout4_{}_{}_{:%Y%m%d_%H%M%S}", RuntimeName(), REX::FModule::GetCurrentModule().GetFileVersion().string("."), sessionStart));
		_api->SetCaptureFilePathTemplate(pathTemplate.c_str());
	}

	bool RenderDoc::CheckCaptureDiskSpace(int a_frames) const
	{
		try {
			if (!_captures.HasSufficientDiskSpace(a_frames)) {
				const auto requiredMiB = renderdoc::RequiredSpaceBytes(a_frames) / (1024 * 1024);
				L->warn("RenderDoc capture aborted: at least {} MiB free required in {}",
					requiredMiB, _resolvedCaptureFolderUtf8);
				cs::Menu::ShowToast(
					std::format("RenderDoc capture aborted: at least {} MiB free required.", requiredMiB),
					4.0,
					DMUI_STATUS_SEVERITY_WARNING,
					"RenderDoc");
				return false;
			}
		} catch (const std::filesystem::filesystem_error& e) {
			L->warn("RenderDoc capture aborted: failed to query free disk space for {}: {}",
				_resolvedCaptureFolderUtf8, e.what());
			cs::Menu::ShowToast(
				"RenderDoc capture aborted: disk check failed",
				4.0,
				DMUI_STATUS_SEVERITY_ERROR,
				"RenderDoc");
			return false;
		}

		return true;
	}

	RenderDoc::CaptureBinding RenderDoc::GetCaptureBinding() const
	{
		std::scoped_lock lock(_captureTargetMutex);
		CaptureBinding binding;
		switch (_settings.captureTarget) {
		case CaptureTarget::kTemporalD3D12:
			binding.device.copy_from(_device12.get());
			binding.window = _window12;
			break;
		case CaptureTarget::kEngineD3D11:
		default:
			binding.device.copy_from(_device11.get());
			binding.window = _window11;
			break;
		}
		return binding;
	}

	bool RenderDoc::CaptureTargetAvailable() const noexcept
	{
		switch (_settings.captureTarget) {
		case CaptureTarget::kTemporalD3D12:
			return _d3d12TargetAvailable.load(std::memory_order_acquire);
		case CaptureTarget::kEngineD3D11:
		default:
			return _d3d11TargetAvailable.load(std::memory_order_acquire);
		}
	}

	bool RenderDoc::BindCaptureTarget(bool a_reportUnavailable)
	{
		if (!_api || !_api->SetActiveWindow) {
			if (a_reportUnavailable) {
				L->warn(
					"RenderDoc capture aborted: the runtime does not expose "
					"SetActiveWindow");
				cs::Menu::ShowToast(
					"RenderDoc capture target binding is unavailable",
					4.0,
					DMUI_STATUS_SEVERITY_ERROR,
					"RenderDoc");
			}
			return false;
		}

		const auto binding = GetCaptureBinding();
		if (!binding.device || !binding.window) {
			if (a_reportUnavailable) {
				const auto* name =
					CaptureTargetDisplayName(_settings.captureTarget);
				L->warn(
					"RenderDoc capture aborted: selected target {} is unavailable",
					name);
				cs::Menu::ShowToast(
					std::format(
						"RenderDoc capture target unavailable: {}",
						name),
					4.0,
					DMUI_STATUS_SEVERITY_ERROR,
					"RenderDoc");
			}
			return false;
		}

		if (!FramesEngineCaptureManually())
			_api->SetActiveWindow(binding.device.get(), binding.window);
		return true;
	}

	void RenderDoc::ApplyPendingComments()
	{
		if (!_api)
			return;

		const auto count = _captures.Count();
		if (count != _captureCount.load(std::memory_order_relaxed))
			_captures.Poll(BuildAutomaticCaptureComments());
		_captureCount.store(count, std::memory_order_relaxed);
	}

	std::string RenderDoc::BuildAutomaticCaptureComments(std::string_view a_userComments) const
	{
		// FO4: the host supplies identity and loaded-feature metadata to the capture service.
		auto comments = std::format("Fallout 4 {} {}\nCommunity Shaders {}\n",
			RuntimeName(), REX::FModule::GetCurrentModule().GetFileVersion().string("."), Plugin::VERSION.string("."));
		std::vector<std::string> features;
		for (const auto* feature : FeatureManager::Get().GetAll()) {
			if (feature->IsLoaded())
				features.push_back(std::format("{} ({})", feature->GetName(), Plugin::VERSION.string(".")));
		}
		std::ranges::sort(features);
		if (!features.empty()) {
			comments += "Enabled Features:\n";
			for (const auto& feature : features)
				comments += std::format("- {}\n", feature);
		}
		if (!a_userComments.empty())
			comments += std::format("\nUser Comments:\n{}", a_userComments);
		return comments;
	}

	void RenderDoc::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("loaded", _api != nullptr)
			.Field("attempted", _attemptedLoad)
			.Field(
				"capture_target",
				CaptureTargetConfigName(_settings.captureTarget))
			.Field("capture_target_available", CaptureTargetAvailable())
			.Field(
				"engine_d3d11_available",
				_d3d11TargetAvailable.load(std::memory_order_relaxed))
			.Field(
				"temporal_d3d12_available",
				_d3d12TargetAvailable.load(std::memory_order_relaxed))
			.Field("captures", static_cast<std::int64_t>(_captureCount.load(std::memory_order_relaxed)))
			.Field("capture_frames", static_cast<std::int64_t>(_settings.captureFrameCount))
			.Field("folder", _resolvedCaptureFolderUtf8);
	}

	bool RenderDoc::FramesEngineCaptureManually() const noexcept
	{
		// FO4: the temporal proxy presents through D3D12, so game-device captures need explicit frame boundaries.
		return _settings.captureTarget == CaptureTarget::kEngineD3D11 &&
		       _d3d12TargetAvailable.load(std::memory_order_acquire);
	}

	bool RenderDoc::RequestFrames(std::uint32_t a_frames)
	{
		if (!BindCaptureTarget(true) || !CheckCaptureDiskSpace(static_cast<int>(a_frames)))
			return false;
		if (_commentsBuf[0]) {
			_captures.QueueComments(BuildAutomaticCaptureComments(_commentsBuf.data()));
			_commentsBuf[0] = 0;
		}
		if (FramesEngineCaptureManually()) {
			_manualFramesPending.store(a_frames, std::memory_order_release);
		} else {
			_captures.Trigger(static_cast<int>(a_frames));
		}
		return true;
	}

	void RenderDoc::OnGameFramePresented()
	{
		if (!_api)
			return;
		if (_manualFrameDevice) {
			_api->EndFrameCapture(_manualFrameDevice.get(), nullptr);
			_manualFrameDevice = nullptr;
		}
		auto pending = _manualFramesPending.load(std::memory_order_acquire);
		while (pending && !_manualFramesPending.compare_exchange_weak(
							  pending, pending - 1, std::memory_order_acq_rel)) {
		}
		if (!pending)
			return;
		{
			std::scoped_lock lock(_captureTargetMutex);
			_manualFrameDevice.copy_from(_device11.get());
		}
		if (_manualFrameDevice)
			_api->StartFrameCapture(_manualFrameDevice.get(), nullptr);
	}

	void RenderDoc::TriggerCapture()
	{
		// Failed startup loads require a restart.
		if (!_api) {
			L->warn("RenderDoc runtime not loaded; install RenderDoc or fix dll_path, then restart with the feature loaded");
			return;
		}
		const auto frameCount = ClampCaptureFrameCount(_settings.captureFrameCount);
		if (!RequestFrames(static_cast<std::uint32_t>(frameCount)))
			return;

		L->info("Capture triggered for {}: {} frames", CaptureTargetDisplayName(_settings.captureTarget), frameCount);
	}

	void RenderDoc::TriggerMultiFrameCapture()
	{
		TriggerCapture();
	}

	void RenderDoc::ClearCaptures()
	{
		_captures.ClearCaptures();
	}

	void RenderDoc::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		{
			std::scoped_lock lock(_captureTargetMutex);
			_device11.copy_from(a_device);
			_d3d11TargetAvailable.store(
				_device11 && _window11,
				std::memory_order_release);
		}
	}

	void RenderDoc::DrawOverlay()
	{
		ApplyPendingComments();
	}

	void RenderDoc::TickHostFrame()
	{
		ApplyPendingComments();
	}

	void RenderDoc::BindD3D11CaptureTarget(
		ID3D11Device* a_device,
		HWND a_window)
	{
		{
			std::scoped_lock lock(_captureTargetMutex);
			_device11.copy_from(a_device);
			_window11 = a_window;
			_d3d11TargetAvailable.store(
				_device11 && _window11,
				std::memory_order_release);
		}
	}

	void RenderDoc::BindD3D12CaptureTarget(
		ID3D12Device* a_device,
		HWND a_window)
	{
		{
			std::scoped_lock lock(_captureTargetMutex);
			_device12.copy_from(a_device);
			_window12 = a_window;
			_d3d12TargetAvailable.store(
				_device12 && _window12,
				std::memory_order_release);
		}
	}

	void RenderDoc::UnbindD3D12CaptureTarget(ID3D12Device* a_device)
	{
		std::scoped_lock lock(_captureTargetMutex);
		if (a_device && _device12.get() != a_device)
			return;
		_d3d12TargetAvailable.store(false, std::memory_order_release);
		_window12 = nullptr;
		_device12 = nullptr;
	}

	void RenderDoc::DrawSettings()
	{
		// FO4: visible controls and native dialogs are owned by the forwarding host.
		settings::SettingsEdit edit{ *this };

		const bool d3d11Available =
			_d3d11TargetAvailable.load(std::memory_order_acquire);
		const bool d3d12Available =
			_d3d12TargetAvailable.load(std::memory_order_acquire);
		const std::array captureTargets{
			dmui::ChoiceOption<CaptureTarget>{
				CaptureTarget::kEngineD3D11,
				d3d11Available ? "Engine D3D11" : "Engine D3D11 - unavailable",
				"engine-d3d11",
				d3d11Available },
			dmui::ChoiceOption<CaptureTarget>{
				CaptureTarget::kTemporalD3D12,
				d3d12Available ? "Temporal D3D12" : "Temporal D3D12 - unavailable",
				"temporal-d3d12",
				d3d12Available }
		};
		if (const ui::Section section{ "renderdoc-settings", "Capture Settings" }; section) {
			const auto captureTarget = dmui::DrawChoice<CaptureTarget>(
				"renderdoc-capture-target",
				_settings.captureTarget,
				std::span<const dmui::ChoiceOption<CaptureTarget>>{ captureTargets },
				"Unavailable",
				"Capture target");
			if (edit.Discrete(captureTarget.changed)) {
				_settings.captureTarget = *captureTarget.selected;
			}
			if (!CaptureTargetAvailable()) {
				dmui::ui::TextDisabled(
					"Selected capture target is unavailable.");
			}

			char dllPathBuf[260];
			strncpy_s(dllPathBuf, _settings.dllPath.c_str(), _TRUNCATE);
			if (edit.Continuous(dmui::ui::InputText("DLL path", dllPathBuf, sizeof(dllPathBuf))))
				_settings.dllPath = dllPathBuf;
			dmui::ui::TextDisabled("Empty uses the installed RenderDoc. DLL path changes require a restart.");

			char folderBuf[260];
			strncpy_s(folderBuf, _settings.captureFolder.c_str(), _TRUNCATE);
			if (edit.Continuous(dmui::ui::InputText("Capture folder", folderBuf, sizeof(folderBuf))))
				_settings.captureFolder = folderBuf;
			if (dmui::ui::IsItemDeactivatedAfterEdit()) {
				ApplyCapturePath();
			}

			const auto frameRange = renderdoc_settings::kSchema.EditRange(&Settings::captureFrameCount);
			(void)edit.Continuous(dmui::ui::SliderScalar(
				"Capture Frame Count",
				&_settings.captureFrameCount,
				&frameRange.min,
				&frameRange.max));
			_settings.captureFrameCount = ClampCaptureFrameCount(_settings.captureFrameCount);
			dmui::ui::TextDisabled("Required free space: %llu MiB.",
				renderdoc::RequiredSpaceBytes(_settings.captureFrameCount) / (1024 * 1024));

			(void)dmui::ui::InputTextMultiline("Comments (embedded in next .rdc)",
				_commentsBuf.data(), _commentsBuf.size(),
				dmui::ui::Vec2{ 0, dmui::ui::GetTextLineHeightWithSpacing() * 3 });
		}

		if (const ui::Section section{ "renderdoc-capture", "Capture" }; section) {
			dmui::ui::BeginDisabled(!_api || !CaptureTargetAvailable());
			if (dmui::ui::Button("Trigger Capture"))
				TriggerCapture();
			dmui::ui::EndDisabled();
			dmui::ui::TextWrapped(
				"RenderDoc is loaded and may severely impact performance. Disable startup loading and restart to unload it. "
				"Upscaling and frame generation may be incompatible with captures.");

			if (!_api)
				dmui::ui::TextDisabled("Runtime load failed - install RenderDoc or fix the DLL path, then restart.");
		}

		if (const ui::Section section{ "renderdoc-captures", "Captures" }; section) {
			try {
				if (dmui::ui::Button("Open Capture Directory"))
					OpenCaptureLocation(_resolvedCaptureFolder, DMUI_EXTERNAL_TARGET_DIRECTORY);
				dmui::ui::SameLine();
				if (dmui::ui::Button("Copy Directory Path"))
					dmui::ui::SetClipboardText(_resolvedCaptureFolderUtf8.c_str());
				dmui::ui::TextDisabled("Capture Directory: %s", _resolvedCaptureFolderUtf8.c_str());
				const auto usageMiB = _captures.DiskUsageBytes() / (1024 * 1024);
				dmui::ui::Text("Capture Size: %.2f GiB", static_cast<double>(usageMiB) / 1024.0);
				if (usageMiB > 0 && dmui::ui::Button("Clear All Captures"))
					Menu::Get().RequestClearRenderDocCaptures();
				DrawCaptureFiles();
			} catch (const std::filesystem::filesystem_error& error) {
				dmui::ui::TextWrapped("Capture directory unavailable: %s", error.what());
			}
		}
	}

	void RenderDoc::DrawCaptureFiles()
	{
		const bool refresh = dmui::ui::Button("Refresh List");
		auto files = _captures.Inventory(refresh);
		dmui::ui::SameLine();
		dmui::ui::TextDisabled("(%zu files)", files.size());
		if (files.empty()) {
			dmui::ui::TextDisabled("No capture files found.");
			return;
		}
		// FO4: forwarded headers expose sort buttons instead of ImGui sort specs or double clicks.
		if (!dmui::ui::BeginTable("renderdoc-captures", 3,
				dmui::ui::TableFlags::kBorders | dmui::ui::TableFlags::kRowBg))
			return;
		dmui::ui::TableNextRow(dmui::ui::TableRowFlags::kHeaders);
		const std::array headers{ "Filename", "Size", "Created" };
		for (int column = 0; column < 3; ++column) {
			(void)dmui::ui::TableSetColumnIndex(column);
			if (dmui::ui::Button(headers[column])) {
				_fileSortDescending = _fileSort == column ? !_fileSortDescending : column == 2;
				_fileSort = column;
			}
		}
		std::ranges::sort(files, [this](const auto& a, const auto& b) {
			const auto comparison = _fileSort == 0 ? a.path.filename().compare(b.path.filename()) :
			                        _fileSort == 1 ? (a.bytes > b.bytes) - (a.bytes < b.bytes) :
			                                         (a.modified > b.modified) - (a.modified < b.modified);
			return _fileSortDescending ? comparison > 0 : comparison < 0;
		});
		for (const auto& file : files) {
			const auto filename = PathToUtf8(file.path.filename());
			const auto path = PathToUtf8(file.path);
			dmui::ui::PushID(path.c_str());
			dmui::ui::TableNextRow();
			(void)dmui::ui::TableSetColumnIndex(0);
			if (dmui::ui::Button(filename.c_str()))
				OpenCaptureLocation(file.path, DMUI_EXTERNAL_TARGET_VIRTUAL_FILE);
			if (dmui::ui::IsItemHovered())
				dmui::ui::SetTooltip("%s", path.c_str());
			if (!file.deletionError.empty())
				dmui::ui::TextWrapped("Deletion failed: %s", file.deletionError.c_str());
			(void)dmui::ui::TableSetColumnIndex(1);
			dmui::ui::Text("%.1f %s",
				static_cast<float>(file.bytes) / (file.bytes >= 1024 * 1024 ? 1024 * 1024 : 1024),
				file.bytes >= 1024 * 1024 ? "MB" : "KB");
			(void)dmui::ui::TableSetColumnIndex(2);
			const auto created = std::format("{:%F %R}", std::chrono::clock_cast<std::chrono::system_clock>(file.modified));
			dmui::ui::TextUnformatted(created.c_str());
			dmui::ui::PopID();
		}
		dmui::ui::EndTable();
	}

	std::vector<std::string_view> RenderDoc::GetRestartSettings() const
	{
		return cs::settings::RestartRequired(renderdoc_settings::kSchema, _bootSettings, _settings);
	}

	void RenderDoc::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
		ApplyCapturePath();
		L->info("Settings reset to defaults");
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				cs::FeatureManager::Get().Register(RenderDoc::GetSingleton());
			}
		};
		static AutoRegister _autoRegister;
	}
}
