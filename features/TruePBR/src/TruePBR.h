#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "ShaderDefines.h"

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
		void Load() override;
		void DrawSettings() override;
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
		std::atomic_uint32_t _debugMode{ 0 };
		std::atomic_bool _floatEmissiveRequested{ false };
		// The DXGI format of the created emissive target; 0 until it exists.
		std::atomic_uint32_t _emissiveFormat{ 0 };
	};
}
