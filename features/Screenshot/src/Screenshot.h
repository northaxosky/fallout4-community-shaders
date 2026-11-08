#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "ScreenshotSettings.h"
#include "Utils/CSBuffer.h"

#include <DirectXTex.h>

#include <Windows.h>
#include <d3d11.h>
#include <winrt/base.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cs::features
{
	// Saves the engine's own screenshot request as a PNG, HDR10 when HDR output is on.
	class Screenshot : public Feature
	{
	public:
		using Settings = screenshot_settings::Settings;
		using RequestId = std::uint64_t;

		struct CaptureRefusal
		{
			std::string message;
			std::string hint;
		};

		struct CaptureCompletion
		{
			RequestId id{};
			std::filesystem::path path;
			std::uint64_t bytes{};
			std::string error;
		};

		using CaptureObserver = std::function<void(const CaptureCompletion&)>;

		static Screenshot* GetSingleton();

		std::string_view GetName() const override { return "Screenshot"; }
		std::string_view GetDisplayName() const override { return "Screenshot"; }
		bool SupportsEnableToggle() const override { return false; }
		std::string GetCategory() const override { return FeatureCategories::kUtility; }
		std::string GetFeatureSummary() const override
		{
			return "Asynchronous PNG screenshots with HDR10 output, aspect crops and clipboard copy.";
		}

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;

		// Queues the engine's screenshot for the next frame; the completion carries the id.
		[[nodiscard]] std::expected<RequestId, CaptureRefusal> RequestCapture();
		// Runs on the main thread, after Main::Swap.
		void AddCaptureObserver(CaptureObserver a_observer);

		// Engine hooks, on the render thread inside Main::Swap.
		void OnQueueChecked(bool a_queued) noexcept
		{
			_queuedThisFrame = a_queued;
			_sceneKept = false;
		}
		// False when the engine should still write its own file.
		[[nodiscard]] bool OnTakeScreenShot();

	private:
		Screenshot() = default;

		struct Readback
		{
			winrt::com_ptr<ID3D11Texture2D> staging;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
			UINT width = 0;
			UINT height = 0;
			std::filesystem::path path;
			bool hdr = false;
			RequestId id{};
			Settings settings;
		};

		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(screenshot_settings::kSchema, _settings); }

		// Before the interface draws: keeps the scene when the capture excludes it.
		void OnPreInterface();
		void OnFrameBoundary();
		[[nodiscard]] bool Capture();
		[[nodiscard]] ID3D11Texture2D* SelectSource(bool a_interface, bool& a_hdr);
		void PollReadbacks();
		void Save(const Readback& a_readback, DirectX::ScratchImage&& a_image);
		void Complete(CaptureCompletion a_completion);
		void DrainCompletions();

		Settings _settings;
		// Render thread only.
		bool _queuedThisFrame = false;
		std::unique_ptr<buffer::Texture2D> _scene;
		bool _sceneKept = false;
		std::vector<Readback> _readbacks;
		std::atomic<RequestId> _requested{ 0 };
		RequestId _lastRequestId{};
		std::mutex _completionMutex;
		std::vector<CaptureCompletion> _completions;
		std::vector<CaptureObserver> _observers;
	};
}
