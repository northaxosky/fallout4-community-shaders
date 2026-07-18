#pragma once

#include "Feature.h"
#include "Utils/Hotkey.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include <Windows.h>

struct RENDERDOC_API_1_7_0;

namespace cs::features
{
	class RenderDoc : public Feature
	{
	public:
		static RenderDoc* GetSingleton();

		std::string_view GetName() const override { return "RenderDoc"; }
		std::string GetFeatureSummary() const override { return "Loads the RenderDoc capture library and bridges F4SE input to its capture hotkey."; }
		std::string GetCategory() const override { return "Diagnostics"; }
		bool HasResettableSettings() const override { return true; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;

		void TriggerCapture();
		void TriggerMultiFrameCapture();

		struct Settings
		{
			bool        enabled = false;
			std::string dllPath = "Data\\F4SE\\Plugins\\RenderDoc\\renderdoc.dll";
			std::string captureFolder = "";
			double      minFreeDiskGiB = 1.0;
			int         multiFrameCount = 5;

			// Capture chords, parsed via cs::input::Hotkey. "none"/"" unbinds. Multi wins if identical.
			std::string captureHotkey = "F11";
			std::string multiCaptureHotkey = "Shift+F11";
		};

	private:
		RenderDoc() = default;

		void SaveSettings();
		void RefreshHotkeys();
		bool TryLoadRuntime();
		void ApplyCapturePath();
		bool CheckCaptureDiskSpace() const;
		void ApplyPendingComments();
		static bool HandleWndProc(HWND, UINT, WPARAM, LPARAM);

		Settings              _settings;
		std::filesystem::path _resolvedCaptureFolder;
		HMODULE              _module = nullptr;
		RENDERDOC_API_1_7_0* _api    = nullptr;
		bool _attemptedLoad = false;

		cs::input::Hotkey _captureHotkey;
		cs::input::Hotkey _multiCaptureHotkey;
		// vk of the capture chord awaiting its key-up, so we can consume the paired release. 0 = none.
		std::uint32_t _captureReleaseVk = 0;

		std::array<char, 1024> _commentsBuf{};
	};
}
