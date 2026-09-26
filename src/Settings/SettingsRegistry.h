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
				Field{ "dump_hotkey", "Suggested telemetry dump binding; the host's saved override takes precedence.", &Logging::dumpHotkey }
			}
		};

		struct ShaderOwnership
		{
			bool enabled = true;
		};

		inline constexpr Schema kShaderOwnership{
			std::tuple{ Field{ "enabled", "Enable baseline shader replacement ownership.", &ShaderOwnership::enabled, {}, ApplyTiming::kNextLaunch } }
		};

		struct ShaderTarget
		{
			bool enabled = true;
		};

		constexpr auto ShaderTargetSchema(std::string_view a_target)
		{
			return Schema{ std::tuple{ Field{ a_target, "", &ShaderTarget::enabled, {}, ApplyTiming::kNextLaunch } } };
		}

		struct Menu
		{
			std::string debugViewFeature;
			std::string debugView;
		};

		inline constexpr Schema kMenu{
			std::tuple{
				Field{ "debug_view_feature", "Feature owning the selected fullscreen debug view.", &Menu::debugViewFeature },
				Field{ "debug_view", "Selected fullscreen debug view; empty disables it.", &Menu::debugView }
			}
		};

		struct Preset
		{
			std::string active;
			bool autoLoadOnBoot = false;
		};

		inline constexpr Schema kPreset{
			std::tuple{
				Field{ "active", "Identity of the selected preset.", &Preset::active },
				Field{ "auto_load_on_boot", "Apply the selected preset when the game starts.", &Preset::autoLoadOnBoot, {}, ApplyTiming::kNextLaunch }
			}
		};

		struct Activation
		{
			bool load = false;
		};

		inline constexpr Schema kActivation{
			std::tuple{ Field{ "load", "Load this feature on game startup.", &Activation::load, {}, ApplyTiming::kNextLaunch } }
		};
	}

	inline Registry BuildCoreRegistry()
	{
		Registry registry{
			{ { "logging" }, MakeSchemaView(core::kLogging) },
			{ { "logging", "channels" }, {}, true },
			{ { "shader_ownership" }, MakeSchemaView(core::kShaderOwnership) },
			{ { "shader_ownership", "targets" }, {}, false,
				"Allow baseline replacement of each shader target. (restart required)" },
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

	inline void AddFeatureSections(Registry& a_registry, std::string_view a_key, SchemaView a_schema)
	{
		a_registry.push_back({ { "features", std::string(a_key) }, MakeSchemaView(core::kActivation) });
		if (!a_schema.empty())
			a_registry.push_back({ { "features", std::string(a_key), "settings" }, std::move(a_schema) });
	}
}
