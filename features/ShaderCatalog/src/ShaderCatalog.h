#pragma once

#include "Feature.h"

#include <atomic>
#include <string>

struct IDXGIAdapter;
struct ID3D11Device;

namespace cs::features
{
	class ShaderCatalog : public Feature
	{
	public:
		static ShaderCatalog* GetSingleton();

		std::string_view GetName() const override { return "ShaderCatalog"; }

		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* adapter, ID3D11Device* device) override;
		void DrawSettings() override;

		struct Settings
		{
			bool        enabled = false;
			int         writerFlushIntervalMs = 5000;
			std::string catalogPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\shader-catalog.sqlite";
			int         symbolicationBudgetUs = 200;
		};

	private:
		ShaderCatalog() = default;
		~ShaderCatalog() override;

		void LoadSettings();
		void SaveSettings();

		Settings _settings;
		std::atomic<bool> _started{ false };
		std::atomic<bool> _hooksInstalled{ false };
	};
}
