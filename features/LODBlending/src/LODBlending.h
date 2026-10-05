#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "LODBlendingSettings.h"
#include "ShaderDefines.h"

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

		static std::uint32_t ClassifyPrepassDraw(RE::BSRenderPass* a_pass) noexcept;
		static void ApplyPrepassDraw(std::uint32_t a_class) noexcept;
		void FinishPrepassFrame() noexcept;

		bool SaveSettings() override;
		settings::SchemaView GetSettingsSchema() const override { return settings::MakeSchemaView(lod_blending::kSchema); }

		Settings _settings;
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		std::string _validationDetail;

		struct PrepassCounts
		{
			std::uint32_t draws = 0;
			std::uint32_t lodDraws = 0;
			std::uint32_t flagDraws = 0;
			std::uint32_t materialDraws = 0;
		};
		// Render thread only; folded into the atomics below after each deferred prepass.
		PrepassCounts _frameCounts;
		bool _loggedFirstPrepass = false;
		bool _loggedFirstLOD = false;
		std::atomic_bool _observerInstalled{ false };
		std::atomic<std::uint32_t> _lastFrame{ 0 };
		std::atomic<std::uint32_t> _lastDraws{ 0 };
		std::atomic<std::uint32_t> _lastLodDraws{ 0 };
		std::atomic<std::uint32_t> _lastFlagDraws{ 0 };
		std::atomic<std::uint32_t> _lastMaterialDraws{ 0 };
		std::atomic<std::uint64_t> _sessionDraws{ 0 };
		std::atomic<std::uint64_t> _sessionLodDraws{ 0 };
	};
}
