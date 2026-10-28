#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::settings
{
	namespace core
	{
		struct Logging
		{
			std::string level = "info";
			bool telemetry = false;
			std::uint32_t telemetryIntervalSeconds = 5;
			std::string dumpHotkey = "Ctrl+F12";
		};

		inline constexpr Schema kLogging{
			std::tuple{
				Field{ "level", "Global log level: trace, debug, info, warn, error, critical, or off.", &Logging::level },
				Field{ "telemetry", "Write periodic telemetry to the log.", &Logging::telemetry },
				Field{ "telemetry_interval_seconds", "Interval between telemetry log entries in seconds.", &Logging::telemetryIntervalSeconds, Range{ 1u, UINT32_MAX } },
				Field{ "dump_hotkey", "Suggested telemetry dump binding; the host's saved override takes precedence.", &Logging::dumpHotkey } }
		};

		struct ShaderOwnership
		{
			bool enableShaders = true;
		};

		inline constexpr Schema kShaderOwnership{
			std::tuple{ Field{ "enable_shaders", "Use custom shaders; disabling this effectively disables all features.", &ShaderOwnership::enableShaders } }
		};

		struct ShaderCache
		{
			bool backgroundCompilation = false;
			std::uint32_t maxSizeMb = 4096;
		};

		inline constexpr Schema kShaderCache{
			std::tuple{
				Field{ "background_compilation", "Play while shaders compile instead of holding Continue, Load, and New Game until they finish; shaders not yet compiled render as stock.", &ShaderCache::backgroundCompilation, {}, ApplyTiming::kNextLaunch },
				Field{ "max_size_mb", "Maximum disk shader cache size in MiB.", &ShaderCache::maxSizeMb, Range{ 512u, 32768u }, ApplyTiming::kNextLaunch } }
		};

		struct Diagnostics
		{
			bool frameAnnotations = false;
		};

		inline constexpr Schema kDiagnostics{
			std::tuple{ Field{ "frame_annotations", "Name engine passes and draws in GPU captures (RenderDoc, Nsight); adds CPU cost.", &Diagnostics::frameAnnotations, {}, ApplyTiming::kNextLaunch } }
		};

		struct ShaderTarget
		{
			bool enabled = true;
		};

		constexpr auto ShaderTargetSchema(std::string_view a_target)
		{
			return Schema{ std::tuple{ Field{ a_target, "", &ShaderTarget::enabled } } };
		}

		struct Menu
		{
			std::string debugViewFeature;
			std::string debugView;
		};

		inline constexpr Schema kMenu{
			std::tuple{
				Field{ "debug_view_feature", "Feature owning the selected fullscreen debug view.", &Menu::debugViewFeature },
				Field{ "debug_view", "Selected fullscreen debug view; empty disables it.", &Menu::debugView } }
		};

		struct Preset
		{
			std::string active;
			bool autoLoadOnBoot = false;
		};

		inline constexpr Schema kPreset{
			std::tuple{
				Field{ "active", "Identity of the selected preset.", &Preset::active },
				Field{ "auto_load_on_boot", "Apply the selected preset when the game starts.", &Preset::autoLoadOnBoot, {}, ApplyTiming::kNextLaunch } }
		};

		struct Activation
		{
			bool load = false;
		};

		inline constexpr Schema kActivation{
			std::tuple{ Field{ "load", "Load this feature on game startup.", &Activation::load, {}, ApplyTiming::kNextLaunch } }
		};

		struct Enabled
		{
			bool enabled = true;
		};

		inline constexpr Schema kEnabled{
			std::tuple{ Field{ "enabled", "Apply this feature's effect; takes effect live.", &Enabled::enabled } }
		};
	}

	template <class... Fields>
	typename Schema<Fields...>::SettingsType ParseCoreSection(
		const Schema<Fields...>& a_schema,
		const toml::table& a_root,
		std::string_view a_section,
		std::string& a_error)
	{
		typename Schema<Fields...>::SettingsType value{};
		if (const auto* table = a_root[a_section].as_table()) {
			if (!ParseTable(a_schema, *table, value, a_error))
				a_error.insert(0, std::string(a_section) + ".");
		}
		return value;
	}

	inline Registry BuildCoreRegistry()
	{
		Registry registry{
			{ { "logging" }, MakeSchemaView(core::kLogging) },
			{ { "logging", "channels" }, {}, true },
			{ { "shader_ownership" }, MakeSchemaView(core::kShaderOwnership) },
			{ { "shader_ownership", "targets" }, {}, false,
				"Off binds the game's shader for that type, including feature changes to it." },
			{ { "shader_cache" }, MakeSchemaView(core::kShaderCache) },
			{ { "diagnostics" }, MakeSchemaView(core::kDiagnostics) },
			{ { "menu" }, MakeSchemaView(core::kMenu) },
			{ { "menu", "debug_view_previews" }, {}, true },
			{ { "preset" }, MakeSchemaView(core::kPreset) }
		};
		for (const auto& target : engine::GetShaderInjectionTargets()) {
			auto fields = MakeSchemaView(core::ShaderTargetSchema(target.name));
			registry[3].fields.push_back(std::move(fields.front()));
		}
		return registry;
	}

	inline void AddFeatureSections(
		Registry& a_registry,
		std::string_view a_key,
		SchemaView a_schema,
		bool a_weatherProfiles = false,
		std::optional<bool> a_enabledDefault = std::nullopt)
	{
		auto activation = MakeSchemaView(core::kActivation);
		if (a_enabledDefault) {
			auto enabled = MakeSchemaView(core::kEnabled);
			enabled.front().defaultValue = *a_enabledDefault;
			activation.push_back(std::move(enabled.front()));
		}
		a_registry.push_back({ { "features", std::string(a_key) }, std::move(activation) });
		if (!a_schema.empty())
			a_registry.push_back({ { "features", std::string(a_key), "settings" }, std::move(a_schema) });
		if (a_weatherProfiles)
			a_registry.push_back({ { "features", std::string(a_key), "weather" }, {}, true });
	}
}
