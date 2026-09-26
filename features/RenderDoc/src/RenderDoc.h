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
		std::string GetFeatureSummary() const override { return "Loads the RenderDoc capture library and registers capture actions with DearModdingUI."; }
		std::string GetCategory() const override { return FeatureCategories::kDevTools; }
		bool HasResettableSettings() const override { return true; }

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
		[[nodiscard]] bool CaptureHotkeysEnabled() const noexcept
		{
			return IsHealthy() && _settings.enabled && _api;
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

		using Settings = renderdoc_settings::Settings;

	private:
		RenderDoc() = default;

		bool SaveSettings() override;
		settings::SchemaView GetSettingsSchema() const override { return settings::MakeSchemaView(renderdoc_settings::kSchema); }
		bool TryLoadRuntime();
		void ApplyCapturePath();
		bool CheckCaptureDiskSpace() const;
		[[nodiscard]] bool BindCaptureTarget(bool a_reportUnavailable);
		[[nodiscard]] bool CaptureTargetAvailable() const noexcept;
		void QueuePendingComments(std::uint32_t a_expectedCaptures);
		void ApplyPendingComments();

		struct CaptureBinding
		{
			winrt::com_ptr<IUnknown> device;
			HWND window = nullptr;
		};

		[[nodiscard]] CaptureBinding GetCaptureBinding() const;

		Settings              _settings;
		Settings              _bootSettings;
		std::filesystem::path _resolvedCaptureFolder;
		std::string           _resolvedCaptureFolderUtf8;
		HMODULE              _module = nullptr;
		RENDERDOC_API_1_7_0* _api    = nullptr;
		bool _attemptedLoad = false;
		std::atomic<std::uint32_t> _captureCount{ 0 };

		mutable std::mutex             _captureTargetMutex;
		winrt::com_ptr<ID3D11Device>   _device11;
		winrt::com_ptr<ID3D12Device>   _device12;
		HWND                           _window11 = nullptr;
		HWND                           _window12 = nullptr;
		std::atomic_bool               _d3d11TargetAvailable{ false };
		std::atomic_bool               _d3d12TargetAvailable{ false };

		// Comments apply to a completed capture, so they wait for the file to appear.
		std::string   _pendingComments;
		std::uint32_t _pendingCaptures = 0;
		std::uint32_t _lastCaptureCount = 0;

		std::array<char, 1024> _commentsBuf{};
	};
}
