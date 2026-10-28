#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "Render/FrameAnnotations.h"
#include "RenderDocSettings.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <Windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <winrt/base.h>

struct RENDERDOC_API_1_7_0;

namespace cs::features
{
	class RenderDoc : public Feature
	{
	public:
		using CaptureTarget = renderdoc_settings::CaptureTarget;
		using RequestId = std::uint64_t;

		struct CaptureRequest
		{
			CaptureTarget target = CaptureTarget::kEngineD3D11;
			std::uint32_t frames = 1;
		};

		struct CaptureRefusal
		{
			std::string message;
			std::string hint;
		};

		struct CaptureCompletion
		{
			RequestId id{};
			CaptureTarget target = CaptureTarget::kEngineD3D11;
			std::uint32_t frames{};
			std::vector<renderdoc::CaptureFile> captures;
			std::string error;
		};

		using CaptureObserver = std::function<void(const CaptureCompletion&)>;

		static RenderDoc* GetSingleton();

		std::string_view GetName() const override { return "RenderDoc"; }
		std::string_view GetDisplayName() const override { return "RenderDoc"; }
		std::string GetFeatureSummary() const override { return "In-game RenderDoc capture support."; }
		bool SupportsEnableToggle() const override { return false; }
		std::string GetCategory() const override { return FeatureCategories::kUtility; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void DrawSettings() override;
		void DrawOverlay() override;
		void OnD3D11Ready(IDXGIAdapter*, ID3D11Device*) override;
		std::vector<std::string_view> GetRestartSettings() const override;
		void RestoreDefaultSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		void OnFrameBoundary();
		void BindD3D11CaptureTarget(ID3D11Device* a_device, HWND a_window);
		void BindD3D12CaptureTarget(ID3D12Device* a_device, HWND a_window);
		void UnbindD3D12CaptureTarget(ID3D12Device* a_device);
		void OnGameFramePresented();
		[[nodiscard]] bool CaptureHotkeysEnabled() const noexcept
		{
			return IsHealthy() && _api;
		}
		[[nodiscard]] const std::string& SuggestedCaptureHotkey() const noexcept
		{
			return _settings.captureHotkey;
		}
		[[nodiscard]] const std::string& SuggestedMultiCaptureHotkey() const noexcept
		{
			return _settings.multiCaptureHotkey;
		}

		[[nodiscard]] std::expected<RequestId, CaptureRefusal> RequestCapture(const CaptureRequest& a_request);
		// Runs on the thread that sees the .rdc stop growing or the capture fail.
		void AddCaptureObserver(CaptureObserver a_observer);
		void TriggerCapture();
		void TriggerMultiFrameCapture();
		void ClearCaptures();
		[[nodiscard]] const std::filesystem::path& CaptureDirectory() const noexcept { return _resolvedCaptureFolder; }

		using Settings = renderdoc_settings::Settings;

	private:
		RenderDoc() = default;

		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(renderdoc_settings::kSchema, _settings); }
		bool TryLoadRuntime(std::string& a_error);
		bool ResolveCapturePath(std::string& a_error);
		void ApplyCapturePath();
		[[nodiscard]] std::optional<CaptureRefusal> CheckCaptureDiskSpace(int a_frames) const;
		[[nodiscard]] bool CaptureTargetAvailable() const noexcept;
		void ApplyPendingComments();
		void PollCaptureCompletion();
		[[nodiscard]] std::string BuildAutomaticCaptureComments(std::string_view a_userComments = {}) const;
		void DrawCaptureFiles();

		struct CaptureBinding
		{
			winrt::com_ptr<IUnknown> device;
			HWND window = nullptr;
		};

		[[nodiscard]] CaptureBinding GetCaptureBinding(CaptureTarget a_target) const;

		struct ActiveCapture
		{
			RequestId id{};
			CaptureRequest request;
			CaptureBinding binding;
			std::uint32_t countBefore{};
			std::uint32_t remaining{};
			bool started = false;
		};

		// Inactivity window: a file still growing keeps the wait alive.
		static constexpr std::chrono::seconds kCaptureFileTimeout{ 60 };
		static constexpr std::chrono::milliseconds kCaptureFileStableFor{ 500 };

		struct AwaitedFiles
		{
			RequestId id{};
			CaptureRequest request;
			std::uint32_t countBefore{};
			std::vector<renderdoc::CaptureFile> files;
			std::chrono::steady_clock::time_point lastChange;
		};

		void Complete(CaptureCompletion a_completion);

		Settings _settings;
		Settings _bootSettings;
		std::filesystem::path _resolvedCaptureFolder;
		std::string _resolvedCaptureFolderUtf8;
		HMODULE _module = nullptr;
		RENDERDOC_API_1_7_0* _api = nullptr;
		bool _attemptedLoad = false;
		std::atomic<std::uint32_t> _captureCount{ 0 };
		renderdoc::CaptureService _captures;

		mutable std::mutex _captureTargetMutex;
		winrt::com_ptr<ID3D11Device> _device11;
		winrt::com_ptr<ID3D12Device> _device12;
		HWND _window11 = nullptr;
		HWND _window12 = nullptr;
		std::atomic_bool _d3d11TargetAvailable{ false };
		std::atomic_bool _d3d12TargetAvailable{ false };

		mutable std::mutex _captureMutex;
		std::optional<ActiveCapture> _active;
		// Captures show every annotation event from request to completion.
		std::optional<render::frame_annotations::ScopedForce> _annotationForce;
		std::optional<AwaitedFiles> _awaited;
		std::vector<CaptureObserver> _observers;
		RequestId _lastRequestId{};

		std::array<char, 1024> _commentsBuf{};
		int _fileSort = 2;
		bool _fileSortDescending = true;
	};
}
