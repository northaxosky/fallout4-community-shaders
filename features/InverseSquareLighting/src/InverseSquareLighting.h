#pragma once

#include "Feature.h"
#include "ShaderDefines.h"

namespace cs::features
{
	class InverseSquareLighting : public ShaderFeature<isl::kShaderDefines>
	{
	public:
		static InverseSquareLighting* GetSingleton();
		std::string_view GetName() const override { return "InverseSquareLighting"; }
		std::string_view GetDisplayName() const override { return "Inverse Square Lighting"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Physically accurate inverse-square light falloff.";
		}
		void Load() override;
		void OnDataLoaded() override;
		void OnPostPostLoad() override;
		void OnRuntimeQuarantined() noexcept override;
		void OnD3D11Ready(IDXGIAdapter*, ID3D11Device*) override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void DrawSettings() override;
		void DrawFailLoadMessage() override { DrawSettings(); }
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

	private:
		InverseSquareLighting() = default;
	};
}
