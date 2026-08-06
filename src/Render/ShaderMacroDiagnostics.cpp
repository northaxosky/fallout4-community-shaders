#include "Render/ShaderMacroDiagnostics.h"

#include "Log.h"
#include "LogThrottle.h"
#include "PCH.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/ShaderMacroDiagnosticsModel.h"
#include "Render/ShaderSubclassHooks.h"

#include <array>
#include <atomic>
#include <cstring>
#include <exception>
#include <mutex>
#include <string_view>

namespace cs::engine
{
	namespace
	{
		struct EngineShaderMacro
		{
			const char* name = nullptr;
			const char* value = nullptr;
		};

		constexpr std::size_t kMacroCapacity = 64;
		constexpr std::size_t kMacroTextCapacity = 1024;
		constexpr std::size_t kPermutationCapacity = 512;

		struct PendingObservation
		{
			std::uint32_t rawTechnique = 0;
			std::uint32_t pluginPsid = 0;
			std::uint32_t enginePsid = 0;
			std::size_t macroCount = 0;
			std::size_t macroTextLength = 0;
			bool pluginPsidKnown = false;
			bool enginePsidKnown = false;
			std::array<char, kMacroTextCapacity> macroText{};
		};

		struct PermutationRecord
		{
			std::uint32_t rawTechnique = 0;
			std::size_t macroSetIndex = 0;
			std::size_t macroTextLength = 0;
			std::array<char, kMacroTextCapacity> macroText{};
		};

		struct DistinctRecordResult
		{
			std::size_t routeIndex = 0;
			std::size_t macroSetIndex = 0;
			bool capacityExceeded = false;
		};

		auto* L = cs::log::Get("cs.render.shadermacros");
		thread_local PendingObservation g_pending;
		std::array<PermutationRecord, kPermutationCapacity> g_permutations;
		std::mutex g_permutationMutex;
		std::size_t g_permutationCount = 0;
		std::size_t g_macroSetCount = 0;
		std::atomic<std::uint64_t> g_creationCount{ 0 };
		std::atomic<std::uint64_t> g_setupCount{ 0 };
		std::atomic<std::uint64_t> g_compositeSetupCount{ 0 };
		std::atomic<std::size_t> g_compositeDistinctCount{ 0 };
		std::atomic<std::size_t> g_distinctCount{ 0 };
		std::atomic<std::size_t> g_distinctMacroSetCount{ 0 };
		std::atomic<std::size_t> g_setupDistinctCount{ 0 };
		std::atomic<std::size_t> g_setupDistinctMacroSetCount{ 0 };
		std::mutex g_setupMutex;
		LightSetupTupleStore g_lightSetupTuples;
		CompositeSetupTupleStore g_compositeSetupTuples;
		std::atomic_flag g_lightSetupOverflowLogged = ATOMIC_FLAG_INIT;
		std::atomic_flag g_compositeSetupOverflowLogged = ATOMIC_FLAG_INIT;
		std::atomic_flag g_setupFailureLogged = ATOMIC_FLAG_INIT;

		bool AppendText(
			std::array<char, kMacroTextCapacity>& a_output,
			std::size_t& a_length,
			std::string_view a_text) noexcept
		{
			if (a_text.size() > a_output.size() - a_length - 1)
				return false;
			std::memcpy(
				a_output.data() + a_length,
				a_text.data(),
				a_text.size());
			a_length += a_text.size();
			a_output[a_length] = '\0';
			return true;
		}

		bool FormatMacroTable(
			const std::array<EngineShaderMacro, kMacroCapacity>& a_macros,
			PendingObservation& a_observation) noexcept
		{
			for (std::size_t index = 0; index < a_macros.size(); ++index) {
				const auto& macro = a_macros[index];
				if (!macro.name) {
					a_observation.macroCount = index;
					return true;
				}
				if (index != 0
					&& !AppendText(
						a_observation.macroText,
						a_observation.macroTextLength,
						", ")) {
					return false;
				}
				if (!AppendText(
						a_observation.macroText,
						a_observation.macroTextLength,
						macro.name)
					|| !AppendText(
						a_observation.macroText,
						a_observation.macroTextLength,
						"=")
					|| !AppendText(
						a_observation.macroText,
						a_observation.macroTextLength,
						macro.value && macro.value[0] != '\0'
							? std::string_view(macro.value)
							: std::string_view("<empty>"))) {
					return false;
				}
			}
			return false;
		}

		bool CaptureMacroViews(
			const std::array<EngineShaderMacro, kMacroCapacity>& a_macros,
			std::array<ShaderMacroDefinitionView, kMacroCapacity>& a_output,
			std::size_t& a_count) noexcept
		{
			for (std::size_t index = 0; index < a_macros.size(); ++index) {
				const auto& macro = a_macros[index];
				if (!macro.name) {
					a_count = index;
					return true;
				}
				a_output[index] = {
					.name = macro.name,
					.value = macro.value ? macro.value : ""
				};
			}
			return false;
		}

		std::optional<std::uint32_t> EnginePsid(
			const EnginePixelShaderLookupCorrelationResult&
				a_correlation) noexcept
		{
			if (!a_correlation.observation)
				return std::nullopt;
			return a_correlation.observation->returnedPsid.Value();
		}

		std::optional<std::uint32_t> PluginPsid(
			const ShaderSubclassSetupObservation& a_observation) noexcept
		{
			if (!a_observation.pluginResolvedPsid)
				return std::nullopt;
			return a_observation.pluginResolvedPsid->Value();
		}

		void LogSetupOverflow(
			std::atomic_flag& a_logged,
			std::string_view a_family,
			std::size_t a_capacity) noexcept
		{
			if (!a_logged.test_and_set(std::memory_order_relaxed)) {
				L->error(
					"{} setup diagnostic exceeded {} distinct tuples.",
					a_family,
					a_capacity);
			}
		}

		void LogSetupFailure(std::string_view a_reason) noexcept
		{
			if (!g_setupFailureLogged.test_and_set(
					std::memory_order_relaxed)) {
				L->error(
					"Setup diagnostic could not retain or format a tuple: {}",
					a_reason);
			}
		}

		DistinctRecordResult RecordDistinct(
			const PendingObservation& a_observation) noexcept
		{
			std::scoped_lock lock(g_permutationMutex);
			std::size_t macroSetIndex = 0;
			for (std::size_t index = 0;
				index < g_permutationCount;
				++index) {
				const auto& record = g_permutations[index];
				const bool sameMacroTable =
					record.macroTextLength
						== a_observation.macroTextLength
					&& std::memcmp(
						record.macroText.data(),
						a_observation.macroText.data(),
						record.macroTextLength)
						== 0;
				if (!sameMacroTable)
					continue;
				macroSetIndex = record.macroSetIndex;
				if (record.rawTechnique == a_observation.rawTechnique) {
					return {
						.macroSetIndex = macroSetIndex
					};
				}
			}

			if (g_permutationCount == g_permutations.size())
				return { .capacityExceeded = true };

			if (macroSetIndex == 0) {
				macroSetIndex = ++g_macroSetCount;
				g_distinctMacroSetCount.store(
					g_macroSetCount, std::memory_order_release);
			}

			auto& record = g_permutations[g_permutationCount];
			record.rawTechnique = a_observation.rawTechnique;
			record.macroSetIndex = macroSetIndex;
			record.macroTextLength = a_observation.macroTextLength;
			std::memcpy(
				record.macroText.data(),
				a_observation.macroText.data(),
				a_observation.macroTextLength + 1);
			++g_permutationCount;
			g_distinctCount.store(
				g_permutationCount, std::memory_order_release);
			return {
				.routeIndex = g_permutationCount,
				.macroSetIndex = macroSetIndex
			};
		}

		void* PrepareMacroObservation(
			const PixelShaderCreationDescriptor& a_descriptor) noexcept
		{
			if (!a_descriptor.route
				|| a_descriptor.route->subclass != "BSDFLightShader"
				|| a_descriptor.route->stage != ShaderStage::kPixel) {
				return nullptr;
			}

			g_pending = {};
			g_pending.rawTechnique =
				a_descriptor.route->rawTechnique;
			if (a_descriptor.route->pluginResolvedPsid) {
				g_pending.pluginPsidKnown = true;
				g_pending.pluginPsid =
					a_descriptor.route->pluginResolvedPsid->Value();
			}
			if (a_descriptor.route->engineLookup) {
				g_pending.enginePsidKnown = true;
				g_pending.enginePsid =
					a_descriptor.route->engineLookup->returnedPsid.Value();
			}

			std::array<EngineShaderMacro, kMacroCapacity> macros{};
			static REL::Relocation<
				EngineShaderMacro*(
					std::uint32_t,
					EngineShaderMacro*)>
				getLightMacros{
					REL::ID({ 825685, 2319657, 2319657 })
				};
			const auto* result = getLightMacros(
				g_pending.rawTechnique, macros.data());
			if (result != macros.data()) {
				CS_LOG_ONCE(
					L,
					spdlog::level::err,
					"Light macro emitter returned an unexpected buffer.");
				return nullptr;
			}
			if (!FormatMacroTable(macros, g_pending)) {
				CS_LOG_ONCE(
					L,
					spdlog::level::err,
					"Light macro emitter output exceeded diagnostic capacity.");
				return nullptr;
			}
			return &g_pending;
		}

		void CompleteMacroObservation(
			void* a_token,
			const PixelShaderSwapCompletion& a_completion) noexcept
		{
			if (!a_token
				|| a_completion.originalResult < 0
				|| !a_completion.stockOutput) {
				return;
			}

			const auto& observation =
				*static_cast<const PendingObservation*>(a_token);
			const auto creationCount =
				g_creationCount.fetch_add(
					1, std::memory_order_relaxed) + 1;
			const auto distinct = RecordDistinct(observation);
			if (distinct.capacityExceeded) {
				CS_LOG_ONCE(
					L,
					spdlog::level::err,
					"Light macro diagnostic exceeded {} distinct permutations.",
					g_permutations.size());
			} else if (distinct.routeIndex != 0) {
				L->info(
					"Light macro permutation #{} (macro_set #{}): "
					"raw=0x{:08X}, "
					"plugin_psid_known={}, plugin_psid=0x{:08X}, "
					"engine_psid_known={}, engine_psid=0x{:08X}, "
					"macro_count={}, macros=[{}]",
					distinct.routeIndex,
					distinct.macroSetIndex,
					observation.rawTechnique,
					observation.pluginPsidKnown,
					observation.pluginPsid,
					observation.enginePsidKnown,
					observation.enginePsid,
					observation.macroCount,
					observation.macroText.data());
			}

			if (L->should_log(spdlog::level::info)) {
				CS_LOG_EVERY_MS(
					L,
					5000,
					spdlog::level::info,
					"Light macro diagnostic: creations={}, "
					"distinct_routes={}, distinct_macro_sets={}",
					creationCount,
					g_distinctCount.load(std::memory_order_acquire),
					g_distinctMacroSetCount.load(
						std::memory_order_acquire));
			}
		}
		void ObserveCompositeTechniqueSetup(
			const ShaderSubclassSetupObservation& a_observation) noexcept
		{
			const auto setupCount =
				g_compositeSetupCount.fetch_add(
					1, std::memory_order_relaxed) + 1;
			const CompositeSetupTupleKeyView key{
				.subclass = a_observation.subclass,
				.rawTechnique = a_observation.rawTechnique,
				.engineLookupPsid =
					EnginePsid(a_observation.engineLookupCorrelation),
				.pluginResolvedPsid = PluginPsid(a_observation),
				.correlationStatus =
					a_observation.engineLookupCorrelation.status,
				.correlationReason =
					a_observation.engineLookupCorrelation.reason,
				.tiledLighting = a_observation.tiledLighting,
				.rgbspecGlobalByte = std::nullopt
			};
			try {
				ShaderMacroDiagnosticsInsert inserted;
				{
					std::scoped_lock lock(g_setupMutex);
					inserted = g_compositeSetupTuples.Insert(key);
					if (inserted.result
						== ShaderMacroDiagnosticsInsertResult::kFirstSight) {
						g_compositeDistinctCount.store(
							g_compositeSetupTuples.Size(),
							std::memory_order_release);
					}
				}
				if (inserted.result
					== ShaderMacroDiagnosticsInsertResult::kFirstSight) {
					L->info("{}", FormatCompositeRuntimeHalfLine(key));
				} else if (inserted.result
					== ShaderMacroDiagnosticsInsertResult::
						kCapacityExceeded) {
					LogSetupOverflow(
						g_compositeSetupOverflowLogged,
						"Composite",
						kCompositeSetupTupleCapacity);
				}
			} catch (const std::exception& e) {
				LogSetupFailure(e.what());
			}

			if (L->should_log(spdlog::level::info)) {
				CS_LOG_EVERY_MS(
					L,
					5000,
					spdlog::level::info,
					"Composite setup diagnostic: setups={}, "
					"distinct_routes={}",
					setupCount,
					g_compositeDistinctCount.load(
						std::memory_order_acquire));
			}
		}

		void ObserveLightTechniqueSetup(
			const ShaderSubclassSetupObservation& a_observation) noexcept
		{
			if (a_observation.subclass == "BSDFCompositeShader") {
				ObserveCompositeTechniqueSetup(a_observation);
				return;
			}
			if (a_observation.subclass != "BSDFLightShader")
				return;

			const auto setupCount =
				g_setupCount.fetch_add(1, std::memory_order_relaxed) + 1;

			std::array<EngineShaderMacro, kMacroCapacity> macros{};
			static REL::Relocation<
				EngineShaderMacro*(
					std::uint32_t,
					EngineShaderMacro*)>
				getLightMacros{
					REL::ID({ 825685, 2319657, 2319657 })
				};
			if (getLightMacros(
					a_observation.rawTechnique, macros.data())
				!= macros.data()) {
				CS_LOG_ONCE(
					L,
					spdlog::level::err,
					"Light macro emitter returned an unexpected buffer "
					"at setup.");
				return;
			}
			std::array<ShaderMacroDefinitionView, kMacroCapacity> macroViews{};
			std::size_t macroCount = 0;
			if (!CaptureMacroViews(macros, macroViews, macroCount)) {
				CS_LOG_ONCE(
					L,
					spdlog::level::err,
					"Light macro emitter output exceeded diagnostic "
					"capacity at setup.");
				return;
			}

			const LightSetupTupleKeyView key{
				.subclass = a_observation.subclass,
				.rawTechnique = a_observation.rawTechnique,
				.engineLookupPsid =
					EnginePsid(a_observation.engineLookupCorrelation),
				.pluginResolvedPsid = PluginPsid(a_observation),
				.correlationStatus =
					a_observation.engineLookupCorrelation.status,
				.correlationReason =
					a_observation.engineLookupCorrelation.reason,
				.macros = std::span(
					macroViews.data(), macroCount)
			};
			try {
				ShaderMacroDiagnosticsInsert inserted;
				{
					std::scoped_lock lock(g_setupMutex);
					inserted = g_lightSetupTuples.Insert(key);
					if (inserted.result
						== ShaderMacroDiagnosticsInsertResult::kFirstSight) {
						g_setupDistinctCount.store(
							g_lightSetupTuples.Size(),
							std::memory_order_release);
						g_setupDistinctMacroSetCount.store(
							g_lightSetupTuples.MacroSetCount(),
							std::memory_order_release);
					}
				}
				if (inserted.result
					== ShaderMacroDiagnosticsInsertResult::kFirstSight) {
					L->info("{}", FormatLightRuntimeHalfLine(key));
				} else if (inserted.result
					== ShaderMacroDiagnosticsInsertResult::
						kCapacityExceeded) {
					LogSetupOverflow(
						g_lightSetupOverflowLogged,
						"Light",
						kLightSetupTupleCapacity);
				}
			} catch (const std::exception& e) {
				LogSetupFailure(e.what());
			}

			if (L->should_log(spdlog::level::info)) {
				CS_LOG_EVERY_MS(
					L,
					5000,
					spdlog::level::info,
					"Light setup diagnostic: setups={}, distinct_routes={}, "
					"distinct_macro_sets={}",
					setupCount,
					g_setupDistinctCount.load(std::memory_order_acquire),
					g_setupDistinctMacroSetCount.load(
						std::memory_order_acquire));
			}
		}
	}

	void InstallShaderMacroDiagnostics()
	{
		if (!RegisterPixelShaderSwapObserver({
				.complete = &CompleteMacroObservation,
				.prepareDetailed = &PrepareMacroObservation })) {
			L->error("Failed to register Light macro diagnostic observer.");
			return;
		}
		if (!RegisterShaderSubclassSetupObserver(
				&ObserveLightTechniqueSetup)) {
			L->error(
				"Failed to register Light setup-technique diagnostic "
				"observer.");
			return;
		}
		L->info(
			"Light macro diagnostic registered at creation and setup "
			"(emitter REL::ID {{825685, 2319657, 2319657}}).");
	}
}
