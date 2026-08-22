#include "Telemetry/Telemetry.h"

#include "Feature.h"
#include "FeatureState.h"
#include "Log.h"
#include "LogThrottle.h"
#include "Plugin.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"

#include <atomic>
#include <charconv>
#include <exception>
#include <limits>
#include <sstream>
#include <system_error>

namespace cs::telemetry
{
	namespace
	{
		std::atomic<std::uint64_t> g_frame{ 0 };
		std::atomic_bool          g_enabled{ false };
		std::atomic<std::uint32_t> g_intervalFrames{ 60 };
		bool g_installed = false;
		// without the post-composite anchor the pump never ticks
		std::atomic_bool g_compositeSamplingAvailable{ true };

		void AppendSeparatorAndKey(std::string& a_line, std::string_view a_key)
		{
			if (!a_line.empty())
				a_line.push_back(' ');
			a_line.append(a_key);
			a_line.push_back('=');
		}

		void AppendInteger(std::string& a_line, std::int64_t a_value)
		{
			char buffer[32]{};
			const auto result = std::to_chars(std::begin(buffer), std::end(buffer), a_value);
			if (result.ec == std::errc{})
				a_line.append(buffer, result.ptr);
		}

		void AppendDouble(std::string& a_line, double a_value)
		{
			char buffer[64]{};
			const auto result = std::to_chars(
				std::begin(buffer), std::end(buffer), a_value, std::chars_format::general);
			if (result.ec == std::errc{})
				a_line.append(buffer, result.ptr);
		}

		void AppendString(std::string& a_line, std::string_view a_value)
		{
			const bool quote = a_value.find_first_of(" =\"\t\r\n") != std::string_view::npos;
			if (!quote) {
				a_line.append(a_value);
				return;
			}

			a_line.push_back('"');
			for (const char c : a_value) {
				switch (c) {
				case '\\':
					a_line.append("\\\\");
					break;
				case '"':
					a_line.append("\\\"");
					break;
				case '\t':
					a_line.append("\\t");
					break;
				case '\r':
					a_line.append("\\r");
					break;
				case '\n':
					a_line.append("\\n");
					break;
				default:
					a_line.push_back(c);
					break;
				}
			}
			a_line.push_back('"');
		}

		std::int64_t TomlInteger(std::uint64_t a_value) noexcept
		{
			constexpr auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
			return static_cast<std::int64_t>(a_value > max ? max : a_value);
		}

		void CollectShaderInjection(Sink& a_sink)
		{
			const auto summary =
				cs::engine::GetShaderInjectionSummary();
			const auto bsdfComposite =
				cs::engine::GetShaderInjectionTargetSnapshot(
					cs::engine::ShaderInjectionTarget::kBsdfComposite);
			a_sink
				.Field(
					"requested",
					static_cast<std::int64_t>(summary.requested))
				.Field(
					"compile_attempted",
					static_cast<std::int64_t>(
						summary.compileAttempted))
				.Field(
					"compiled_ok",
					static_cast<std::int64_t>(summary.compiled))
				.Field(
					"swappable",
					static_cast<std::int64_t>(summary.swappable))
				.Field(
					"requested_by_feature_contributor",
					static_cast<std::int64_t>(
						summary.requestedByFeatureContributor))
				.Field(
					"requested_by_baseline_ownership",
					static_cast<std::int64_t>(
						summary.requestedByBaselineOwnership))
				.Field(
					"requested_by_developer_force_on",
					static_cast<std::int64_t>(
						summary.requestedByDeveloperForceOn))
				.Field("stock_matches", TomlInteger(summary.matches))
				.Field(
					"replacements",
					TomlInteger(summary.substitutions))
				.Field(
					"passthrough_compile_failed",
					TomlInteger(summary.passthroughCompileFail))
				.Field(
					"passthrough_not_ready",
					TomlInteger(summary.passthroughNotReady))
				.Field(
					"passthrough_disabled",
					TomlInteger(summary.passthroughDisabled))
				.Field("dispatches", TomlInteger(summary.dispatches))
				.Field(
					"bsdf_composite_stock_matches",
					TomlInteger(bsdfComposite.matches))
				.Field(
					"bsdf_composite_replacements",
					TomlInteger(bsdfComposite.substitutions))
				.Field(
					"bsdf_composite_dispatches",
					TomlInteger(bsdfComposite.dispatches));
		}
	}

	Sink& Sink::Field(std::string_view a_key, std::string_view a_value)
	{
		AppendSeparatorAndKey(_line, a_key);
		AppendString(_line, a_value);
		_table.insert_or_assign(a_key, std::string(a_value));
		return *this;
	}

	Sink& Sink::Field(std::string_view a_key, std::int64_t a_value)
	{
		AppendSeparatorAndKey(_line, a_key);
		AppendInteger(_line, a_value);
		_table.insert_or_assign(a_key, a_value);
		return *this;
	}

	Sink& Sink::Field(std::string_view a_key, double a_value)
	{
		AppendSeparatorAndKey(_line, a_key);
		AppendDouble(_line, a_value);
		_table.insert_or_assign(a_key, a_value);
		return *this;
	}

	Sink& Sink::Field(std::string_view a_key, bool a_value)
	{
		AppendSeparatorAndKey(_line, a_key);
		_line.push_back(a_value ? '1' : '0');
		_table.insert_or_assign(a_key, a_value);
		return *this;
	}

	Sink& Sink::Dimensions(
		std::string_view a_key,
		std::uint32_t a_width,
		std::uint32_t a_height)
	{
		char buffer[32]{};
		auto widthResult = std::to_chars(std::begin(buffer), std::end(buffer), a_width);
		if (widthResult.ec != std::errc{})
			return Field(a_key, std::string_view{});
		*widthResult.ptr++ = 'x';
		const auto heightResult = std::to_chars(widthResult.ptr, std::end(buffer), a_height);
		if (heightResult.ec != std::errc{})
			return Field(a_key, std::string_view{});
		return Field(a_key, std::string_view(buffer, heightResult.ptr));
	}

	std::string Sink::ToLine() const
	{
		return _line;
	}

	const toml::table& Sink::AsTable() const noexcept
	{
		return _table;
	}

	std::uint64_t CurrentFrame() noexcept
	{
		return g_frame.load(std::memory_order_relaxed);
	}

	namespace pump
	{
		void Tick()
		{
			const auto frame = g_frame.fetch_add(1, std::memory_order_relaxed) + 1;
			if (!g_enabled.load(std::memory_order_relaxed))
				return;

			const auto interval = g_intervalFrames.load(std::memory_order_relaxed);
			if (frame % interval != 0)
				return;

			auto* logger = cs::log::Get("cs.telemetry");
			try {
				Sink sink;
				CollectShaderInjection(sink);
				logger->info(
					"frame={} component=shader_injection {}",
					frame,
					sink.ToLine());
			} catch (const std::exception& e) {
				CS_LOG_ONCE(
					logger,
					spdlog::level::warn,
					"Telemetry collection failed for shader_injection: {}",
					e.what());
			} catch (...) {
				CS_LOG_ONCE(
					logger,
					spdlog::level::warn,
					"Telemetry collection failed for shader_injection: non-standard exception");
			}
			for (const auto* feature : FeatureManager::Get().GetAll()) {
				try {
					if (!feature->ProducesTelemetry())
						continue;

					Sink sink;
					feature->CollectTelemetry(sink);
					const auto fields = sink.ToLine();
					if (fields.empty())
						logger->info("frame={} feature={}", frame, feature->GetName());
					else
						logger->info("frame={} feature={} {}", frame, feature->GetName(), fields);
				} catch (const std::exception& e) {
					CS_LOG_ONCE(logger, spdlog::level::warn,
						"Telemetry collection failed for {}: {}", feature->GetName(), e.what());
				} catch (...) {
					CS_LOG_ONCE(logger, spdlog::level::warn,
						"Telemetry collection failed for {}: non-standard exception", feature->GetName());
				}
			}
		}

		void DumpAll()
		{
			auto* logger = cs::log::Get("cs.telemetry");
			try {
				toml::table root;
				root.insert_or_assign("build", CS_BUILD_DESCRIBE);
				root.insert_or_assign("frame", TomlInteger(CurrentFrame()));
				root.insert_or_assign("logging", cs::log::ConfigAsToml());

				Sink shaderInjection;
				CollectShaderInjection(shaderInjection);
				root.insert_or_assign(
					"shader_injection",
					shaderInjection.AsTable());

				toml::table features;
				for (const auto* feature : FeatureManager::Get().GetRegisteredFeatures()) {
					toml::table featureTable;
					try {
						const auto& state = feature->GetState();
						featureTable.insert_or_assign("loaded", feature->IsLoaded());
						featureTable.insert_or_assign("active", feature->IsActive());
						featureTable.insert_or_assign("installed", state.installed);
						featureTable.insert_or_assign("desired_active", state.desiredActive);
						featureTable.insert_or_assign(
							"state", std::string(FeatureRuntimeStateName(state.runtimeState)));
						if (!state.detail.empty())
							featureTable.insert_or_assign("detail", state.detail);

						Sink sink;
						if (feature->ProducesTelemetry())
							feature->CollectTelemetry(sink);
						featureTable.insert_or_assign("telemetry", sink.AsTable());
					} catch (const std::exception& e) {
						featureTable.insert_or_assign("telemetry_error", e.what());
					} catch (...) {
						featureTable.insert_or_assign("telemetry_error", "non-standard exception");
					}
					features.insert_or_assign(feature->GetName(), std::move(featureTable));
				}
				root.insert_or_assign("features", std::move(features));

				std::ostringstream out;
				out << toml::json_formatter{ root };
				logger->info("telemetry-dump {}", out.str());
			} catch (const std::exception& e) {
				logger->warn("Telemetry dump failed: {}", e.what());
			} catch (...) {
				logger->warn("Telemetry dump failed: non-standard exception");
			}
		}

		void SetEnabled(bool a_enabled)
		{
			g_enabled.store(
				a_enabled
					&& g_compositeSamplingAvailable.load(std::memory_order_relaxed),
				std::memory_order_relaxed);
		}

		void SetIntervalFrames(std::uint32_t a_interval)
		{
			g_intervalFrames.store(a_interval == 0 ? 1 : a_interval, std::memory_order_relaxed);
		}

		bool Enabled() noexcept
		{
			return g_enabled.load(std::memory_order_relaxed);
		}

		std::uint32_t IntervalFrames() noexcept
		{
			return g_intervalFrames.load(std::memory_order_relaxed);
		}
	}

	void Install()
	{
		if (g_installed)
			return;
		g_installed = true;
		if (!cs::engine::RegisterPostDeferredComposite([] {
				pump::Tick();
			})) {
			g_compositeSamplingAvailable.store(false, std::memory_order_relaxed);
			pump::SetEnabled(false);
			cs::log::Get("cs.telemetry")->error(
				"Telemetry composite sampling disabled: post-composite hook registration failed.");
		}
	}
}
