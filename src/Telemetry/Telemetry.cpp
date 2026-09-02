#include "Telemetry/Telemetry.h"

#include "Feature.h"
#include "FeatureState.h"
#include "Log.h"
#include "LogThrottle.h"
#include "Plugin.h"
#include "Render/Engine.h"
#include "Render/FrameBuffer.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"

#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
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
		std::atomic<std::uint32_t> g_intervalSeconds{ 5 };
		std::atomic<std::uint64_t> g_lastEmitMilliseconds{ 0 };
		std::atomic_bool          g_dumpRequested{ false };
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

		std::string FormatHex64(std::uint64_t a_value)
		{
			char buffer[16]{};
			const auto result =
				std::to_chars(std::begin(buffer), std::end(buffer), a_value, 16);
			std::string output = "0x";
			if (result.ec == std::errc{}) {
				output.append(buffer, result.ptr);
			}
			return output;
		}

		void CollectShaderInjection(Sink& a_sink)
		{
			const auto summary =
				cs::engine::GetShaderInjectionSummary();
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
				.Field("dispatches", TomlInteger(summary.dispatches));

			for (std::uint8_t index = 0;
				index <
				static_cast<std::uint8_t>(
					cs::engine::ShaderInjectionTarget::kCount);
				++index) {
				const auto target =
					cs::engine::GetShaderInjectionTargetSnapshot(
						static_cast<cs::engine::ShaderInjectionTarget>(
							index));
				if (!target.requested || target.name.empty()) {
					continue;
				}
				a_sink
					.Field(
						target.name + "_stock_matches",
						TomlInteger(target.matches))
					.Field(
						target.name + "_replacements",
						TomlInteger(target.substitutions))
					.Field(
						target.name + "_dispatches",
						TomlInteger(target.dispatches));
			}
		}

		[[nodiscard]] std::string FormatVector3(float a_x, float a_y, float a_z)
		{
			std::string result;
			AppendDouble(result, a_x);
			result.push_back(' ');
			AppendDouble(result, a_y);
			result.push_back(' ');
			AppendDouble(result, a_z);
			return result;
		}

		[[nodiscard]] std::string FormatFloat4(const DirectX::XMFLOAT4& a_value)
		{
			std::string result = FormatVector3(a_value.x, a_value.y, a_value.z);
			result.push_back(' ');
			AppendDouble(result, a_value.w);
			return result;
		}

		[[nodiscard]] toml::array TomlVector3(
			float a_x,
			float a_y,
			float a_z)
		{
			return toml::array{
				static_cast<double>(a_x),
				static_cast<double>(a_y),
				static_cast<double>(a_z)
			};
		}

		void CollectDumpContext(toml::table& a_root)
		{
			// DumpAll runs from the post-composite render hook.
			auto* player = RE::PlayerCharacter::GetSingleton();
			const auto position = player ? player->GetPosition() : RE::NiPoint3{};
			const bool positionAvailable = player
				&& std::isfinite(position.x)
				&& std::isfinite(position.y)
				&& std::isfinite(position.z);
			a_root.insert_or_assign(
				"player_position_available",
				positionAvailable);
			if (positionAvailable) {
				a_root.insert_or_assign(
					"player_position",
					TomlVector3(position.x, position.y, position.z));
			}

			const auto& camera = cs::engine::GetFrameBuffer();
			a_root.insert_or_assign("camera_snapshot_valid", camera.valid);
			a_root.insert_or_assign(
				"camera_orientation_source",
				"published_frame_buffer_b12");
			if (!camera.valid)
				return;

			const auto origin = cs::engine::CameraWorldOrigin(camera.data);
			a_root.insert_or_assign(
				"camera_origin",
				TomlVector3(origin.x, origin.y, origin.z));
			a_root.insert_or_assign(
				"camera_view_to_world_row0",
				TomlVector3(
					camera.data.ViewToWorld[0].x,
					camera.data.ViewToWorld[0].y,
					camera.data.ViewToWorld[0].z));
			a_root.insert_or_assign(
				"camera_view_to_world_row1",
				TomlVector3(
					camera.data.ViewToWorld[1].x,
					camera.data.ViewToWorld[1].y,
					camera.data.ViewToWorld[1].z));
			a_root.insert_or_assign(
				"camera_view_to_world_row2",
				TomlVector3(
					camera.data.ViewToWorld[2].x,
					camera.data.ViewToWorld[2].y,
					camera.data.ViewToWorld[2].z));
		}

		[[nodiscard]] bool IsIdentity(const __m128 (&a_matrix)[4]) noexcept
		{
			alignas(16) float rows[4][4]{};
			for (std::size_t row = 0; row < 4; ++row) {
				_mm_store_ps(rows[row], a_matrix[row]);
			}
			for (std::size_t row = 0; row < 4; ++row) {
				for (std::size_t column = 0; column < 4; ++column) {
					const float expected = row == column ? 1.0f : 0.0f;
					if (std::abs(rows[row][column] - expected) > 1e-5f) {
						return false;
					}
				}
			}
			return true;
		}

		void CollectFrameBuffer(Sink& a_sink)
		{
			const auto status = cs::engine::GetFrameBufferStatus();
			const auto& published = cs::engine::GetFrameBuffer();
			const auto& latest = cs::engine::GetLatestFrameBuffer();
			const auto publishedOrigin =
				cs::engine::CameraWorldOrigin(published.data);
			const auto fullscreenLightOrigin =
				cs::engine::CameraWorldOrigin(status.fullscreenLight.data);
			a_sink
				.Field("hook_installed", status.hookInstalled)
				.Field("hooked_context_is_current", status.hookedContextIsCurrent)
				.Field("identified", status.identified)
				.Field("identity_source", std::string_view(status.identitySource))
				.Field("context_matches_slot12", status.contextMatchesSlot12)
				.Field("byte_width", static_cast<std::int64_t>(status.byteWidth))
				.Field("usage", static_cast<std::int64_t>(status.usage))
				.Field("cpu_access_flags", static_cast<std::int64_t>(status.cpuAccessFlags))
				.Field("bind_flags", static_cast<std::int64_t>(status.bindFlags))
				.Field("snapshots", TomlInteger(status.snapshots))
				.Field("maps_last_frame", static_cast<std::int64_t>(status.mapsLastFrame))
				.Field("maps_max_per_frame", static_cast<std::int64_t>(status.maxMapsPerFrame))
				.Field("latest_valid", status.latestSnapshotValid)
				.Field("latest_sequence", TomlInteger(status.latestSequence))
				.Field("latest_frame", static_cast<std::int64_t>(status.latestFrameCount))
				.Field(
					"latest_is_perspective",
					cs::engine::IsPerspectiveProjection(latest.data.CurrFrameWorldToClip[3]))
				.Field("latest_camera_pos_adjust", FormatFloat4(latest.data.CameraPosAdjust))
				.Field("latest_view_to_world_row0", FormatFloat4(latest.data.ViewToWorld[0]))
				.Field("latest_view_to_world_row1", FormatFloat4(latest.data.ViewToWorld[1]))
				.Field("latest_view_to_world_row2", FormatFloat4(latest.data.ViewToWorld[2]))
				.Field("published_valid", status.publishedSnapshotValid)
				.Field("published_this_frame", status.publishedThisFrame)
				.Field("published_sequence", TomlInteger(status.publishedSequence))
				.Field(
					"published_frame",
					static_cast<std::int64_t>(status.publishedFrameCount))
				.Field(
					"publish_source",
					std::string_view(
						cs::engine::FrameBufferPublishSourceName(status.publishSource)))
				.Field(
					"last_reject_reason",
					std::string_view(
						cs::engine::FrameBufferRejectReasonName(status.lastRejectReason)))
				.Field(
					"fullscreen_light_anchors",
					static_cast<std::int64_t>(status.fullscreenLightAnchorsThisFrame))
				.Field(
					"publications",
					static_cast<std::int64_t>(status.publicationsThisFrame))
				.Field(
					"rejections",
					static_cast<std::int64_t>(status.rejectionsThisFrame))
				.Field(
					"distinct_cameras",
					static_cast<std::int64_t>(status.distinctCamerasThisFrame))
				.Field(
					"minimum_origin_magnitude",
					static_cast<double>(status.minimumOriginMagnitude))
				.Field(
					"published_camera_hash",
					FormatHex64(status.publishedCameraHash))
				.Field("completed_frames", TomlInteger(status.completedFrames))
				.Field("publication_frames", TomlInteger(status.publicationFrames))
				.Field("no_publication_frames", TomlInteger(status.noPublicationFrames))
				.Field(
					"near_zero_origin_rejections",
					TomlInteger(status.nearZeroOriginRejections))
				.Field(
					"published_is_perspective",
					cs::engine::IsPerspectiveProjection(
						published.data.CurrFrameWorldToClip[3]))
				.Field(
					"published_camera_origin",
					FormatVector3(
						publishedOrigin.x,
						publishedOrigin.y,
						publishedOrigin.z))
				.Field(
					"published_camera_pos_adjust",
					FormatFloat4(published.data.CameraPosAdjust))
				.Field(
					"published_view_to_world_row0",
					FormatFloat4(published.data.ViewToWorld[0]))
				.Field(
					"published_view_to_world_row1",
					FormatFloat4(published.data.ViewToWorld[1]))
				.Field(
					"published_view_to_world_row2",
					FormatFloat4(published.data.ViewToWorld[2]))
				.Field("fullscreen_light_valid", status.fullscreenLight.valid)
				.Field(
					"fullscreen_light_sequence",
					TomlInteger(status.fullscreenLight.sequence))
				.Field(
					"fullscreen_light_origin",
					FormatVector3(
						fullscreenLightOrigin.x,
						fullscreenLightOrigin.y,
						fullscreenLightOrigin.z))
				.Field(
					"fullscreen_light_row0",
					FormatFloat4(status.fullscreenLight.data.ViewToWorld[0]))
				.Field(
					"fullscreen_light_row1",
					FormatFloat4(status.fullscreenLight.data.ViewToWorld[1]))
				.Field(
					"fullscreen_light_row2",
					FormatFloat4(status.fullscreenLight.data.ViewToWorld[2]));

			// The engine-struct reads exist only to be compared against the snapshot.
			auto* state = cs::engine::GetGraphicsState();
			if (!state) {
				return;
			}
			const auto& cameraState = state->cameraState;
			a_sink
				.Field(
					"state_pos_adjust",
					FormatVector3(
						cameraState.posAdjust.x,
						cameraState.posAdjust.y,
						cameraState.posAdjust.z))
				.Field(
					"state_prev_pos_adjust",
					FormatVector3(
						cameraState.previousPosAdjust.x,
						cameraState.previousPosAdjust.y,
						cameraState.previousPosAdjust.z))
				.Field("state_view_mat_identity", IsIdentity(cameraState.camViewData.viewMat));
		}

		void CollectFrameBufferRegisters(Sink& a_sink)
		{
			const auto& snapshot = cs::engine::GetFrameBuffer();
			for (std::size_t index = 0; index < cs::engine::kFrameBufferRegisters; ++index) {
				char key[16]{};
				const auto written = std::snprintf(key, sizeof(key), "b12_%02zu", index);
				if (written <= 0) {
					continue;
				}
				a_sink.Field(
					std::string_view(key, static_cast<std::size_t>(written)),
					FormatFloat4(cs::engine::FrameBufferRegister(snapshot.data, index)));
			}
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
		static void DumpAll();

		void Tick()
		{
			const auto frame = g_frame.fetch_add(1, std::memory_order_relaxed) + 1;
			if (g_dumpRequested.exchange(false, std::memory_order_acq_rel)) {
				DumpAll();
			}
			if (!g_enabled.load(std::memory_order_relaxed))
				return;

			const auto now = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch())
					.count());
			auto lastEmit = g_lastEmitMilliseconds.load(std::memory_order_relaxed);
			if (lastEmit == 0) {
				g_lastEmitMilliseconds.compare_exchange_strong(
					lastEmit, now, std::memory_order_relaxed);
				return;
			}
			const auto intervalMilliseconds =
				static_cast<std::uint64_t>(
					g_intervalSeconds.load(std::memory_order_relaxed))
				* 1000;
			if (now - lastEmit < intervalMilliseconds
				|| !g_lastEmitMilliseconds.compare_exchange_strong(
					lastEmit, now, std::memory_order_relaxed)) {
				return;
			}

			auto* logger = cs::log::Get("cs.telemetry");
			try {
				Sink sink;
				CollectFrameBuffer(sink);
				logger->info("frame={} component=frame_buffer {}", frame, sink.ToLine());
			} catch (const std::exception& e) {
				CS_LOG_ONCE(
					logger,
					spdlog::level::warn,
					"Telemetry collection failed for frame_buffer: {}",
					e.what());
			} catch (...) {
				CS_LOG_ONCE(
					logger,
					spdlog::level::warn,
					"Telemetry collection failed for frame_buffer: non-standard exception");
			}
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

		static void DumpAll()
		{
			auto* logger = cs::log::Get("cs.telemetry");
			try {
				toml::table root;
				root.insert_or_assign("build", CS_BUILD_DESCRIBE);
				CollectDumpContext(root);
				root.insert_or_assign("frame", TomlInteger(CurrentFrame()));
				root.insert_or_assign("logging", cs::log::ConfigAsToml());

				Sink shaderInjection;
				CollectShaderInjection(shaderInjection);
				root.insert_or_assign(
					"shader_injection",
					shaderInjection.AsTable());

				Sink frameBuffer;
				CollectFrameBuffer(frameBuffer);
				CollectFrameBufferRegisters(frameBuffer);
				root.insert_or_assign("frame_buffer", frameBuffer.AsTable());

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

		void RequestDump() noexcept
		{
			g_dumpRequested.store(true, std::memory_order_release);
		}

		void SetEnabled(bool a_enabled)
		{
			const bool enabled =
				a_enabled
				&& g_compositeSamplingAvailable.load(std::memory_order_relaxed);
			if (g_enabled.exchange(enabled, std::memory_order_relaxed) != enabled)
				g_lastEmitMilliseconds.store(0, std::memory_order_relaxed);
		}

		void SetIntervalSeconds(std::uint32_t a_interval)
		{
			g_intervalSeconds.store(a_interval == 0 ? 1 : a_interval, std::memory_order_relaxed);
		}

		bool Enabled() noexcept
		{
			return g_enabled.load(std::memory_order_relaxed);
		}

		std::uint32_t IntervalSeconds() noexcept
		{
			return g_intervalSeconds.load(std::memory_order_relaxed);
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
