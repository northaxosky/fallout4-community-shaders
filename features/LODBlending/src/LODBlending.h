#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "LODBlendingSettings.h"
#include "Render/ShaderSubclassHooks.h"
#include "ShaderDefines.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace RE
{
	class BSRenderPass;
}

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
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

		cs::LODBlendingFeatureData GetCommonBufferData() const;

		using Settings = lod_blending::Settings;

	private:
		LODBlending() = default;

		static bool ClassifyPrepassDraw(RE::BSRenderPass* a_pass, cs::engine::PrepassBakePath a_path) noexcept;

		bool SaveSettings() override;
		settings::SchemaView GetSettingsSchema() const override { return settings::MakeSchemaView(lod_blending::kSchema); }

		Settings _settings;
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _injectionsOperational{ false };

		struct BakeCounts
		{
			std::atomic<std::uint64_t> draws{ 0 };
			std::atomic<std::uint64_t> lodDraws{ 0 };
		};
		// Indexed by PrepassBakePath; classification runs on the creating or drawing thread.
		std::array<BakeCounts, 2> _bakeCounts;
	};
}
