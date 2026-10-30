#include "TerrainVariation.h"
#include "Render/SharedDataLayout.h"

#include <DearModdingUI/Client.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/ObjectLOD.h"
#include "Render/OverlayShape.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderSubclassHooks.h"
#include "Render/SharedData.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"

#include "RE/B/BSGeometry.h"
#include "RE/B/BSGraphics.h"
#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSLightingShaderProperty.h"
#include "RE/B/BSRenderPass.h"
#include "RE/N/NiRTTI.h"
#include "RE/N/NiTexture.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.terrainvariation");

		// FO4: only natural folders tile seamlessly; man-made ones and the atlas don't.
		constexpr std::array<std::string_view, 5> kNaturalFolders{
			"landscape/rocks/", "landscape/dlc06rocks/", "landscape/ground/", "landscape/trees/", "landscape/dirtcliffs/"
		};
		constexpr std::string_view kAtlasStem = "rockriverstones";

		std::string CanonicaliseTexturePath(std::string_view a_path)
		{
			std::string canonical(a_path);
			for (auto& character : canonical) {
				character = character == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
			}

			if (canonical.starts_with("data/")) {
				canonical.erase(0, 5);
			}
			if (canonical.starts_with("textures/")) {
				canonical.erase(0, 9);
			}

			return canonical;
		}

		bool IsNaturalLandscapePath(std::string_view a_canonical)
		{
			if (a_canonical.size() > 6 && a_canonical.starts_with("dlc0") && std::isdigit(static_cast<unsigned char>(a_canonical[4])) && a_canonical[5] == '/') {
				a_canonical.remove_prefix(6);
			}
			if (!std::ranges::any_of(kNaturalFolders, [&](std::string_view a_folder) { return a_canonical.starts_with(a_folder); })) {
				return false;
			}
			return !a_canonical.substr(a_canonical.rfind('/') + 1).starts_with(kAtlasStem);
		}

		// Guards meshTextureCache and meshTextureKeepAlive.
		std::shared_mutex meshTextureMutex;
		// Keyed on the interned BSFixedString pointer of a diffuse texture name.
		std::unordered_map<const char*, bool> meshTextureCache;
		// Keeps every cached key interned so its pointer cannot be recycled.
		std::vector<RE::BSFixedString> meshTextureKeepAlive;

		bool IsLandscapeDiffuseTexture(const RE::BSFixedString& a_name)
		{
			const auto key = a_name.c_str();
			if (key == nullptr || *key == '\0') {
				return false;
			}

			{
				const std::shared_lock lock(meshTextureMutex);
				if (auto it = meshTextureCache.find(key); it != meshTextureCache.end()) {
					return it->second;
				}
			}

			const std::unique_lock lock(meshTextureMutex);
			auto [it, inserted] = meshTextureCache.try_emplace(key, false);
			if (inserted) {
				const auto canonical = CanonicaliseTexturePath(key);
				it->second = IsNaturalLandscapePath(canonical);
				meshTextureKeepAlive.push_back(a_name);
			}

			return it->second;
		}

		// FO4: baked records are immutable, so the shader gates enableMeshSupport.
		bool IsLandscapeTexturedMesh(cs::engine::PrepassDrawContext& a_draw) noexcept
		{
			auto* pass = a_draw.pass;
			auto* geometry = pass->GetGeometry();
			auto* lightProperty = netimmerse_cast<RE::BSLightingShaderProperty*>(pass->GetShaderProperty());
			if (!geometry || !lightProperty || a_draw.InObjectLOD() || cs::engine::IsOverlayShape(pass)) {
				return false;
			}

			using enum RE::BSShaderProperty::EShaderPropertyFlag;
			if (lightProperty->flags.any(kMultiTextureLandscape, kLODLandscape)) {
				return false;
			}

			// Offsetting a UV only makes sense under wrap addressing
			const auto* material = static_cast<const RE::BSLightingShaderMaterialBase*>(lightProperty->material);
			if (material == nullptr || material->textureClampMode != RE::BSGraphics::TextureAddressMode::kWrap_S_Wrap_T) {
				return false;
			}

			const auto* baseTexture = lightProperty->GetBaseTexture();
			return baseTexture != nullptr && IsLandscapeDiffuseTexture(baseTexture->name);
		}
	}

	TerrainVariation* TerrainVariation::GetSingleton()
	{
		static TerrainVariation instance;
		return &instance;
	}

	bool TerrainVariation::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(terrain_variation::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(terrain_variation::kSchema, _settings);
		return true;
	}

	bool TerrainVariation::SaveSettings()
	{
		return settings::SaveDelta(terrain_variation::kSchema, GetConfigKey(), _settings, *L);
	}

	void TerrainVariation::Load()
	{
		if (!cs::engine::RegisterFeatureShaderBindings("TerrainVariation", *this)) {
			FailLoad("Terrain Variation shader contribution registration failed.");
			return;
		}
		if (!cs::engine::RegisterPrepassDrawClassifier(*this, cs::engine::PrepassLaneComponent::kY, &IsLandscapeTexturedMesh)) {
			FailLoad("Terrain Variation prepass draw classifier installation failed.");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
	}

	void TerrainVariation::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("operational", _injectionsOperational.load(std::memory_order_relaxed))
			.Field("registrations_ready", _registrationsReady.load(std::memory_order_relaxed));
		cs::engine::WritePrepassLaneTelemetry(a_sink, cs::engine::PrepassLaneComponent::kY, "mesh");
	}

	bool TerrainVariation::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "shader contributions did not all register";
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error = "the shared substrate is unavailable, so b6 carries no Terrain Variation settings";
			return false;
		}
		if (!cs::engine::ValidateShaderInjectionRoutes("TerrainVariation", a_error))
			return false;
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	void TerrainVariation::WriteSharedData(render::SharedFeatureDataCB& a_feature, render::FO4SharedDataCB& a_fo4)
	{
		if (!_injectionsOperational.load(std::memory_order_acquire))
			return;
		a_fo4.EnabledTerrainVariation = 1u;
		a_feature.terrainVariationSettings.enableLODTerrainTilingFix = _settings.enableLODTerrainTilingFix ? 1u : 0u;
		a_feature.terrainVariationSettings.enableMeshSupport = _settings.enableMeshSupport ? 1u : 0u;
	}

	void TerrainVariation::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		const auto& [lodTiling, meshSupport] = terrain_variation::kSchema.fields;
		if (const ui::Section section{ "terrain-variation-coverage", "Coverage" }; section) {
			edit.Toggle(lodTiling, _settings, "Applies the tiling fix to LOD terrain objects.\nThis helps reduce the visible tiling effect on distant terrain.");
			edit.Toggle(meshSupport, _settings, "Applies the tiling fix to meshes that use landscape textures, such as dirt cliffs and mountain slabs.\nAlpha tested meshes like foliage and decals are never affected.");
		}
	}

	void TerrainVariation::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(TerrainVariation::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
