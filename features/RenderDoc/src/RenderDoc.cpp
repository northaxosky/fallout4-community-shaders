#include "RenderDoc.h"

#include <renderdoc_app.h>

#include <DearModdingUI/Client.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>

#include "F4SE/API.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "REX/CONVERT.h"
#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"
#include "Settings/SettingsPersistence.h"
#include "Menu/SettingsEdit.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.renderdoc"); }

	constexpr double      kBytesPerGiB = 1024.0 * 1024.0 * 1024.0;
	using renderdoc_settings::kMinMultiFrameCount;
	using renderdoc_settings::kMaxMultiFrameCount;
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
		int ClampMultiFrameCount(int64_t a_value)
		{
			return static_cast<int>(std::clamp(a_value,
				static_cast<int64_t>(kMinMultiFrameCount),
				static_cast<int64_t>(kMaxMultiFrameCount)));
		}

		double ClampMinFreeDiskGiB(double a_value)
		{
			return a_value >= 0.0 ? a_value : RenderDoc::Settings{}.minFreeDiskGiB;
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

		std::filesystem::path ExpandCaptureFolderEnvironment(const std::string& a_configured)
		{
			std::wstring configured;
			if (!REX::UTF8_TO_UTF16(a_configured, configured)) {
				L->warn("Failed to decode RenderDoc capture folder as UTF-8; using it without environment expansion");
				return a_configured;
			}

			const auto requiredSize = REX::W32::ExpandEnvironmentStringsW(configured.c_str(), nullptr, 0);
			if (requiredSize == 0) {
				L->warn("Failed to expand environment variables in RenderDoc capture folder; using the configured path");
				return configured;
			}

			std::wstring expanded(requiredSize, L'\0');
			const auto expandedSize = REX::W32::ExpandEnvironmentStringsW(
				configured.c_str(), expanded.data(), requiredSize);
			if (expandedSize == 0 || expandedSize > requiredSize) {
				L->warn("Failed to expand environment variables in RenderDoc capture folder; using the configured path");
				return configured;
			}

			expanded.resize(expandedSize - 1);
			return expanded;
		}

		std::filesystem::path ResolveCaptureFolder(const std::string& a_configured)
		{
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
		if (!settings::Parse(renderdoc_settings::kSchema, a_config, candidate, a_error)) {
			return false;
		}

		_settings = candidate;
		_bootSettings = candidate;
		return true;
	}

	void RenderDoc::Load()
	{
		L->info("Settings: enabled={} dll={} folder={} min_free_disk_gib={:.2f} multi_frame_count={} capture_target={} capture={} multi_capture={}",
			_settings.enabled, _settings.dllPath, _settings.captureFolder,
			_settings.minFreeDiskGiB, _settings.multiFrameCount,
			CaptureTargetConfigName(_settings.captureTarget),
			_settings.captureHotkey, _settings.multiCaptureHotkey);

		if (!_settings.enabled)
			return;
		// Load before D3D initialization.
		if (!TryLoadRuntime()) {
			FailLoad("RenderDoc runtime load failed for settings.dll_path '" + _settings.dllPath
				+ "'; verify the path and RenderDoc 1.7 API compatibility");
			return;
		}
	}

	bool RenderDoc::SaveSettings()
	{
		return settings::SaveDelta(renderdoc_settings::kSchema, GetConfigKey(), _settings, *L);
	}

	bool RenderDoc::TryLoadRuntime()
	{
		if (_api)
			return true;
		if (_attemptedLoad)
			return false;
		_attemptedLoad = true;

		// Post-D3D loading can crash.
		_module = LoadLibraryA(_settings.dllPath.c_str());
		if (!_module) {
			L->warn("LoadLibrary({}) failed: {:#x}", _settings.dllPath, GetLastError());
			return false;
		}

		auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(_module, "RENDERDOC_GetAPI"));
		if (!getApi) {
			L->warn("RENDERDOC_GetAPI not found in {}", _settings.dllPath);
			FreeLibrary(_module);
			_module = nullptr;
			return false;
		}

		if (getApi(eRENDERDOC_API_Version_1_7_0, reinterpret_cast<void**>(&_api)) != 1 || !_api) {
			L->warn("RENDERDOC_GetAPI returned no API for version 1.7.0");
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
		_lastCaptureCount = _api->GetNumCaptures();
		L->info("RenderDoc runtime loaded");
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
		auto pathTemplate = PathToUtf8(_resolvedCaptureFolder / "FO4");
		_api->SetCaptureFilePathTemplate(pathTemplate.c_str());
	}

	bool RenderDoc::CheckCaptureDiskSpace() const
	{
		const auto& captureDir = _resolvedCaptureFolder;

		std::error_code ec;
		std::filesystem::create_directories(captureDir, ec);
		if (ec) {
			L->warn("RenderDoc capture aborted: failed to prepare capture folder {}: {}",
				captureDir.string(), ec.message());
			cs::Menu::ShowToast(
				"RenderDoc capture aborted: capture folder unavailable",
				4.0,
				DMUI_STATUS_SEVERITY_ERROR);
			return false;
		}

		try {
			const auto availableBytes = std::filesystem::space(captureDir).available;
			const double availableGiB = static_cast<double>(availableBytes) / kBytesPerGiB;
			const double requiredGiB = ClampMinFreeDiskGiB(_settings.minFreeDiskGiB);
			if (availableGiB < requiredGiB) {
				L->warn("RenderDoc capture aborted: {:.2f} GiB free in {} below configured {:.2f} GiB",
					availableGiB, captureDir.string(), requiredGiB);
				cs::Menu::ShowToast(
					"RenderDoc capture aborted: low disk space",
					4.0,
					DMUI_STATUS_SEVERITY_WARNING);
				return false;
			}
		} catch (const std::filesystem::filesystem_error& e) {
			L->warn("RenderDoc capture aborted: failed to query free disk space for {}: {}",
				captureDir.string(), e.what());
			cs::Menu::ShowToast(
				"RenderDoc capture aborted: disk check failed",
				4.0,
				DMUI_STATUS_SEVERITY_ERROR);
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
					DMUI_STATUS_SEVERITY_ERROR);
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
					DMUI_STATUS_SEVERITY_ERROR);
			}
			return false;
		}

		_api->SetActiveWindow(binding.device.get(), binding.window);
		return true;
	}

	void RenderDoc::QueuePendingComments(std::uint32_t a_expectedCaptures)
	{
		if (!_commentsBuf[0])
			return;

		_pendingComments = _commentsBuf.data();
		_pendingCaptures = a_expectedCaptures;
		_commentsBuf[0] = 0;
	}

	void RenderDoc::ApplyPendingComments()
	{
		if (!_api)
			return;

		const auto count = _api->GetNumCaptures();
		if (count == _lastCaptureCount)
			return;

		for (auto i = _lastCaptureCount; i < count && _pendingCaptures > 0; ++i, --_pendingCaptures) {
			std::uint32_t length = 0;
			if (!_api->GetCapture(i, nullptr, &length, nullptr) || length == 0)
				continue;

			std::string path(length, '\0');
			if (_api->GetCapture(i, path.data(), &length, nullptr))
				_api->SetCaptureFileComments(path.c_str(), _pendingComments.c_str());
		}

		if (_pendingCaptures == 0)
			_pendingComments.clear();
		_lastCaptureCount = count;
	}

	void RenderDoc::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("enabled", _settings.enabled)
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
			.Field("multi_frames", static_cast<std::int64_t>(_settings.multiFrameCount))
			.Field("folder", _resolvedCaptureFolderUtf8);
	}

	void RenderDoc::TriggerCapture()
	{
		if (!_settings.enabled) {
			L->warn("TriggerCapture called while feature disabled");
			return;
		}
		// Failed startup loads require a restart.
		if (!_api) {
			L->warn("RenderDoc runtime not loaded; restart the game with RenderDoc enabled to capture");
			return;
		}
		if (!BindCaptureTarget(true))
			return;
		if (!CheckCaptureDiskSpace())
			return;

		_api->TriggerCapture();
		QueuePendingComments(1);
		_captureCount.fetch_add(1, std::memory_order_relaxed);

		L->info(
			"Single-frame capture triggered for {}",
			CaptureTargetDisplayName(_settings.captureTarget));
	}

	void RenderDoc::TriggerMultiFrameCapture()
	{
		if (!_settings.enabled) {
			L->warn("TriggerMultiFrameCapture called while feature disabled");
			return;
		}
		if (!_api) {
			L->warn("RenderDoc runtime not loaded; restart the game with RenderDoc enabled to capture");
			return;
		}
		if (!BindCaptureTarget(true))
			return;
		if (!CheckCaptureDiskSpace())
			return;
		if (!_api->TriggerMultiFrameCapture) {
			L->warn("RenderDoc runtime does not expose TriggerMultiFrameCapture");
			return;
		}

		const auto frameCount = static_cast<uint32_t>(ClampMultiFrameCount(_settings.multiFrameCount));
		_api->TriggerMultiFrameCapture(frameCount);
		QueuePendingComments(frameCount);
		_captureCount.fetch_add(1, std::memory_order_relaxed);

		L->info(
			"Multi-frame capture triggered for {}: {} frames",
			CaptureTargetDisplayName(_settings.captureTarget),
			frameCount);
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
		settings::SettingsEdit edit{ *this };
		bool prevEnabled = _settings.enabled;
		if (edit.Discrete(dmui::ui::Checkbox("Enabled", &_settings.enabled))) {
			if (_settings.enabled && !_api)
				L->warn("Enabled at runtime; restart the game to load renderdoc.dll safely");
			else if (!_settings.enabled && prevEnabled)
				L->info("Disabled; runtime stays loaded until process exit");
		}

		dmui::ui::TextDisabled(
			"The host owns capture bindings. Suggested defaults: %s and %s.",
			_settings.captureHotkey.c_str(),
			_settings.multiCaptureHotkey.c_str());

		const bool d3d11Available =
			_d3d11TargetAvailable.load(std::memory_order_acquire);
		const bool d3d12Available =
			_d3d12TargetAvailable.load(std::memory_order_acquire);
		const std::array captureTargets{
			dmui::ChoiceOption<CaptureTarget>{
				CaptureTarget::kEngineD3D11,
				d3d11Available
					? "Engine D3D11"
					: "Engine D3D11 - unavailable",
				"engine-d3d11",
				d3d11Available },
			dmui::ChoiceOption<CaptureTarget>{
				CaptureTarget::kTemporalD3D12,
				d3d12Available
					? "Temporal D3D12"
					: "Temporal D3D12 - unavailable",
				"temporal-d3d12",
				d3d12Available }
		};
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
				"Selected target is unavailable. Temporal D3D12 is registered "
				"only while native temporal presentation is active.");
		}

		char dllPathBuf[260];
		strncpy_s(dllPathBuf, _settings.dllPath.c_str(), _TRUNCATE);
		if (edit.Continuous(dmui::ui::InputText("DLL path", dllPathBuf, sizeof(dllPathBuf))))
			_settings.dllPath = dllPathBuf;

		char folderBuf[260];
		strncpy_s(folderBuf, _settings.captureFolder.c_str(), _TRUNCATE);
		if (edit.Continuous(dmui::ui::InputText("Capture folder", folderBuf, sizeof(folderBuf))))
			_settings.captureFolder = folderBuf;
		if (dmui::ui::IsItemDeactivatedAfterEdit()) {
			ApplyCapturePath();
		}

		const double diskStep = 0.25;
		const double diskFastStep = 1.0;
		if (edit.Continuous(dmui::ui::InputScalar(
				"Minimum free disk (GiB)",
				&_settings.minFreeDiskGiB,
				&diskStep,
				&diskFastStep,
				"%.2f")))
			_settings.minFreeDiskGiB = ClampMinFreeDiskGiB(_settings.minFreeDiskGiB);
		const auto frameRange = renderdoc_settings::kSchema.EditRange(&Settings::multiFrameCount);
		(void)edit.Continuous(dmui::ui::SliderScalar(
			"Multi-frame count",
			&_settings.multiFrameCount,
			&frameRange.min,
			&frameRange.max));
		if (dmui::ui::IsItemDeactivatedAfterEdit()) {
			_settings.multiFrameCount = ClampMultiFrameCount(_settings.multiFrameCount);
		}

		(void)dmui::ui::InputTextMultiline("Comments (embedded in next .rdc)",
			_commentsBuf.data(), _commentsBuf.size(),
			dmui::ui::Vec2{ 0, dmui::ui::GetTextLineHeightWithSpacing() * 3 });

		dmui::ui::BeginDisabled(!_api || !CaptureTargetAvailable());
		if (dmui::ui::Button("Trigger Capture"))
			TriggerCapture();
		dmui::ui::SameLine();
		if (dmui::ui::Button("Trigger Multi-Frame"))
			TriggerMultiFrameCapture();
		dmui::ui::EndDisabled();

		if (!_api && _settings.enabled)
			dmui::ui::TextDisabled("Runtime load failed - fix the DLL path then restart the game.");
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
