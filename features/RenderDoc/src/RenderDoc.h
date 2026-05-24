#pragma once

#include "Feature.h"

#include <array>
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

		void Load() override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;

		void TriggerCapture();

		struct Settings
		{
			bool        enabled = false;
			std::string dllPath = "Data\\F4SE\\Plugins\\RenderDoc\\renderdoc.dll";
			std::string captureFolder = "Data\\F4SE\\Plugins\\RenderDoc\\captures";
		};

	private:
		RenderDoc() = default;

		void LoadSettings();
		void SaveSettings();
		bool TryLoadRuntime();
		void ApplyCapturePath();

		Settings _settings;
		HMODULE              _module = nullptr;
		RENDERDOC_API_1_7_0* _api    = nullptr;
		bool _attemptedLoad = false;

		std::array<char, 1024> _commentsBuf{};
	};
}
