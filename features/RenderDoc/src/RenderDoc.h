#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "RenderDocSettings.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

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

		static RenderDoc* GetSingleton();

		std::string_view GetName() const override { return "RenderDoc"; }
		std::string_view GetDisplayName() const override { return "RenderDoc"; }
		std::string GetFeatureSummary() const override { return "In-game RenderDoc capture support."; }
		std::string GetCategory() const override { return FeatureCategories::kDevTools; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void DrawSettings() override;
		void DrawOverlay() override;
		void OnD3D11Ready(IDXGIAdapter*, ID3D11Device*) override;
		std::vector<std::string_view> GetRestartSettings() const override;
		void RestoreDefaultSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		void TickHostFrame();
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
		void ApplyCapturePath();
		bool CheckCaptureDiskSpace(int a_frames) const;
		[[nodiscard]] bool BindCaptureTarget(bool a_reportUnavailable);
		[[nodiscard]] bool CaptureTargetAvailable() const noexcept;
		[[nodiscard]] bool FramesEngineCaptureManually() const noexcept;
		[[nodiscard]] bool RequestFrames(std::uint32_t a_frames);
		void ApplyPendingComments();
		[[nodiscard]] std::string BuildAutomaticCaptureComments(std::string_view a_userComments = {}) const;
		void DrawCaptureFiles();

		struct CaptureBinding
		{
			winrt::com_ptr<IUnknown> device;
			HWND window = nullptr;
		};

		[[nodiscard]] CaptureBinding GetCaptureBinding() const;

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

		std::atomic<std::uint32_t> _manualFramesPending{ 0 };
		winrt::com_ptr<ID3D11Device> _manualFrameDevice;

		std::array<char, 1024> _commentsBuf{};
		int _fileSort = 2;
		bool _fileSortDescending = true;
	};
}
