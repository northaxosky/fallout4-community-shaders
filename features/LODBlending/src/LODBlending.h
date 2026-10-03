#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "LODBlendingSettings.h"
#include "ShaderDefines.h"

#include <atomic>
#include <string>

namespace cs::features
{
	class LODBlending : public ShaderFeature<lod_blending_shader::kShaderDefines>
	{
	public:
		static LODBlending* GetSingleton();

		std::string_view GetName() const override { return "LODBlending"; }
		std::string_view GetDisplayName() const override { return "LOD Blending"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "LOD terrain and object brightness and gamma matching.";
		}

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }

		cs::LODBlendingFeatureData GetCommonBufferData() const;

		using Settings = lod_blending::Settings;

	private:
		LODBlending() = default;

		bool SaveSettings() override;
		settings::SchemaView GetSettingsSchema() const override { return settings::MakeSchemaView(lod_blending::kSchema); }

		Settings _settings;
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		std::string _validationDetail;
	};
}
