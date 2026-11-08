#pragma once

#include "Capture/CaptureContract.h"
#include "Feature.h"
#include "FeatureCategories.h"
#include "ScreenshotFile.h"
#include "ScreenshotSettings.h"
#include "Utils/CSBuffer.h"

#include <DirectXTex.h>

#include <Windows.h>
#include <d3d11.h>
#include <winrt/base.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace cs::features
{
	// Saves the engine's screenshot request as a PNG, HDR10 under HDR output.
	class Screenshot : public Feature
	{
	public:
		using Settings = screenshot_settings::Settings;
		using RequestId = capture::RequestId;
		using CaptureRefusal = capture::Refusal;

		struct CaptureCompletion
		{
			RequestId id{};
			std::filesystem::path path;
			std::uint64_t bytes{};
			std::string error;
		};

		using Observers = capture::ObserverList<CaptureCompletion>;
		using CaptureObserver = Observers::Observer;

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

		// Queues the engine screenshot; the completion carries the id.
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
		void RunEncoder(std::stop_token a_stop);
		void StopEncoder();
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
		Observers _observers;

		struct EncodeJob
		{
			RequestId id{};
			screenshot_file::Job job;
		};
		std::mutex _encodeMutex;
		std::condition_variable_any _encodeWake;
		std::deque<EncodeJob> _encodeQueue;
		// Started on the first save; main thread only.
		std::jthread _encoder;
		bool _encoderStopped = false;
	};
}
