#include "Render/ShaderMacroDiagnostics.h"

#include "Log.h"
#include "LogThrottle.h"
#include "PCH.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/ShaderSubclassHooks.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstring>
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
		std::array<std::uint32_t, 256> g_compositeTechniques{};
		std::size_t g_compositeTechniqueCount = 0;
		std::atomic<std::size_t> g_distinctCount{ 0 };
		std::atomic<std::size_t> g_distinctMacroSetCount{ 0 };

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
		// The composite macro emitter is not lifted (engine-facts puts it at
		// AE 0x226C510 with unvalidated rules), so count raw techniques only.
		void ObserveCompositeTechniqueSetup(
			std::uint32_t a_techniqueBits) noexcept
		{
			const auto setupCount =
				g_compositeSetupCount.fetch_add(
					1, std::memory_order_relaxed) + 1;

			bool isNew = false;
			std::size_t index = 0;
			{
				std::scoped_lock lock(g_permutationMutex);
				const auto seen = std::find(
					g_compositeTechniques.begin(),
					g_compositeTechniques.begin()
						+ static_cast<std::ptrdiff_t>(
							g_compositeTechniqueCount),
					a_techniqueBits);
				if (seen
					== g_compositeTechniques.begin()
						+ static_cast<std::ptrdiff_t>(
							g_compositeTechniqueCount)) {
					if (g_compositeTechniqueCount
						< g_compositeTechniques.size()) {
						g_compositeTechniques[g_compositeTechniqueCount++] =
							a_techniqueBits;
						index = g_compositeTechniqueCount;
						isNew = true;
						g_compositeDistinctCount.store(
							g_compositeTechniqueCount,
							std::memory_order_release);
					}
				}
			}

			if (isNew) {
				L->info(
					"Composite setup permutation #{}: raw=0x{:08X}",
					index,
					a_techniqueBits);
			}

			if (L->should_log(spdlog::level::info)) {
				CS_LOG_EVERY_MS(
					L,
					5000,
					spdlog::level::info,
					"Composite setup diagnostic: setups={}, "
					"distinct_techniques={}",
					setupCount,
					g_compositeDistinctCount.load(
						std::memory_order_acquire));
			}
		}

		// Draw-time vantage: CreatePixelShader never sees most permutations
		// because the engine creates and caches them outside a technique-known
		// SetupTechnique scope. Every drawn permutation passes through here.
		void ObserveLightTechniqueSetup(
			void* /*a_shader*/,
			const char* a_subclassName,
			std::uint32_t a_techniqueBits) noexcept
		{
			if (!a_subclassName)
				return;
			const std::string_view subclass(a_subclassName);
			if (subclass == "BSDFCompositeShader") {
				ObserveCompositeTechniqueSetup(a_techniqueBits);
				return;
			}
			if (subclass != "BSDFLightShader")
				return;

			const auto setupCount =
				g_setupCount.fetch_add(1, std::memory_order_relaxed) + 1;

			thread_local PendingObservation observation;
			observation = PendingObservation{};
			observation.rawTechnique = a_techniqueBits;

			std::array<EngineShaderMacro, kMacroCapacity> macros{};
			static REL::Relocation<
				EngineShaderMacro*(
					std::uint32_t,
					EngineShaderMacro*)>
				getLightMacros{
					REL::ID({ 825685, 2319657, 2319657 })
				};
			if (getLightMacros(a_techniqueBits, macros.data())
				!= macros.data()) {
				CS_LOG_ONCE(
					L,
					spdlog::level::err,
					"Light macro emitter returned an unexpected buffer "
					"at setup.");
				return;
			}
			if (!FormatMacroTable(macros, observation)) {
				CS_LOG_ONCE(
					L,
					spdlog::level::err,
					"Light macro emitter output exceeded diagnostic "
					"capacity at setup.");
				return;
			}

			const auto distinct = RecordDistinct(observation);
			if (distinct.capacityExceeded) {
				CS_LOG_ONCE(
					L,
					spdlog::level::err,
					"Light macro diagnostic exceeded {} distinct "
					"permutations.",
					g_permutations.size());
			} else if (distinct.routeIndex != 0) {
				L->info(
					"Light setup permutation #{} (macro_set #{}): "
					"raw=0x{:08X}, macro_count={}, macros=[{}]",
					distinct.routeIndex,
					distinct.macroSetIndex,
					observation.rawTechnique,
					observation.macroCount,
					observation.macroText.data());
			}

			if (L->should_log(spdlog::level::info)) {
				CS_LOG_EVERY_MS(
					L,
					5000,
					spdlog::level::info,
					"Light setup diagnostic: setups={}, distinct_routes={}, "
					"distinct_macro_sets={}",
					setupCount,
					g_distinctCount.load(std::memory_order_acquire),
					g_distinctMacroSetCount.load(
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
