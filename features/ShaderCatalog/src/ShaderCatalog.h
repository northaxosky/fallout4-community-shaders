#pragma once

#include "Feature.h"
#include "FeatureCategories.h"

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
		std::string GetFeatureSummary() const override { return "Captures every shader the engine compiles into a SQLite catalog for inspection."; }
		std::string GetCategory() const override { return FeatureCategories::kDevTools; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		bool HasCapability(FeatureCapability a_capability) const noexcept override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* adapter, ID3D11Device* device) override;
		void DrawSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		bool HooksInstalled() const noexcept { return _hooksInstalled.load(std::memory_order_acquire); }

		struct Settings
		{
			bool        enabled = false;
			int         writerFlushIntervalMs = 5000;
			std::string catalogPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\shader-catalog.sqlite";
			// BSShader subclass technique attribution skips unverified layouts to prevent next-gen CTDs without affecting the swap broker.
			bool        subclassAttribution = true;
		};

	private:
		ShaderCatalog() = default;
		~ShaderCatalog() override;

		void SaveSettings();

		Settings _settings;
		std::atomic<bool> _started{ false };
		std::atomic<bool> _hooksInstalled{ false };
	};
}
