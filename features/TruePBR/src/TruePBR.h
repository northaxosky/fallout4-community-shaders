#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "RecordTable.h"
#include "Render/PrepassExtraTargets.h"
#include "ShaderDefines.h"
#include "TruePBRSettings.h"

namespace cs::features
{
	class TruePBR : public ShaderFeature<true_pbr::kShaderDefines>
	{
	public:
		static TruePBR* GetSingleton();
		std::string_view GetName() const override { return "TruePBR"; }
		std::string_view GetDisplayName() const override { return "True PBR"; }
		std::string GetCategory() const override { return FeatureCategories::kMaterials; }
		std::string GetFeatureSummary() const override { return "Physically based materials authored with metal-roughness textures."; }
		bool Configure(const toml::table&, std::string&) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter*, ID3D11Device*) override;
		void WriteSharedData(render::SharedFeatureDataCB&, render::FO4SharedDataCB&) override;
		void DrawSettings() override;
		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(true_pbr::kSchema, _settings); }
		void RestoreDefaultSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(telemetry::Sink&) const override;
		bool HasDebugUI() const override { return true; }
		void DrawDebug() override;
		void Prepass() override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view) noexcept override;
		FullscreenDebugData GetFullscreenDebugData() const noexcept override;

	private:
		bool ValidateInjectionPrerequisites(std::string&) override;
		void BindRecordTable(ID3D11DeviceContext*);
		bool RegisterMaterialTargets();
		// Latched once per frame so every pass of a frame agrees.
		bool EnabledThisFrame() noexcept;
		true_pbr::Settings _settings;
		std::optional<engine::PrepassExtraTargetId> _f0Target;
		true_pbr::RecordTable _records;
		std::uint32_t _latchedFrame = UINT32_MAX;
		bool _latchedEnabled = false;
		std::atomic_uint32_t _debugMode{ 0 };
		std::atomic_bool _floatEmissiveRequested{ false };
		// The DXGI format of the created emissive target; 0 until it exists.
		std::atomic_uint32_t _emissiveFormat{ 0 };
	};
}
