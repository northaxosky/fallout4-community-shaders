#include "Render/PrepassLane.h"

#include "Feature.h"
#include "Log.h"
#include "PCH.h"
#include "Render/PrepassTechnique.h"
#include "Render/ShaderHookSupport.h"
#include "Telemetry/Telemetry.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <format>

#include "RE/B/BSDFPrePassShader.h"
#include "RE/B/BSGeometry.h"
#include "RE/B/BSRenderPass.h"
#include "RE/B/BSShaderProperty.h"
#include "RE/N/NiRTTI.h"

namespace cs::engine
{
	RE::BSLightingShaderProperty* PrepassDrawContext::LightingProperty() noexcept
	{
		if (!_lightingProperty)
			_lightingProperty = netimmerse_cast<RE::BSLightingShaderProperty*>(pass->GetShaderProperty());
		return *_lightingProperty;
	}

	namespace
	{
		auto* L = cs::log::Get("cs.render.prepasslane");

		constexpr std::size_t kPrepassFlagSlots = 32;
		constexpr std::size_t kPrepassBakePaths = 2;

		struct ClassifierCounts
		{
			std::atomic<std::uint64_t> classified{ 0 };
			std::atomic<std::uint64_t> flagged{ 0 };
		};

		template <class Classifier>
		struct Registered
		{
			Classifier classify = nullptr;
			Feature* owner = nullptr;
		};

		// Written only during Load, before rendering, so reads need no lock.
		std::array<Registered<PrepassDrawClassifier>, kPrepassFlagSlots> g_flagClassifiers{};
		Registered<PrepassRecordClassifier> g_recordClassifier;
		std::uint32_t g_activeFlags = 0;
		bool g_prepassLaneActive = false;
		std::array<std::array<ClassifierCounts, kPrepassBakePaths>, kPrepassFlagSlots> g_classifierCounts;
		std::atomic<std::uint64_t> g_laneBaked{ 0 };
		std::atomic<std::uint64_t> g_laneUnavailable{ 0 };

		// Stock never reads the cb2 pad before material flags.
		constexpr std::size_t kPrepassMaterialFlagsSlot = 0x60 - 0x58;
		constexpr std::uint64_t kLaneDiagnosticLimit = 16;
		// Landscape reuses the slot as WetnessControl_Reflect, which stock reads.
		constexpr std::uint32_t kPrepassLandscapeBit = prepass_technique::kLandscape;
		constexpr std::uint32_t kPerGeometryConstantLevel = 2;
		constexpr std::size_t kLaneDwords = sizeof(PrepassLanePayload) / sizeof(float);

		struct PrepassLaneRequest
		{
			PrepassBakePath path;
			std::uint32_t descriptor;
			PrepassLanePayload payload;
			std::int32_t offset;  // -1 when unresolved
		};

		thread_local PrepassLaneRequest* t_prepassLane = nullptr;

		[[nodiscard]] std::size_t FlagSlot(PrepassDrawFlag a_flag) noexcept
		{
			return static_cast<std::size_t>(std::countr_zero(static_cast<std::uint32_t>(a_flag)));
		}

		[[nodiscard]] std::int32_t PrepassLaneOffset(const RE::BSGraphics::PixelShader* a_pixel) noexcept
		{
			if (!a_pixel)
				return -1;
			// 0xFF marks a constant this variant lacks.
			const auto flags = static_cast<std::uint8_t>(a_pixel->constantTable[kPrepassMaterialFlagsSlot]);
			return flags != 0xFF && flags >= 4 ? flags - 4 : -1;
		}

		void CountLane(bool a_baked, std::uint32_t a_descriptor, std::int32_t a_offset) noexcept
		{
			if (a_baked) {
				g_laneBaked.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			if (g_laneUnavailable.fetch_add(1, std::memory_order_relaxed) < kLaneDiagnosticLimit)
				L->warn("Prepass lane unavailable: descriptor=0x{:08X} offset={}", a_descriptor, a_offset);
		}

		void WriteLanePayload(float* a_destination, const PrepassLanePayload& a_payload) noexcept
		{
			std::memcpy(a_destination, &a_payload, sizeof(a_payload));
		}

		[[nodiscard]] const RE::BSGraphics::PixelShader* FindPrepassPixelShader(
			const RE::BSShader* a_shader,
			std::uint32_t a_descriptor) noexcept
		{
			RE::BSGraphics::PixelShader key;
			key.id = RE::BSDFPrePassShader::GetPixelShaderID(a_descriptor);
			auto* keyPointer = &key;
			const auto& shaders = a_shader->GetPixelShaders();
			const auto found = shaders.find(keyPointer);
			return found != shaders.end() ? *found : nullptr;
		}

		template <class Registration>
		[[nodiscard]] bool Runs(const Registration& a_registration, PrepassBakePath a_path) noexcept
		{
			return a_registration.classify && (a_path == PrepassBakePath::kCommandBuffer || a_registration.owner->IsEnabled());
		}

		// Constants persist between draws, so unused components are zeroed.
		[[nodiscard]] PrepassLaneRequest MakePrepassLaneRequest(
			RE::BSShader* a_shader,
			RE::BSRenderPass* a_pass,
			PrepassBakePath a_path) noexcept
		{
			PrepassLaneRequest request{
				.path = a_path,
				.descriptor = a_pass->passEnum,
				.payload = {},
				.offset = PrepassLaneOffset(FindPrepassPixelShader(a_shader, a_pass->passEnum))
			};
			PrepassDrawContext draw(a_pass, a_path);
			for (auto pending = g_activeFlags; pending; pending &= pending - 1) {
				const auto slot = static_cast<std::size_t>(std::countr_zero(pending));
				const auto& registration = g_flagClassifiers[slot];
				if (!Runs(registration, a_path))
					continue;
				const bool flagged = registration.classify(draw);
				if (telemetry::pump::Enabled()) {
					auto& counts = g_classifierCounts[slot][static_cast<std::size_t>(a_path)];
					counts.classified.fetch_add(1, std::memory_order_relaxed);
					counts.flagged.fetch_add(flagged ? 1u : 0u, std::memory_order_relaxed);
				}
				if (flagged)
					request.payload.flags |= 1u << slot;
			}
			if (Runs(g_recordClassifier, a_path))
				request.payload.recordId = g_recordClassifier.classify(draw);
			return request;
		}

		// Each runtime inlines the other's write point: NG/AE map, OG flush.
		struct PrepassSetupGeometryHook
		{
			static constexpr std::size_t size = 0x07;

			static void thunk(
				RE::BSShader* a_self,
				RE::BSRenderPass* a_pass)
			{
				if (!g_prepassLaneActive || !a_pass || (a_pass->passEnum & kPrepassLandscapeBit) != 0) {
					func(a_self, a_pass);
					return;
				}
				auto request = MakePrepassLaneRequest(a_self, a_pass, PrepassBakePath::kImmediate);
				const ThreadLocalPointerScope scope(t_prepassLane, &request);
				func(a_self, a_pass);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		void WriteImmediateLane(const RE::BSGraphics::ConstantGroup* a_group) noexcept
		{
			const auto* request = t_prepassLane;
			if (!request || request->path != PrepassBakePath::kImmediate)
				return;
			const bool fits = request->offset >= 0 && a_group && a_group->data;
			if (fits)
				WriteLanePayload(a_group->data + request->offset, request->payload);
			CountLane(fits, request->descriptor, request->offset);
		}

		struct PixelConstantMapHook
		{
			static RE::BSGraphics::ConstantGroup* thunk(
				RE::BSGraphics::Renderer* a_renderer,
				RE::BSGraphics::PixelShader* a_pixel,
				std::uint32_t a_level)
			{
				auto* group = func(a_renderer, a_pixel, a_level);
				if (a_level == kPerGeometryConstantLevel)
					WriteImmediateLane(group);
				return group;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		// OG-only: its unmap runs after SetupGeometry fills cb2.
		struct ConstantGroupFlushHook
		{
			static void thunk(
				RE::BSGraphics::Renderer* a_renderer,
				RE::BSGraphics::ConstantGroup* a_vertex,
				RE::BSGraphics::ConstantGroup* a_pixel)
			{
				WriteImmediateLane(a_pixel);
				func(a_renderer, a_vertex, a_pixel);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		// The record is immutable once baked, so write before the copy.
		struct PrepassCreateCommandBufferHook
		{
			static std::byte* thunk(
				RE::BSShader* a_self,
				RE::BSRenderPass* a_pass)
			{
				if (!g_prepassLaneActive || !a_pass || (a_pass->passEnum & kPrepassLandscapeBit) != 0)
					return func(a_self, a_pass);
				auto request = MakePrepassLaneRequest(a_self, a_pass, PrepassBakePath::kCommandBuffer);
				const ThreadLocalPointerScope scope(t_prepassLane, &request);
				return func(a_self, a_pass);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BuildCommandBufferHook
		{
			static std::byte* thunk(
				RE::BSShader* a_self,
				RE::BSShader::BuildCommandBufferParam& a_param)
			{
				if (const auto* request = t_prepassLane; request && request->path == PrepassBakePath::kCommandBuffer) {
					const bool fits = request->offset >= 0 && a_param.pixelConstants &&
					                  static_cast<std::uint32_t>(request->offset) + kLaneDwords <= a_param.pixelRegisters * 4;
					if (fits)
						WriteLanePayload(a_param.pixelConstants + request->offset, request->payload);
					CountLane(fits, request->descriptor, request->offset);
				}
				return func(a_self, a_param);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		[[nodiscard]] bool InstallLaneHooks()
		{
			static const bool installed =
				TryPatch(
					L, "BSDFPrePassShader SetupGeometry lane",
					[] { stl::write_vfunc<RE::BSDFPrePassShader, PrepassSetupGeometryHook>(); }) &&
				TryPatch(
					L, "immediate pixel constants lane",
					[] {
						if (REX::FModule::IsRuntimeOG())
							stl::detour_thunk<ConstantGroupFlushHook>(RE::ID::BSGraphics::Renderer::FlushConstantGroup);
						else
							stl::detour_thunk<PixelConstantMapHook>(RE::ID::BSGraphics::Renderer::GetShaderConstantGroupPS);
					}) &&
				TryPatch(
					L, "BSDFPrePassShader command-buffer creator lane",
					[] { stl::detour_thunk<PrepassCreateCommandBufferHook>(RE::ID::BSDFPrePassShader::CreateCommandBuffer); }) &&
				TryPatch(
					L, "command-buffer builder lane",
					[] { stl::detour_thunk<BuildCommandBufferHook>(RE::ID::BSShader::BuildCommandBuffer); });
			return installed;
		}
	}

	bool RegisterPrepassDrawClassifier(Feature& a_owner, PrepassDrawFlag a_flag, PrepassDrawClassifier a_classifier)
	{
		const auto bits = static_cast<std::uint32_t>(a_flag);
		if (!InstallLaneHooks() || !a_classifier || !std::has_single_bit(bits))
			return false;
		auto& slot = g_flagClassifiers[FlagSlot(a_flag)];
		if (slot.classify)
			return false;
		slot = { a_classifier, &a_owner };
		g_activeFlags |= bits;
		g_prepassLaneActive = true;
		return true;
	}

	bool RegisterPrepassRecordClassifier(Feature& a_owner, PrepassRecordClassifier a_classifier)
	{
		if (!InstallLaneHooks() || !a_classifier || g_recordClassifier.classify)
			return false;
		g_recordClassifier = { a_classifier, &a_owner };
		g_prepassLaneActive = true;
		return true;
	}

	PrepassLaneStats GetPrepassLaneStats() noexcept
	{
		return { g_laneBaked.load(std::memory_order_relaxed), g_laneUnavailable.load(std::memory_order_relaxed) };
	}

	PrepassClassifierStats GetPrepassClassifierStats(PrepassDrawFlag a_flag, PrepassBakePath a_path) noexcept
	{
		const auto& counts = g_classifierCounts[FlagSlot(a_flag)][static_cast<std::size_t>(a_path)];
		return { counts.classified.load(std::memory_order_relaxed), counts.flagged.load(std::memory_order_relaxed) };
	}

	void WritePrepassLaneTelemetry(telemetry::Sink& a_sink, PrepassDrawFlag a_flag, std::string_view a_flagged)
	{
		const auto records = GetPrepassClassifierStats(a_flag, PrepassBakePath::kCommandBuffer);
		const auto immediate = GetPrepassClassifierStats(a_flag, PrepassBakePath::kImmediate);
		const auto lanes = GetPrepassLaneStats();
		a_sink
			.Field("records", records.classified)
			.Field(std::format("{}_records", a_flagged), records.flagged)
			.Field("immediate_draws", immediate.classified)
			.Field(std::format("{}_immediate_draws", a_flagged), immediate.flagged)
			.Field("lanes_baked", lanes.baked)
			.Field("lanes_unavailable", lanes.unavailable);
	}
}
