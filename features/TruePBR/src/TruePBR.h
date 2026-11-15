#pragma once

#include "Feature.h"
#include "FeatureCategories.h"

namespace cs::features
{
	class TruePBR : public Feature
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
	};
}
